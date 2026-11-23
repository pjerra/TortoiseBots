#!/usr/bin/env python3
"""Wiring pins for the strategy rows the unregistered-key audit switched on (#209 group a).

Each key below was named by a compiled TB row but not registered, so the row never fired (the engine skips a missing
trigger/action and a missing value reads as an inert stand-in). The pins prove the keys stay registered through the
generator, with the full build's own creator and kind, and that the registry check no longer lists them:
  * the generator's key lists name every key, so a later regeneration cannot drop one;
  * the generated SliceAiObjectContext.cpp registers it under the same kind as the full build (a value that slipped
    into the trigger list would still compile);
  * slice_registry_check.py has (a) empty and none of these keys left in (b);
  * the two sources the new rows need (the check-values action, the stuck values) are in the list the slice build reads.

Run: python3 -m unittest forever/tools/test_209_keys_wiring.py
"""
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402
import slice_gen_context as gen  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"

# (row, kind, key): the row that fires the key and the kind the full build registers it as.
ROWS = [
    ("dps aoe", "strategy", "dps aoe"),
    ("dps aoe", "action", "dps aoe"),
    ("dps aoe", "trigger", "not dps aoe target active"),
    ("salvation removal", "action", "remove blessing of salvation"),
    ("salvation removal", "action", "remove greater blessing of salvation"),
    ("salvation removal", "trigger", "has blessing of salvation"),
    ("salvation removal", "trigger", "has greater blessing of salvation"),
    ("check values", "action", "check values"),
    ("check values", "strategy", "map"),
    ("check values", "strategy", "map full"),
    ("check values", "value", "all targets"),
    ("target books", "value", "keepout set aside"),
    ("target books", "value", "runners let go"),
    ("target books", "value", "unreachable entries"),
    ("target books", "value", "unreachable targets"),
    ("corpse timers", "value", "current position"),
    ("corpse timers", "value", "time since last change"),
    ("behaviour toggles", "strategy", "attack tagged"),
    ("behaviour toggles", "strategy", "preheal"),
    ("behaviour toggles", "strategy", "silent"),
]


class Registered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()

    def test_generator_lists_every_key(self):
        for row, kind, key in ROWS:
            self.assertIn(key, gen.KEYS[kind], "%s: %s '%s' is not in slice_gen_context.py" % (row, kind, key))

    def test_generated_context_registers_every_key_with_the_full_builds_creator(self):
        slice_reg = chk.slice_registry()
        for row, kind, key in ROWS:
            self.assertIn(key, slice_reg[kind], "%s: %s '%s' is not in SliceAiObjectContext.cpp" % (row, kind, key))
            self.assertEqual(slice_reg[kind][key]["expr"], self.full[kind][key]["expr"], key)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_gen_context.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_registry_check_no_longer_lists_them(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_registry_check.py"), "--list"],
                              capture_output=True, text=True, timeout=300)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr)
        self.assertIn("(a) slice keys that are not in the full build, or map elsewhere: 0", proc.stdout)
        for row, kind, key in ROWS:
            self.assertIsNone(re.search(r"(?m)^    %s  " % re.escape(key), proc.stdout),
                              "%s: %s '%s' is still on the missing list" % (row, kind, key))

    def test_new_sources_are_in_the_slice_list(self):
        with open(os.path.join(ROOT, LIST), encoding="utf-8") as f:
            paths = {line.strip() for line in f if line.strip() and not line.startswith("#")}
        for path in ("ai/playerbot/strategy/actions/CheckValuesAction.cpp",
                     "ai/playerbot/strategy/values/StuckValues.cpp"):
            self.assertIn(path, paths, "%s is not in %s" % (path, LIST))
