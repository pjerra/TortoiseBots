#!/usr/bin/env python3
"""Wiring pin for the Codex review fix of fix/run-review-1: a queued `move-direct` point belongs to a Map INSTANCE, not to a map id.

`move-direct` used to compare only `GetMapId()`: a bot that entered another copy of the same dungeon while the order waited (up to OrderMaxAgeMs) passed the
check and walked the old instance's coordinates. These assertions fail on the commit before the fix:

  * the queue records the lead's instance with the map;
  * the executor compares map AND instance through the pure `PointOnBotMap` rule (equal map id, another instance refuses);
  * OrderSpec/Order carry the value through QueueOrder.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src" / "host"


def text(rel):
    return (SRC / rel).read_text()


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


class RevFix2(unittest.TestCase):
    def test_move_direct_checks_the_instance(self):
        body = function_body(text("TbOrders.cpp"), r'if \(o\.verb == "move-direct"\)\s*\{')
        self.assertIn("PointOnBotMap(o.map, o.instance, bot->GetMapId(), bot->GetInstanceId())", body)
        self.assertLess(body.index("PointOnBotMap("), body.index("SegmentPrefixRefusedOnMap("))
        self.assertLess(body.index("PointOnBotMap("), body.index("MovePoint("))
        self.assertNotRegex(body, r"o\.map && bot->GetMapId\(\) != o\.map")

    def test_the_run_records_the_instance_with_the_point(self):
        orders = text("run/TbRunOrders.cpp")
        self.assertIn('batch.Add(lead, "move-direct", true, true);', orders)
        self.assertIn("batch.Back().instance = lead->GetInstanceId();", orders)

    def test_the_queue_carries_the_instance(self):
        api = (ROOT / "forever" / "src" / "host" / "TbOrdersApi.h").read_text()
        self.assertRegex(api, r"struct OrderSpec\s*\{[^}]*uint32 instance")
        orders = text("TbOrders.cpp")
        self.assertRegex(orders, r"struct Order\s*\{[^}]*uint32 instance")
        self.assertIn("order.instance = spec.instance;", orders)


if __name__ == "__main__":
    unittest.main()
