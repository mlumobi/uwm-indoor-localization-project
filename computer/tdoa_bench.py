"""Capture and evaluate the three-board TDoA firmware bench over USB."""

import argparse
import csv
import json
import math
import statistics
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import serial

from tdoa_solver import C, TICK_HZ, TICK_MODULUS, Layout, Tracker, parse_event, tick_difference


@dataclass(frozen=True)
class Report:
    direction: str
    kind: str
    node: int
    boot: int
    source: int
    source_boot: int
    sequence: int
    ticks: int
    host_time: float

    @classmethod
    def parse(cls, line, host_time):
        parts = line.split()
        if len(parts) < 3 or parts[0] != "TDOA_BENCH" or parts[2] not in ("sync", "blink"):
            return None
        direction, kind = parts[1:3]
        try:
            if direction == "tx" and len(parts) == 7:
                node, boot, seq, ticks = map(int, parts[3:])
                source, source_boot = node, boot
            elif direction == "rx" and len(parts) == 9:
                node, boot, source, source_boot, seq, ticks = map(int, parts[3:])
            else:
                return None
        except ValueError:
            return None
        if not 0 <= ticks < TICK_MODULUS:
            return None
        return cls(direction, kind, node, boot, source, source_boot, seq, ticks, host_time)

    @property
    def key(self):
        return self.kind, self.source, self.source_boot, self.sequence

    def observation(self):
        return {"anchor": self.node, "boot": self.boot, "ticks": self.ticks}


def capture(port, start, end, directory):
    reports = []
    failures = 0
    with (directory / f"{Path(port).name}.log").open("w", encoding="utf-8", newline="\n") as log:
        with serial.Serial(baudrate=115200, timeout=0.25) as usb:
            usb.port = port
            usb.dtr = usb.rts = False
            usb.open()
            pending = b""
            while time.monotonic() < end:
                pending += usb.read_until(size=512)
                if pending.endswith(b"\n"):
                    now = time.monotonic()
                    line = pending.decode("utf-8", errors="replace").strip()
                    pending = b""
                    if line:
                        log.write(f"{now - start:.6f} {line}\n")
                    if report := Report.parse(line, now):
                        reports.append(report)
                    if line.startswith("TDOA_BENCH tx_failed "):
                        failures += 1
                elif len(pending) >= 512:
                    pending = b""
    return reports, failures


def percentile(values, fraction):
    return sorted(values)[math.ceil(len(values) * fraction) - 1] if values else None


def evaluate(args, streams, start, end, output):
    (main, main_failures), (anchor, _), (tag, tag_failures) = streams
    tx = {r.key for r in tag if r.direction == "tx" and r.kind == "blink" and r.node == args.tag_id
          and start + 1 <= r.host_time <= end - 1}
    main = [r for r in main if r.node == args.main_id]
    remote = {r.key: r for r in anchor if r.direction == "rx" and r.node == args.anchor_id}
    reference_blinks = {r.key for r in main if r.direction == "rx" and r.kind == "blink"}
    syncs = {r.key for r in main if r.direction == "tx" and r.kind == "sync"}
    matched = tx & reference_blinks & remote.keys()
    summary = {"tag_blinks_sent": len(tx), "main_blinks_received": len(tx & reference_blinks),
               "secondary_blinks_received": len(tx & remote.keys()), "blinks_received_by_both": len(matched),
               "sync_beacons_sent": len(syncs), "sync_beacons_received": len(syncs & remote.keys()),
               "tag_tx_failures": tag_failures, "main_tx_failures": main_failures,
               "matched_reception_percent": round(100 * len(matched) / len(tx), 2) if tx else None}

    baseline = args.baseline_m or 0.0
    layout = Layout((0, 0, max(baseline, 1), 1), 0, args.main_id,
                    {args.main_id: (0, 0, 0), args.anchor_id: (baseline, 0, 0)})
    (output / "layout.json").write_text(json.dumps(layout.as_json(), indent=2) + "\n", encoding="utf-8")
    tracker = Tracker(layout)
    deltas = []
    wrap_count = 0
    current_tick = elapsed_ticks = 0
    main_boot = None
    with ((output / "events.jsonl").open("w", encoding="utf-8") as events,
          (output / "timing.csv").open("w", encoding="utf-8", newline="") as timings):
        writer = csv.writer(timings)
        writer.writerow(("tag_id", "tag_boot", "sequence", "reference_anchor_id", "anchor_id", "delta_ns"))
        for report in main:
            if report.key not in syncs and report.key not in reference_blinks:
                continue
            if main_boot is not None and main_boot != report.boot:
                raise ValueError("Main anchor restarted during capture. Repeat with stable power.")
            if main_boot is not None:
                delta = tick_difference(report.ticks, current_tick)
                if delta > 0 and report.ticks < current_tick:
                    wrap_count += 1
                elapsed_ticks += max(0, delta)
            main_boot, current_tick = report.boot, report.ticks
            now = elapsed_ticks / TICK_HZ
            observed = [remote[report.key].observation()] if report.key in remote else []
            event = {"v": 1, "type": report.kind, "gateway_boot": report.boot, "seq": report.sequence, "rx": observed}
            if report.kind == "sync":
                event["tx_ticks"] = report.ticks
            else:
                event.update(tag=report.source, tag_boot=report.source_boot)
                observed.append(report.observation())
            event = parse_event(event)
            if not tracker.submit(event, now):
                continue
            events.write(json.dumps({"elapsed_s": now, "event": event}) + "\n")
            _, timing = tracker.update(now)
            for tag_id, boot, sequence, _, aligned in timing:
                if ("blink", tag_id, boot, sequence) in matched and args.main_id in aligned and args.anchor_id in aligned:
                    ns = (aligned[args.anchor_id] - aligned[args.main_id]) / TICK_HZ * 1e9
                    deltas.append(ns)
                    writer.writerow((tag_id, boot, sequence, args.main_id, args.anchor_id, f"{ns:.6f}"))
    summary.update(corrected_blinks=len(deltas), main_timestamp_wraps=wrap_count,
                   surveyed_baseline_m=args.baseline_m)
    if deltas:
        median = statistics.median(deltas)
        summary["median_delta_ns"] = round(median, 3)
        summary["p95_absolute_deviation_from_median_ns"] = round(percentile([abs(d - median) for d in deltas], .95), 3)
        if args.tag_main_m is not None:
            expected = (args.tag_anchor_m - args.tag_main_m) / C * 1e9
            summary["expected_delta_ns"] = round(expected, 3)
            summary["p95_absolute_surveyed_residual_ns"] = round(percentile([abs(d - expected) for d in deltas], .95), 3)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main", required=True, help="Main anchor serial port")
    parser.add_argument("--anchor", required=True, help="Secondary anchor serial port")
    parser.add_argument("--tag", required=True, help="Blinking tag serial port")
    parser.add_argument("--main-id", type=int, default=1003)
    parser.add_argument("--anchor-id", type=int, default=1002)
    parser.add_argument("--tag-id", type=int, default=1001)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--baseline-m", type=float, help="Surveyed main-to-secondary antenna separation")
    parser.add_argument("--tag-main-m", type=float, help="Surveyed tag-to-main separation")
    parser.add_argument("--tag-anchor-m", type=float, help="Surveyed tag-to-secondary separation")
    args = parser.parse_args()
    distances = (args.baseline_m, args.tag_main_m, args.tag_anchor_m)
    if (not math.isfinite(args.seconds) or args.seconds < 5 or
            len({args.main.casefold(), args.anchor.casefold(), args.tag.casefold()}) != 3 or
            len({args.main_id, args.anchor_id, args.tag_id}) != 3 or
            any(value is not None and (not math.isfinite(value) or value <= 0) for value in distances) or
            (any(value is not None for value in distances[1:]) and not all(value is not None for value in distances))):
        parser.error("Use distinct ports and IDs, at least five seconds, and positive survey distances")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = Path(__file__).resolve().with_name("logs") / f"tdoa-bench-{stamp}"
    output.mkdir(parents=True)
    print(f"Capturing three USB ports for {args.seconds:g} seconds in {output}", flush=True)
    ports = (args.main, args.anchor, args.tag)
    start = time.monotonic()
    end = start + args.seconds
    with ThreadPoolExecutor(3) as pool:
        streams = [pool.submit(capture, port, start, end, output) for port in ports]
        summary = evaluate(args, [stream.result() for stream in streams], start, end, output)
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if not summary["corrected_blinks"]:
        raise SystemExit("No clock-corrected pairs were captured. See the three serial logs.")


if __name__ == "__main__":
    main()
