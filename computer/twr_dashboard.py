"""Collect TWR distances via the reference anchor and serve the local dashboard."""

import argparse
import csv
import json
import re
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.request import ProxyHandler, build_opener

import serial
from serial.tools import list_ports


ROLE = re.compile(r"Ranging as (tag|anchor) (\d+)(?: targeting anchor (\d+))?$")
RESULT = re.compile(r"TWR anchor (\d+) measured tag (\d+) seq (\d+): (-?\d+) mm boot (\d+) record (\d+)$")
REPLY = re.compile(r"TWR anchor (\d+) replied to tag (\d+) seq \d+$")
STALE_SECONDS = 3
USB_SERIAL_JTAG = (0x303A, 0x1001)
DEFAULT_LOG_DIR = Path(__file__).resolve().with_name("logs")
LOG_COLUMNS = ("received_at_utc", "tag_id", "anchor_id", "sequence", "distance_m", "source")


class SessionLog:
    def __init__(self, directory):
        self.directory = directory.resolve()
        self.lock = threading.Lock()
        self.file = None
        self.writer = None
        self.path = None
        self.started_at = 0
        self.records = set()
        self.error = ""

    def _status(self):
        return {"active": self.file is not None, "directory": str(self.directory),
                "path": str(self.path) if self.path else None, "count": len(self.records), "error": self.error}

    def status(self):
        with self.lock:
            return self._status()

    def start(self):
        with self.lock:
            if self.file is None:
                self.error = ""
                try:
                    self.directory.mkdir(parents=True, exist_ok=True)
                    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
                    path = self.directory / f"twr-{stamp}.csv"
                    self.file = path.open("x", encoding="utf-8", newline="")
                    self.path = path
                    self.records.clear()
                    self.writer = csv.writer(self.file)
                    self.writer.writerow(LOG_COLUMNS)
                    self.file.flush()
                    self.started_at = time.monotonic()
                except OSError as error:
                    self._fail(error)
            return self._status()

    def _close(self):
        file = self.file
        self.file = self.writer = None
        if file is not None:
            file.close()

    def _fail(self, error):
        self.error = str(error)
        try:
            self._close()
        except OSError:
            pass

    def stop(self):
        with self.lock:
            try:
                self._close()
            except OSError as error:
                self._fail(error)
            return self._status()

    def add(self, reading, source, measured_at):
        tag, anchor, sequence, meters, boot, record = reading
        key = anchor, boot, record
        with self.lock:
            if self.file is None or measured_at < self.started_at or key in self.records:
                return
            try:
                received_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
                self.writer.writerow((received_at, tag, anchor, sequence, meters, source))
                self.file.flush()
                self.records.add(key)
            except OSError as error:
                self._fail(error)


@dataclass
class Board:
    status: str = "Connecting"
    role: str = ""
    node_id: int | None = None
    measurements: dict = field(default_factory=dict)
    heard_tags: set = field(default_factory=set)

    def identify(self, role, node_id):
        if (self.role, self.node_id) != (role, node_id):
            self.role = role
            self.node_id = node_id
            self.measurements.clear()
            self.heard_tags.clear()

    def consume(self, line):
        _, marker, payload = line.strip().partition("uwb: ")
        if not marker:
            return
        if match := ROLE.fullmatch(payload):
            role, node_id, _ = match.groups()
            self.identify(role, int(node_id))
            self.measurements.clear()
        elif match := RESULT.fullmatch(payload):
            anchor, tag, sequence, mm, boot, record = map(int, match.groups())
            return self.store_range(anchor, tag, sequence, mm, boot, record, 0)
        elif match := REPLY.fullmatch(payload):
            self.identify("anchor", int(match[1]))
            self.heard_tags.add(int(match[2]))

    def store_range(self, anchor, tag, sequence, mm, boot, record, age_ms):
        self.identify("anchor", anchor)
        previous = self.measurements.get(tag)
        reading = tag, anchor, sequence, mm / 1000, boot, record
        self.measurements[tag] = (reading, time.monotonic() - age_ms / 1000)
        if previous is None or reading != previous[0]:
            return reading


class Collector:
    def __init__(self, ports, session_log, anchor_url):
        self.ports = ports
        self.session_log = session_log
        self.anchor_url = anchor_url
        self.boards = {}
        self.lock = threading.Lock()
        self.stop = threading.Event()

    def discover(self):
        if self.anchor_url:
            self.read_wifi_anchor()
            return
        while not self.stop.is_set():
            ports = self.ports or [port.device for port in list_ports.comports()
                                   if (port.vid, port.pid) == USB_SERIAL_JTAG]
            with self.lock:
                for port in ports:
                    if port not in self.boards:
                        board = self.boards[port] = Board()
                        threading.Thread(target=self.read_port, args=(port, board), daemon=True).start()
            self.stop.wait(2)

    def read_port(self, port, board):
        while not self.stop.is_set():
            try:
                with serial.Serial(baudrate=115200, timeout=0.3) as connection:
                    connection.port = port
                    connection.dtr = connection.rts = False
                    connection.open()
                    with self.lock:
                        board.status = "Connected"
                    pending = b""
                    while not self.stop.is_set():
                        chunk = connection.read_until(size=1024)
                        pending += chunk
                        if pending.endswith(b"\n"):
                            received_at = time.monotonic()
                            with self.lock:
                                reading = board.consume(pending.decode("utf-8", errors="replace"))
                            if reading is not None:
                                self.session_log.add(reading, port, received_at)
                            pending = b""
                        elif len(pending) >= 1024:
                            pending = b""
            except (OSError, serial.SerialException) as error:
                with self.lock:
                    board.status = f"Unavailable · {error}"
                self.stop.wait(2)

    def read_wifi_anchor(self):
        source = f"Wi-Fi {self.anchor_url}"
        opener = build_opener(ProxyHandler({}))
        with self.lock:
            board = self.boards[source] = Board()
        while not self.stop.is_set():
            try:
                with opener.open(f"{self.anchor_url}/api/twr/tags", timeout=1.5) as response:
                    data = json.load(response)
                if data["role"] != "anchor":
                    raise ValueError("The selected board is in Tag mode")
                anchor = int(data["anchorId"])
                readings = []
                with self.lock:
                    board.status = "Connected"
                    board.identify("anchor", anchor)
                    for sample in data["tags"]:
                        age = max(0, int(sample["ageMs"]))
                        reading = board.store_range(anchor, int(sample["tagId"]), int(sample["sequence"]),
                                                    int(sample["distanceMm"]), int(sample["bootId"]),
                                                    int(sample["recordId"]), age)
                        if reading is not None and age < STALE_SECONDS * 1000:
                            readings.append((reading, time.monotonic() - age / 1000))
                for reading, measured_at in readings:
                    self.session_log.add(reading, source, measured_at)
            except (OSError, ValueError, KeyError, TypeError) as error:
                with self.lock:
                    board.status = f"Unavailable · {error}"
            self.stop.wait(0.4)

    def snapshot(self):
        now = time.monotonic()
        with self.lock:
            boards = []
            anchors = set()
            for port, board in sorted(self.boards.items()):
                if board.role == "anchor":
                    anchors.add(board.node_id)
                boards.append({
                    "port": port, "status": board.status, "role": board.role, "nodeId": board.node_id,
                    "heardTags": sorted(board.heard_tags),
                    "measurements": {tag: {
                        "meters": reading[3], "sequence": reading[2], "ageSeconds": max(0, now - received),
                        "live": board.status == "Connected" and now - received < STALE_SECONDS,
                    } for tag, (reading, received) in board.measurements.items()},
                })
            return {"boards": boards, "anchors": sorted(anchors), "logging": self.session_log.status()}


def run(ports, http_port, log_dir, anchor_url):
    session_log = SessionLog(log_dir)
    collector = Collector(ports, session_log, anchor_url)
    page = Path(__file__).with_name("twr_dashboard.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/":
                self.send_content(page, "text/html; charset=utf-8")
            elif self.path == "/api/twr":
                self.send_json(collector.snapshot())
            else:
                self.send_error(404)

        def do_POST(self):
            action = {"/api/twr/log/start": session_log.start, "/api/twr/log/stop": session_log.stop}.get(self.path)
            if action is None:
                self.send_error(404)
                return
            if self.headers.get("Content-Type") != "application/json":
                self.send_error(415)
                return
            self.send_json(action())

        def send_json(self, data):
            self.send_content(json.dumps(data).encode(), "application/json")

        def send_content(self, content, media_type):
            self.send_response(200)
            self.send_header("Content-Type", media_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(content)

        def log_message(self, format, *args):
            pass

    with ThreadingHTTPServer(("127.0.0.1", http_port), Handler) as server:
        threading.Thread(target=collector.discover, daemon=True).start()
        print(f"TWR dashboard running at http://127.0.0.1:{server.server_port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            collector.stop.set()
            session_log.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--ports", nargs="+", help="Anchor serial ports. Defaults to ESP32 USB autodetection.")
    transport.add_argument("--anchor-url", help="Wi-Fi URL of the anchor, for example http://192.168.4.1.")
    parser.add_argument("--http-port", type=int, default=8765, help="Local dashboard port. Defaults to 8765.")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR, help="Folder for local TWR session CSVs.")
    args = parser.parse_args()
    anchor_url = args.anchor_url.rstrip("/") if args.anchor_url else None
    if anchor_url and not anchor_url.startswith(("http://", "https://")):
        parser.error("--anchor-url must start with http:// or https://")
    run(args.ports, args.http_port, args.log_dir, anchor_url)
