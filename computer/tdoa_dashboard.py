"""Run the computer-side uplink TDoA collector, solver and floor map."""

import argparse
import csv
import json
import math
import random
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlencode, urlsplit
from urllib.request import ProxyHandler, build_opener

from tdoa_solver import Layout, METERS_PER_TICK, TICK_HZ, TICK_MODULUS, Tracker, parse_event
from tdoa_zones import DangerZones


HERE = Path(__file__).resolve().parent
SERIAL_PREFIX = b"TDOA "
MAX_FRAME_BYTES = 4096
POSITION_COLUMNS = ("solved_at_utc", "tag_id", "tag_boot", "sequence", "x_m", "y_m", "residual_m", "anchor_ids")
TIMING_COLUMNS = ("processed_at_utc", "tag_id", "tag_boot", "sequence", "anchor_id", "reference_anchor_id", "delta_ns")
ENTRY_COLUMNS = ("detected_at_utc", "tag_id", "zone_id", "zone_name", "x_m", "y_m")


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class Recording:
    def __init__(self, directory, layout):
        self.directory = directory.resolve()
        self.layout = layout
        self.path = None
        self.raw = self.positions = self.timing = self.zone_entries = None
        self.event_count = self.fix_count = 0
        self.error = ""

    def start(self):
        if self.raw is not None:
            return
        self.error = ""
        try:
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
            path = self.directory / f"tdoa-{stamp}"
            path.mkdir(parents=True)
            self.path, self.event_count, self.fix_count = path, 0, 0
            (path / "layout.json").write_text(json.dumps(self.layout.as_json(), indent=2) + "\n", encoding="utf-8")
            self.positions = (path / "positions.csv").open("x", encoding="utf-8", newline="", buffering=1)
            csv.writer(self.positions).writerow(POSITION_COLUMNS)
            self.timing = (path / "timing.csv").open("x", encoding="utf-8", newline="", buffering=1)
            csv.writer(self.timing).writerow(TIMING_COLUMNS)
            self.zone_entries = (path / "zone-entries.csv").open("x", encoding="utf-8", newline="", buffering=1)
            csv.writer(self.zone_entries).writerow(ENTRY_COLUMNS)
            self.raw = (path / "events.jsonl").open("x", encoding="utf-8", buffering=1)
            self.started_at = time.monotonic()
        except OSError as error:
            self.error = str(error)
            self.stop()

    def stop(self):
        for handle in (self.raw, self.positions, self.timing, self.zone_entries):
            if handle is not None:
                try:
                    handle.close()
                except OSError as error:
                    self.error = str(error)
        self.raw = self.positions = self.timing = self.zone_entries = None

    def event(self, event, now):
        if self.raw is not None:
            try:
                self.raw.write(json.dumps({"elapsed_s": now - self.started_at, "event": event}) + "\n")
                self.event_count += 1
            except OSError as error:
                self.error = str(error)
                self.stop()

    def results(self, fixes, timing, entries):
        if self.raw is None:
            return
        try:
            writer = csv.writer(self.positions)
            for fix in fixes:
                if fix["at"] >= self.started_at:
                    writer.writerow((utc_now(), fix["tag"], fix["tag_boot"], fix["seq"], round(fix["x"], 4),
                                     round(fix["y"], 4), round(fix["rms"], 4), " ".join(map(str, fix["anchors"]))))
                    self.fix_count += 1
            writer = csv.writer(self.timing)
            for tag, boot, sequence, arrived, aligned in timing:
                if arrived < self.started_at:
                    continue
                reference = self.layout.main if self.layout.main in aligned else min(aligned)
                processed = utc_now()
                for anchor, ticks in sorted(aligned.items()):
                    if anchor != reference:
                        delta_ns = (ticks - aligned[reference]) / TICK_HZ * 1e9
                        writer.writerow((processed, tag, boot, sequence, anchor, reference, round(delta_ns, 6)))
            writer = csv.writer(self.zone_entries)
            for event in entries:
                writer.writerow((event["at"], event["tag"], event["zone_id"], event["zone_name"],
                                 round(event["x"], 4), round(event["y"], 4)))
        except OSError as error:
            self.error = str(error)
            self.stop()

    def status(self):
        return {"active": self.raw is not None, "path": str(self.path) if self.path else None,
                "directory": str(self.directory), "events": self.event_count, "fixes": self.fix_count, "error": self.error}


class Collector:
    def __init__(self, args, layout):
        self.args = args
        self.layout = layout
        self.tracker = Tracker(layout)
        self.recording = Recording(args.log_dir, layout)
        mode = "simulation" if args.simulate else "replay" if args.replay else "live"
        self.danger = DangerZones(args.log_dir.resolve(), layout, mode)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.source, self.status = ("Simulation" if args.simulate else "Replay" if args.replay else
                                    "Anchor USB" if args.port else "Anchor Wi-Fi"), "Starting"
        self.invalid = 0
        self.last_event = None
        self.sync_context = deque(maxlen=8)

    def set_status(self, status):
        with self.lock:
            self.status = status

    def ingest(self, value):
        try:
            event = parse_event(value)
        except (KeyError, TypeError, ValueError):
            with self.lock:
                self.invalid += 1
            return
        with self.lock:
            now = time.monotonic()
            self.last_event = now
            if self.tracker.boot != event["gateway_boot"]:
                self.sync_context.clear()
            if self.tracker.submit(event, now):
                if event["type"] == "sync":
                    self.sync_context.append((now, event))
                self.recording.event(event, now)
            self.process_fixes(now)

    def process_fixes(self, now):
        fixes, timing = self.tracker.update(now)
        entries = self.danger.observe(fixes, now, utc_now)
        self.recording.results(fixes, timing, entries)

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            self.process_fixes(now)
            return {**self.tracker.snapshot(now), "source": self.source, "status": self.status,
                    "event_age": None if self.last_event is None else now - self.last_event,
                    "invalid": self.invalid, "layout": self.layout.as_json(), "recording": self.recording.status(),
                    **self.danger.snapshot()}

    def edit_zone(self, action, identifier, data):
        with self.lock:
            if action == "create":
                self.danger.create(data)
            elif action == "rename":
                self.danger.rename(identifier, data.get("name"))
            else:
                self.danger.delete(identifier)
            return self.danger.snapshot()

    def set_recording(self, active):
        with self.lock:
            if active and self.recording.raw is None:
                self.recording.start()
                if self.recording.raw is not None:
                    for received, event in self.sync_context:
                        if self.recording.started_at - received < 2:
                            self.recording.event(event, self.recording.started_at)
            elif not active:
                self.recording.stop()
            return self.recording.status()

    def run(self):
        try:
            if self.args.simulate:
                self.simulate()
            elif self.args.replay:
                self.replay()
            elif self.args.port:
                self.read_serial()
            else:
                self.read_wifi()
        except (ImportError, OSError, KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def read_serial(self):
        import serial

        while not self.stop.is_set():
            try:
                with serial.Serial(baudrate=self.args.baud, timeout=0.3) as port:
                    port.port = self.args.port
                    port.dtr = port.rts = False
                    port.open()
                    self.set_status(f"Listening on {port.port}")
                    pending = b""
                    discarding = False
                    while not self.stop.is_set():
                        chunk = port.read_until(size=MAX_FRAME_BYTES)
                        if discarding:
                            discarding = not chunk.endswith(b"\n")
                            continue
                        pending += chunk
                        if pending.endswith(b"\n"):
                            if pending.startswith(SERIAL_PREFIX) and len(pending) <= MAX_FRAME_BYTES:
                                try:
                                    self.ingest(json.loads(pending[len(SERIAL_PREFIX):]))
                                except ValueError:
                                    self.ingest(None)
                            pending = b""
                        elif len(pending) >= MAX_FRAME_BYTES:
                            pending = b""
                            discarding = True
            except OSError as error:
                self.set_status(str(error))
                self.stop.wait(1.5)

    def read_wifi(self):
        opener = build_opener(ProxyHandler({}))
        boot, cursor = None, 0
        while not self.stop.is_set():
            try:
                query = urlencode({"after": cursor, **({"boot": boot} if boot is not None else {})})
                url = f"{self.args.anchor_url}/api/tdoa/events?{query}"
                with opener.open(url, timeout=1.5) as response:
                    data = json.loads(response.read(262145))
                new_boot, last, events = data["gateway_boot"], data["cursor"], data["events"]
                if (data["v"] != 1 or type(new_boot) is not int or not 0 <= new_boot < 1 << 32 or
                        type(last) is not int or last < (cursor if boot == new_boot else 0) or
                        not isinstance(events, list) or len(events) > 64 or
                        any(event.get("gateway_boot") != new_boot for event in events)):
                    raise ValueError("Unsupported gateway event page")
                boot, cursor = new_boot, last
                self.set_status(f"Connected to {self.args.anchor_url}")
                for event in events:
                    self.ingest(event)
            except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
                self.set_status(f"Waiting for TDoA gateway · {error}")
            self.stop.wait(0.25)

    def replay(self):
        self.set_status(f"Playing {self.args.replay.name}")
        started = time.monotonic()
        last = 0.0
        with self.args.replay.open(encoding="utf-8") as file:
            for line in file:
                if not line.strip():
                    continue
                entry = json.loads(line)
                elapsed = float(entry["elapsed_s"])
                if not math.isfinite(elapsed) or elapsed < last:
                    raise ValueError("Replay timestamps must be finite and nondecreasing")
                last = elapsed
                if self.stop.wait(max(0, started + elapsed - time.monotonic())):
                    return
                self.ingest(entry["event"])
        self.set_status("Replay complete")

    def simulate(self):
        self.set_status("Generating UWB timestamps for two simulated tags")
        rng = random.Random(42)
        x0, y0, x1, y1 = self.layout.bounds
        main = self.layout.anchors[self.layout.main]
        gw_boot = 1234
        start_ticks = TICK_MODULUS - round(2 * TICK_HZ)
        clocks = {anchor: (gw_boot + index, rng.uniform(-25e-6, 25e-6), rng.randrange(TICK_MODULUS))
                  for index, anchor in enumerate(self.layout.anchors, 1)}

        def report(anchor, tick, jitter=0):
            boot, drift, offset = clocks[anchor]
            if anchor == self.layout.main:
                boot, drift, offset = gw_boot, 0, 0
            local = start_ticks + round(tick * (1 + drift) + jitter) + offset
            return {"anchor": anchor, "boot": boot, "ticks": local % TICK_MODULUS}

        started = time.monotonic()
        frame = 0
        while not self.stop.wait(max(0, started + frame * 0.2 - time.monotonic())):
            tick = frame * 0.2 * TICK_HZ
            arrivals = [report(anchor, tick + math.dist(point, main) / METERS_PER_TICK, rng.gauss(0, 2))
                        for anchor, point in self.layout.anchors.items() if anchor != self.layout.main]
            self.ingest({"v": 1, "type": "sync", "gateway_boot": gw_boot, "seq": frame,
                         "tx_ticks": (start_ticks + round(tick)) % TICK_MODULUS, "rx": arrivals})
            for index, tag in enumerate((2001, 2002)):
                if self.stop.wait(max(0, started + frame * 0.2 + 0.05 * (index + 1) - time.monotonic())):
                    return
                phase = frame * 0.04 + index * 2.4
                position = (x0 + (x1 - x0) * (0.5 + 0.3 * math.cos(phase)),
                            y0 + (y1 - y0) * (0.5 + 0.3 * math.sin(phase)), self.layout.height)
                sent = tick + (index + 1) * 0.05 * TICK_HZ
                arrivals = [report(anchor, sent + math.dist(point, position) / METERS_PER_TICK, rng.gauss(0, 4))
                            for anchor, point in self.layout.anchors.items()]
                self.ingest({"v": 1, "type": "blink", "gateway_boot": gw_boot, "seq": frame,
                             "tag": tag, "tag_boot": tag, "rx": arrivals})
            frame += 1


def run(args, layout):
    collector = Collector(args, layout)
    page = (HERE / "tdoa_dashboard.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def reply(self, data, content_type="application/json", status=200):
            content = data if isinstance(data, bytes) else json.dumps(data).encode()
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(content)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_GET(self):
            if self.path == "/":
                self.reply(page, "text/html; charset=utf-8")
            elif self.path == "/api/tdoa":
                self.reply(collector.snapshot())
            else:
                self.send_error(404)

        def do_POST(self):
            if self.headers.get("Content-Type") != "application/json":
                self.send_error(415)
                return
            if self.path in ("/api/tdoa/record/start", "/api/tdoa/record/stop"):
                self.reply(collector.set_recording(self.path.endswith("/start")))
                return
            parts = self.path.split("/")
            if self.path == "/api/tdoa/zones":
                action, identifier = "create", None
            elif len(parts) == 6 and parts[:4] == ["", "api", "tdoa", "zones"] and parts[5] in ("rename", "delete"):
                identifier, action = parts[4:]
            else:
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 16384:
                    raise ValueError("Invalid danger-zone data size.")
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict) or (action == "create" and not {"name", "vertices"} <= data.keys()):
                    raise ValueError("Invalid danger-zone data.")
                self.reply(collector.edit_zone(action, identifier, data))
            except ValueError as error:
                self.reply({"error": str(error)}, status=400)
            except KeyError:
                self.reply({"error": "This danger zone no longer exists."}, status=404)
            except OSError as error:
                self.reply({"error": f"Could not save danger zones. {error}"}, status=500)

        def log_message(self, format, *args):
            pass

    with ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler) as server:
        threading.Thread(target=collector.run, daemon=True).start()
        print(f"TDoA dashboard running at http://127.0.0.1:{server.server_port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            collector.stop.set()
            collector.set_recording(False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--simulate", action="store_true", help="Generate simulated UWB timestamp events.")
    source.add_argument("--replay", type=Path, help="Play a recording's events.jsonl.")
    source.add_argument("--port", help="Main anchor's serial port, such as COM4 or /dev/cu.usbmodem123.")
    source.add_argument("--anchor-url", help="Main anchor's Wi-Fi URL, such as http://192.168.4.1.")
    parser.add_argument("--layout", type=Path, help="Surveyed anchor layout JSON. Required with a live anchor.")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--http-port", type=int, default=8767)
    parser.add_argument("--log-dir", type=Path, default=HERE / "logs")
    args = parser.parse_args()
    layout = args.layout
    if args.anchor_url:
        args.anchor_url = args.anchor_url.rstrip("/")
        address = urlsplit(args.anchor_url)
        if address.scheme not in ("http", "https") or not address.netloc or address.query or address.fragment:
            parser.error("Use the main anchor's base HTTP URL")
    if layout is None:
        if args.simulate:
            layout = HERE / "tdoa_layout.json"
        elif args.replay:
            layout = args.replay.with_name("layout.json")
        else:
            parser.error("Live mode requires --layout with surveyed coordinates")
    try:
        config = Layout.parse(json.loads(layout.read_text(encoding="utf-8")))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        parser.error(f"Cannot load {layout} · {error}")
    run(args, config)


if __name__ == "__main__":
    main()
