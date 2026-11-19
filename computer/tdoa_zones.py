"""Persist floor-map polygons and detect entries from fresh TDoA fixes."""

import hashlib
import json
import tempfile
from collections import deque
from pathlib import Path
from uuid import uuid4

from tdoa_solver import STALE_SECONDS


MAX_ZONES = 32
MAX_VERTICES = 64
EPSILON = 1e-9


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def on_edge(a, b, point):
    return abs(cross(a, b, point)) <= EPSILON and all(
        min(start, end) - EPSILON <= value <= max(start, end) + EPSILON
        for start, end, value in zip(a, b, point)
    )


def segments_intersect(a, b, c, d):
    ab_c, ab_d = cross(a, b, c), cross(a, b, d)
    cd_a, cd_b = cross(c, d, a), cross(c, d, b)
    return ((ab_c * ab_d < 0 and cd_a * cd_b < 0) or
            on_edge(a, b, c) or on_edge(a, b, d) or on_edge(c, d, a) or on_edge(c, d, b))


def edges(vertices):
    return zip(vertices, vertices[1:] + vertices[:1])


def contains(vertices, point):
    inside = False
    x, y = point
    for a, b in edges(vertices):
        if on_edge(a, b, point):
            return True
        if (a[1] > y) != (b[1] > y) and x < a[0] + (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]):
            inside = not inside
    return inside


def zone_name(value):
    if isinstance(value, str) and 1 <= len(value.strip()) <= 48 and value.isprintable():
        return value.strip()
    raise ValueError("Enter a zone name of up to 48 characters.")


def polygon(value, bounds):
    if not isinstance(value, list) or not 3 <= len(value) <= MAX_VERTICES:
        raise ValueError("Draw at least three points and no more than 64.")
    vertices = []
    x0, y0, x1, y1 = bounds
    for point in value:
        if (not isinstance(point, list) or len(point) != 2 or
                any(type(number) not in (int, float) for number in point)):
            raise ValueError("Each point must have valid map coordinates.")
        x, y = point
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            raise ValueError("Keep all points inside the floor map.")
        vertices.append((x, y))
    if len(set(vertices)) != len(vertices):
        raise ValueError("Use a different location for each point.")
    sides = list(edges(vertices))
    for i, (a, b) in enumerate(sides):
        for j, (c, d) in enumerate(sides[i + 1:], i + 1):
            if j == i + 1:
                overlap = on_edge(a, b, d) or on_edge(c, d, a)
            elif i == 0 and j == len(sides) - 1:
                overlap = on_edge(a, b, c) or on_edge(c, d, b)
            elif segments_intersect(a, b, c, d):
                raise ValueError("Zone edges cannot cross. Undo a point and try again.")
            else:
                overlap = False
            if overlap:
                raise ValueError("Edges cannot double back on one another.")
    if abs(sum(cross(vertices[0], a, b) for a, b in sides)) <= EPSILON:
        raise ValueError("The points must enclose an area.")
    return vertices


class DangerZones:
    def __init__(self, directory, layout, mode):
        self.bounds = layout.bounds
        layout_key = hashlib.sha256(json.dumps(layout.as_json(), sort_keys=True).encode()).hexdigest()[:16]
        self.path = directory / f"tdoa-zones-{mode}-{layout_key}.json"
        self.zones = {}
        self.inside = set()
        self.entries = deque(maxlen=100)
        self.epoch = uuid4().hex
        self.event_id = 0
        if self.path.exists():
            saved = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(saved, list) or len(saved) > MAX_ZONES:
                raise ValueError("Invalid saved danger-zone list")
            for item in saved:
                identifier = item["id"]
                if not isinstance(identifier, str) or not identifier or identifier in self.zones:
                    raise ValueError("Invalid saved danger-zone ID")
                self.zones[identifier] = {"id": identifier, "name": zone_name(item["name"]),
                                          "vertices": polygon(item["vertices"], self.bounds)}

    def save(self, zones):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=self.path.parent, delete=False) as file:
                temporary = Path(file.name)
                json.dump(list(zones.values()), file, indent=2, ensure_ascii=False)
                file.write("\n")
            temporary.replace(self.path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
        self.zones = zones

    def create(self, data):
        if len(self.zones) >= MAX_ZONES:
            raise ValueError("The map supports up to 32 danger zones.")
        identifier = uuid4().hex
        zone = {"id": identifier, "name": zone_name(data["name"]), "vertices": polygon(data["vertices"], self.bounds)}
        self.save({**self.zones, identifier: zone})

    def rename(self, identifier, name):
        current = self.zones[identifier]
        self.save({**self.zones, identifier: {**current, "name": zone_name(name)}})

    def delete(self, identifier):
        remaining = self.zones.copy()
        del remaining[identifier]
        self.save(remaining)
        self.inside = {key for key in self.inside if key[1] != identifier}

    def observe(self, fixes, now, timestamp):
        events = []
        for fix in fixes:
            if now - fix["at"] >= STALE_SECONDS:
                continue
            tag = fix["tag"]
            for identifier, zone in self.zones.items():
                key = tag, identifier
                if not contains(zone["vertices"], (fix["x"], fix["y"])):
                    self.inside.discard(key)
                elif key not in self.inside:
                    self.inside.add(key)
                    self.event_id += 1
                    event = {"id": self.event_id, "tag": tag, "zone_id": identifier, "zone_name": zone["name"],
                             "at": timestamp(), "x": fix["x"], "y": fix["y"]}
                    self.entries.appendleft(event)
                    events.append(event)
        return events

    def snapshot(self):
        return {"zones": list(self.zones.values()), "entries": list(self.entries), "entry_epoch": self.epoch}
