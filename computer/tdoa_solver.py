"""Clock correction and known-height 2D uplink TDoA localization."""

import math
from collections import deque
from dataclasses import dataclass, field


C = 299702547.0
TICK_HZ = 499.2e6 * 128
METERS_PER_TICK = C / TICK_HZ
TICK_MODULUS = 1 << 40
MAX_SYNC_TICKS = TICK_HZ
WAIT_SECONDS = 1.1
STALE_SECONDS = 1.0


def tick_span(later, earlier):
    return (later - earlier) % TICK_MODULUS


def tick_difference(tick, origin):
    half = TICK_MODULUS // 2
    return (tick - origin + half) % TICK_MODULUS - half


@dataclass(frozen=True)
class Layout:
    bounds: tuple
    height: float
    main: int
    anchors: dict

    @classmethod
    def parse(cls, data):
        bounds = tuple(map(float, data["bounds_m"]))
        height = float(data["tag_height_m"])
        main = data["main_anchor_id"]
        anchors = {int(key): tuple(map(float, value)) for key, value in data["anchors"].items()}
        if (len(bounds) != 4 or bounds[0] >= bounds[2] or bounds[1] >= bounds[3] or
                not all(map(math.isfinite, (*bounds, height))) or len(anchors) != len(data["anchors"]) or
                type(main) is not int or main not in anchors or not 1 <= len(anchors) <= 16 or
                any(not 1 <= key <= 65534 or len(point) != 3 or not all(map(math.isfinite, point))
                    for key, point in anchors.items())):
            raise ValueError("Check the map bounds, fixed tag height, main anchor and anchor coordinates")
        return cls(bounds, height, main, anchors)

    def as_json(self):
        return {"bounds_m": self.bounds, "tag_height_m": self.height,
                "main_anchor_id": self.main, "anchors": self.anchors}


def centered(values):
    mean = sum(values) / len(values)
    return [value - mean for value in values]


def solve_position(layout, arrivals):
    """Return (x, y, residual RMS) or raise if the geometry or fit is unreliable."""
    points = [layout.anchors[anchor] for anchor in arrivals]
    observed = centered([ticks * METERS_PER_TICK for ticks in arrivals.values()])
    for i, point in enumerate(points):
        if any(abs(observed[i] - observed[j]) > math.dist(point, other) + 0.5
               for j, other in enumerate(points[:i])):
            raise ValueError("Arrival difference exceeds anchor separation")

    x0, y0, x1, y1 = layout.bounds

    def evaluate(x, y):
        distances = [max(math.dist((x, y, layout.height), point), 1e-9) for point in points]
        residual = [distance - measured for distance, measured in zip(centered(distances), observed)]
        gx = centered([(x - point[0]) / d for point, d in zip(points, distances)])
        gy = centered([(y - point[1]) / d for point, d in zip(points, distances)])
        aa = sum(a * a for a in gx)
        ab = sum(a * b for a, b in zip(gx, gy))
        bb = sum(b * b for b in gy)
        ar = sum(a * r for a, r in zip(gx, residual))
        br = sum(b * r for b, r in zip(gy, residual))
        return sum(r * r for r in residual), aa, ab, bb, ar, br

    def fit(x, y):
        damping = 0.001
        for _ in range(35):
            cost, aa, ab, bb, ar, br = evaluate(x, y)
            determinant = (aa + damping) * (bb + damping) - ab * ab
            dx = (-ar * (bb + damping) + ab * br) / determinant
            dy = (-br * (aa + damping) + ab * ar) / determinant
            nx = min(x1, max(x0, x + dx))
            ny = min(y1, max(y0, y + dy))
            if math.hypot(nx - x, ny - y) < 1e-6:
                break
            if evaluate(nx, ny)[0] < cost:
                x, y = nx, ny
                damping = max(1e-8, damping * 0.3)
            else:
                damping *= 10
        cost, aa, ab, bb, _, _ = evaluate(x, y)
        geometry = (aa * bb - ab * ab) / (aa + bb) ** 2 if aa + bb else 0
        return math.sqrt(cost / len(points)), x, y, geometry

    fits = sorted(fit(x0 + u * (x1 - x0), y0 + v * (y1 - y0))
                  for u in (0.15, 0.5, 0.85) for v in (0.15, 0.5, 0.85))
    rms, x, y, geometry = fits[0]
    if rms > 0.35:
        raise ValueError("Large position-fit residual")
    if geometry < 0.001:
        raise ValueError("Weak anchor geometry")
    if any(error <= rms + 0.015 and math.hypot(x - px, y - py) > 0.35 for error, px, py, _ in fits[1:]):
        raise ValueError("Ambiguous position inside map bounds")
    return x, y, rms


def unsigned(data, key, upper, lower=0):
    value = data[key]
    if type(value) is not int or not lower <= value < upper:
        raise ValueError(f"Invalid {key}")
    return value


def parse_event(value):
    if not isinstance(value, dict) or value.get("v") != 1 or value.get("type") not in ("sync", "blink"):
        raise ValueError("Expected a version 1 sync or blink event")
    result = {"v": 1, "type": value["type"], "rx": [],
              "gateway_boot": unsigned(value, "gateway_boot", 1 << 32), "seq": unsigned(value, "seq", 1 << 32)}
    if value["type"] == "sync":
        result["tx_ticks"] = unsigned(value, "tx_ticks", TICK_MODULUS)
    else:
        result["tag"] = unsigned(value, "tag", 65535, 1)
        result["tag_boot"] = unsigned(value, "tag_boot", 1 << 32)
    if not isinstance(value["rx"], list) or len(value["rx"]) > 16:
        raise ValueError("Invalid anchor observations")
    seen = set()
    for report in value["rx"]:
        observation = {"anchor": unsigned(report, "anchor", 65535, 1),
                       "boot": unsigned(report, "boot", 1 << 32),
                       "ticks": unsigned(report, "ticks", TICK_MODULUS)}
        if observation["anchor"] in seen:
            raise ValueError("Duplicate anchor in event")
        seen.add(observation["anchor"])
        result["rx"].append(observation)
    return result


@dataclass
class Tag:
    boot: int
    last_seen: float
    status: str = "Waiting for position"
    fix: dict | None = None
    trail: deque = field(default_factory=lambda: deque(maxlen=75))


class Tracker:
    def __init__(self, layout):
        self.layout = layout
        self.boot = None
        self.syncs = deque(maxlen=32)
        self.pending = {}
        self.seen = set()
        self.recent = deque()
        self.last_rx = {}
        self.last_sync = {}
        self.tags = {}
        self.counts = {"sync": 0, "blink": 0, "fix": 0, "rejected": 0}

    def submit(self, event, now):
        boot = event["gateway_boot"]
        if self.boot != boot:
            self.boot = boot
            self.syncs.clear()
            self.pending.clear()
            self.seen.clear()
            self.recent.clear()
            self.last_rx.clear()
            self.last_sync.clear()
            self.tags.clear()
        key = event["type"], event.get("tag"), event.get("tag_boot"), event["seq"]
        if key in self.seen:
            return False
        self.recent.append(key)
        self.seen.add(key)
        if len(self.recent) > 4096:
            self.seen.remove(self.recent.popleft())
        kind = event["type"]
        self.counts[kind] += 1
        reports = {r["anchor"]: (r["boot"], r["ticks"]) for r in event["rx"] if r["anchor"] in self.layout.anchors}
        if kind == "sync":
            self.syncs.append((now, event["tx_ticks"], reports))
            self.last_sync.update(dict.fromkeys(reports, now))
            self.last_sync[self.layout.main] = now
        else:
            tag_id = event["tag"]
            tag_boot = event["tag_boot"]
            if tag_id not in self.tags or self.tags[tag_id].boot != tag_boot:
                self.tags[tag_id] = Tag(tag_boot, now)
            self.tags[tag_id].last_seen = now
            self.pending[key] = now, reports
            self.last_rx.update(dict.fromkeys(reports, now))
        return True

    def aligned(self, reports, arrived):
        if not self.syncs:
            return {}
        origin = self.syncs[-1][1]
        aligned = {}
        for anchor, (boot, tick) in reports.items():
            if anchor == self.layout.main:
                delta = tick_difference(tick, origin)
                if boot == self.boot and abs(delta) <= TICK_HZ * 3:
                    aligned[anchor] = delta
                continue
            previous = None
            for received, tx_tick, observations in self.syncs:
                if abs(received - arrived) > 2.0:
                    continue
                pair = observations.get(anchor)
                if pair is None or pair[0] != boot:
                    continue
                local_tick = pair[1]
                if previous is not None:
                    prev_tx, prev_local = previous
                    local_span = tick_span(local_tick, prev_local)
                    ref_span = tick_span(tx_tick, prev_tx)
                    offset = tick_span(tick, prev_local)
                    if (0 < local_span <= MAX_SYNC_TICKS and 0 < ref_span <= MAX_SYNC_TICKS and
                            abs(ref_span / local_span - 1) < 0.0002 and offset <= local_span):
                        propagation = math.dist(self.layout.anchors[anchor], self.layout.anchors[self.layout.main]) / METERS_PER_TICK
                        aligned[anchor] = tick_difference(prev_tx, origin) + propagation + offset * ref_span / local_span
                        break
                previous = tx_tick, local_tick
        return aligned

    def update(self, now):
        fixes = []
        timing = []
        while self.syncs and now - self.syncs[0][0] > 3:
            self.syncs.popleft()
        for key, (arrival, reports) in list(self.pending.items()):
            _, tag_id, boot, seq = key
            tag = self.tags[tag_id]
            if tag.boot != boot:
                del self.pending[key]
                continue
            aligned = self.aligned(reports, arrival)
            if len(aligned) < len(reports) and now - arrival < WAIT_SECONDS:
                continue
            del self.pending[key]
            if aligned:
                timing.append((tag_id, boot, seq, arrival, aligned))
            if tag.fix is not None and arrival < tag.fix["at"]:
                continue
            if len(aligned) < 3:
                tag.status = f"Need 3 synchronized anchors ({len(aligned)} received)"
                self.counts["rejected"] += 1
                continue
            try:
                x, y, rms = solve_position(self.layout, aligned)
            except ValueError as error:
                tag.status = str(error)
                self.counts["rejected"] += 1
                continue
            fix = {"tag": tag_id, "tag_boot": boot, "seq": seq, "x": x, "y": y, "rms": rms,
                   "anchors": sorted(aligned), "at": arrival}
            tag.status = "Position available"
            tag.fix = fix
            tag.trail.append((x, y))
            self.counts["fix"] += 1
            fixes.append(fix)
        return fixes, timing

    def snapshot(self, now):
        tags = []
        for tag_id, tag in sorted(self.tags.items()):
            age = max(0, now - tag.fix["at"]) if tag.fix else None
            tags.append({"id": tag_id, "age": age, "live": age is not None and age < STALE_SECONDS,
                         "status": tag.status, "fix": tag.fix, "trail": list(tag.trail)})
        return {"counts": dict(self.counts), "tags": tags,
                "anchors": [{"id": anchor, "point": point, "main": anchor == self.layout.main,
                             "sync_age": now - self.last_sync[anchor] if anchor in self.last_sync else None,
                             "blink_age": now - self.last_rx[anchor] if anchor in self.last_rx else None}
                            for anchor, point in self.layout.anchors.items()]}
