#!/usr/bin/env python3
"""Wiring pins for the item-use keys the unregistered-key audit switched on (#209 round 3a).

The maintenance strategy and the class AoE kits name these actions, but the slice did not register them, so the rows never
fired (the engine skips a missing action). The pins prove they stay registered through the generator, with the full build's
own creator and kind:
  * the generator's key list names every key, so a later regeneration cannot drop one;
  * the generated SliceAiObjectContext.cpp registers it under the same kind as the full build;
  * slice_registry_check.py has (a) empty and none of these keys left in (b);
  * the source the actions live in is on the list the slice build reads;
  * `open random item` stays out on purpose: opening a bag item is unported on Forever (OpenItemCompat).

Run: python3 -m unittest forever/tools/test_209_r3a_keys_wiring.py
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
    ("maintenance", "action", "use random recipe"),
    ("maintenance", "action", "use random quest item"),
    ("aoe retribution pve", "action", "oil of immolation"),
    ("aoe", "action", "throw grenade"),
]

LEFT_OUT = "open random item"


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

    def test_action_source_is_in_the_slice_list(self):
        with open(os.path.join(ROOT, LIST), encoding="utf-8") as f:
            paths = {line.strip() for line in f if line.strip() and not line.startswith("#")}
        self.assertIn("ai/playerbot/strategy/actions/UseItemAction.cpp", paths, "UseItemAction.cpp is not in %s" % LIST)

    def test_open_random_item_stays_out(self):
        self.assertNotIn(LEFT_OUT, gen.KEYS["action"], "open random item cannot work: OpenItemCompat is unported on Forever")
        self.assertNotIn(LEFT_OUT, chk.slice_registry()["action"])


if __name__ == "__main__":
    unittest.main()
