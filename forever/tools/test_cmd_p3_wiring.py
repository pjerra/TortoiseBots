#!/usr/bin/env python3
"""Wiring pins and mutation test for Commander P3 (`.tb cmd` through the lifted `run::Issue` judge).

Run:
  python3 forever/tools/test_cmd_p3_wiring.py
  CMD_P3_MUTATE=1 python3 forever/tools/test_cmd_p3_wiring.py
"""
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"
CMD = HOST / "cmd"
RUN = HOST / "run"


def read(path):
    return Path(path).read_text()


def strip_comments(source):
    return re.sub(r"//[^\n]*", "", source)


def check_cmd_orders_uses_judge(cmd_orders_src):
    c = strip_comments(cmd_orders_src)
    if "run::Issue(" not in c:
        raise AssertionError("TbCmdOrders.cpp does not route orders through run::Issue")
    for forbidden in ("h3::QueueOrder", "h3::PreflightOrder", "Enqueue("):
        if forbidden in c:
            raise AssertionError(f"TbCmdOrders.cpp bypasses the judge via {forbidden}")


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cmd_orders = read(CMD / "TbCmdOrders.cpp")
        self.run_orders = read(RUN / "TbRunOrders.cpp")
        self.run_orders_h = read(RUN / "TbRunOrders.h")
        self.run_cpp = read(RUN / "TbRun.cpp")
        self.orders_cpp = read(HOST / "TbOrders.cpp")
        self.scripts_cpp = read(ROOT / "forever" / "src" / "TbScripts.cpp")

    def test_cmd_orders_routes_through_lifted_issue(self):
        check_cmd_orders_uses_judge(self.cmd_orders)
        c = strip_comments(self.cmd_orders)
        self.assertIn("party.fromRun = false", c)
        self.assertIn("party.persistentIcons = true", c)
        self.assertIn("h3::ParseGuidText", c)

    def test_lifted_issue_owns_note_run_order(self):
        c = strip_comments(self.run_orders)
        self.assertIn("OrderAnswer Issue(Party const& party", c)
        self.assertEqual(c.count("cmd::NoteRunOrder("), 1, "NoteRunOrder must live once inside lifted Issue(Party const&)")
        self.assertNotIn("cmd::NoteRunOrder(", strip_comments(self.run_cpp))

    def test_persistent_icons_require_live_group(self):
        c = strip_comments(self.run_orders)
        m = re.search(r"void SetPersistentSkull\([^)]*\)\s*\{([^}]*)\}", c, re.S)
        self.assertIsNotNone(m, "SetPersistentSkull helper missing")
        body = m.group(1)
        self.assertIn("party.lead->GetGroup()", body)
        self.assertIn("if (!group)", body)
        self.assertIn("group->SetTargetIcon(", body)

    def test_scripts_table_registers_cmd(self):
        c = strip_comments(self.scripts_cpp)
        self.assertIn('"cmd"', c)
        self.assertIn("TortoiseBots::cmd::HandleCmd", c)

    def test_preflight_allows_hold(self):
        c = strip_comments(self.orders_cpp)
        self.assertIn('verb == "hold"', c)


if __name__ == "__main__":
    if os.environ.get("CMD_P3_MUTATE") == "1":
        # Mutation: commander orders skip the judge and call h3::QueueOrder directly.
        mutated = strip_comments(read(CMD / "TbCmdOrders.cpp")).replace("run::Issue(", "h3::QueueOrder(")
        failed = False
        try:
            check_cmd_orders_uses_judge(mutated)
        except AssertionError as exc:
            print(f"MUTATION FAILED AS EXPECTED: {exc}")
            failed = True
        if not failed:
            print("ERROR: mutation 'skip-judge' was not caught!", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)
    unittest.main()
