import tempfile
import unittest
from pathlib import Path

from tdoa_solver import Layout
from tdoa_zones import DangerZones, contains, polygon


class ZoneTests(unittest.TestCase):
    def test_polygon_validation_and_boundary(self):
        bounds = (0, 0, 8, 6)
        vertices = polygon([[0, 0], [4, 0], [4, 4], [2, 2], [0, 4]], bounds)
        self.assertTrue(contains(vertices, (2, 1)))
        self.assertTrue(contains(vertices, (3, 3)))
        self.assertFalse(contains(vertices, (2, 3)))
        with self.assertRaisesRegex(ValueError, "edges cannot cross"):
            polygon([[0, 0], [2, 2], [2, 0], [0, 2]], bounds)
        with self.assertRaisesRegex(ValueError, "double back"):
            polygon([[0, 0], [4, 0], [2, 0], [2, 4]], bounds)
        with self.assertRaisesRegex(ValueError, "inside the floor map"):
            polygon([[0, 0], [4, 0], [9, 4]], bounds)

    def test_entries_and_layout_persistence(self):
        with tempfile.TemporaryDirectory() as temporary:
            layout = Layout((0, 0, 8, 6), 0.8, 1003, {1003: (0, 0, 2.1)})
            directory = Path(temporary)
            zones = DangerZones(directory, layout, "simulation")
            zones.create({"name": "Loading bay", "vertices": [[1, 1], [3, 1], [3, 3], [1, 3]]})
            identifier, = zones.zones
            timestamp = lambda: "2026-11-18T16:15:00+00:00"

            def update(x, now, at=None, tag=2001):
                return zones.observe([{"x": x, "y": 2, "tag": tag, "at": at if at is not None else now}], now, timestamp)

            self.assertFalse(update(0, 1))
            self.assertFalse(update(2, 3, at=1))
            event, = update(2, 4)
            self.assertEqual((event["tag"], event["zone_name"], event["at"]), (2001, "Loading bay", timestamp()))
            self.assertFalse(update(2, 5))
            self.assertFalse(update(0, 7, at=5))
            self.assertFalse(update(2, 8))
            zones.rename(identifier, "Forklift lane")
            self.assertFalse(update(2, 9))
            self.assertEqual(update(2, 9, tag=2002)[0]["tag"], 2002)
            self.assertFalse(update(0, 10))
            self.assertEqual(update(2, 11)[0]["zone_name"], "Forklift lane")
            self.assertEqual(zones.snapshot()["entries"][-1]["zone_name"], "Loading bay")
            self.assertEqual(DangerZones(directory, layout, "simulation").zones[identifier]["name"], "Forklift lane")
            self.assertFalse(DangerZones(directory, layout, "live").zones)
            zones.delete(identifier)
            self.assertFalse(DangerZones(directory, layout, "simulation").zones)


if __name__ == "__main__":
    unittest.main()
