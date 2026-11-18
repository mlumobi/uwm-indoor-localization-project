import math
import unittest

from tdoa_solver import Layout, METERS_PER_TICK, TICK_HZ, TICK_MODULUS, Tracker, parse_event


class ClockCorrectionTests(unittest.TestCase):
    def setUp(self):
        self.layout = Layout.parse({
            "bounds_m": [0, 0, 8, 6], "tag_height_m": 0.8, "main_anchor_id": 1003,
            "anchors": {"1003": [0, 0, 2.1], "1002": [8, 0, 2.1], "1004": [2, 6, 2.1]},
        })
        self.tracker = Tracker(self.layout)
        self.start = TICK_MODULUS - round(0.1 * TICK_HZ)
        self.position = (3.25, 2.4, self.layout.height)

    def rx(self, anchor, transmitted, source, boot=None):
        elapsed = transmitted + math.dist(source, self.layout.anchors[anchor]) / METERS_PER_TICK
        skew = {1003: 0, 1002: 21e-6, 1004: -17e-6}[anchor]
        offset = {1003: 0, 1002: 0, 1004: 1234567}[anchor]
        return {"anchor": anchor, "boot": boot if boot is not None else anchor,
                "ticks": (self.start + round(elapsed * (1 + skew)) + offset) % TICK_MODULUS}

    def sync(self, seq, seconds, rebooted=False):
        sent = seconds * TICK_HZ
        main = self.layout.anchors[1003]
        return {"v": 1, "type": "sync", "gateway_boot": 1003, "seq": seq,
                "tx_ticks": (self.start + round(sent)) % TICK_MODULUS,
                "rx": [self.rx(1002, sent, main), self.rx(1004, sent, main, 2004 if rebooted else None)]}

    def blink(self):
        sent = 0.09 * TICK_HZ
        return {"v": 1, "type": "blink", "gateway_boot": 1003, "seq": 1, "tag": 2001, "tag_boot": 21,
                "rx": [self.rx(anchor, sent, self.position) for anchor in self.layout.anchors]}

    def test_bracket_crosses_rollover_with_three_anchors(self):
        self.tracker.submit(parse_event(self.sync(1, 0)), 0)
        self.tracker.submit(parse_event(self.blink()), 0.09)
        self.assertEqual(self.tracker.update(0.10), ([], []))
        self.tracker.submit(parse_event(self.sync(2, 0.2)), 0.2)
        (fix,), (timing,) = self.tracker.update(0.2)
        self.assertLess(math.dist((fix["x"], fix["y"]), self.position[:2]), 0.02)
        self.assertEqual(set(timing[-1]), {1002, 1003, 1004})
        self.assertFalse(self.tracker.submit(parse_event(self.blink()), 0.21))
        self.assertFalse(self.tracker.snapshot(1.2)["tags"][0]["live"])

    def test_anchor_restart_does_not_bridge_clocks(self):
        self.tracker.submit(parse_event(self.sync(1, 0)), 0)
        self.tracker.submit(parse_event(self.blink()), 0.09)
        self.tracker.submit(parse_event(self.sync(2, 0.2, rebooted=True)), 0.2)
        fixes, (timing,) = self.tracker.update(1.3)
        self.assertEqual(fixes, [])
        self.assertEqual(set(timing[-1]), {1002, 1003})
        self.assertEqual(self.tracker.counts["rejected"], 1)
        self.assertIn("2 received", self.tracker.snapshot(1.3)["tags"][0]["status"])

    def test_rejects_out_of_range_timestamp(self):
        invalid = self.blink()
        invalid["rx"][0]["ticks"] = TICK_MODULUS
        with self.assertRaisesRegex(ValueError, "Invalid ticks"):
            parse_event(invalid)


if __name__ == "__main__":
    unittest.main()
