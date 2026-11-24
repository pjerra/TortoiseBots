#!/usr/bin/env python3
"""Wiring pins for the emote and mount-animation rows (#209 round 3, worktree w209d).

The emote strategy was compiled with only its `new player nearby` -> `greet` row live: `emote` and `mount anim` were
unregistered action keys, so the engine skipped those rows, and the periodic emote also read `last emote` as a missing
value (an inert stand-in), which would have removed TB's cooldown. The pins prove:
  * the generator lists the keys and the generated SliceAiObjectContext.cpp registers each under the same kind with the
    full build's own creator;
  * EmoteAction.cpp is on the build list the Forever build reads;
  * that file reaches the core only through the both-core helpers (no raw session handler call, no Forever-only field);
  * both the Forever and the Turtle half of the helper pair define it, so the Turtle gate compiles too;
  * the rows this round leaves out stay out (their triggers cannot fire on Forever).

Run: python3 -m unittest forever/tools/test_w209d_emote_wiring.py
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
ACTION = "ai/playerbot/strategy/actions/EmoteAction.cpp"
FOREVER_COMPAT = "forever/src/compat/TbCompat_Unported.h"
TURTLE_COMPAT = "ai/cmangos-compat-unported.h"

# (row, kind, key): the row that fires the key and the kind the full build registers it as.
ROWS = [
    ("periodic emote", "action", "emote"),
    ("periodic emote", "value", "last emote"),
    ("mount anim", "action", "mount anim"),
    ("talk action book", "value", "talk target"),
]

# Left out on purpose: no Forever packet handler table is registered and no emote packet is polled, so these would be
# inert rows; `talk` is idle chatter TB gates off for a commanded bot.
LEFT_OUT = [
    ("trigger", "seldom"),
    ("action", "talk"),
    ("trigger", "receive text emote"),
    ("trigger", "receive emote"),
]


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


class EmoteRows(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()

    def test_generator_lists_every_key(self):
        for row, kind, key in ROWS:
            self.assertIn(key, gen.KEYS[kind], "%s: %s '%s' is not in slice_gen_context.py" % (row, kind, key))

    def test_left_out_keys_stay_out(self):
        for kind, key in LEFT_OUT:
            self.assertNotIn(key, gen.KEYS[kind], "%s '%s' is registered but no Forever trigger feeds it" % (kind, key))

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
        self.assertNotIn("stand-in lacks", proc.stdout)

    def test_emote_action_is_on_the_build_list(self):
        with open(os.path.join(ROOT, LIST), encoding="utf-8") as f:
            paths = {line.strip() for line in f if line.strip() and not line.startswith("#")}
        self.assertIn(ACTION, paths, "%s is not in %s" % (ACTION, LIST))

    def test_the_action_reaches_the_core_only_through_the_compat_pair(self):
        src = read(ACTION)
        self.assertEqual(src.count("PlayTextEmoteCompat("), 4, "every text-emote send goes through the helper")
        self.assertEqual(src.count("MountSpecialAnimCompat(bot)"), 1, "the mount animation goes through the helper")
        for forbidden in ("HandleTextEmoteOpcode", "HandleMountSpecialAnimOpcode",
                          "em->textid", "em->Id", "UNIT_NPC_FLAGS"):
            self.assertNotIn(forbidden, src, "%s is a core-only spelling the other build rejects" % forbidden)
        self.assertIn("HandleEmoteCommand((::Emote)", src, "the emote id is cast to the global Emote enum (the member function Emote hides the bare name)")

    def test_both_cores_define_the_helper_pair(self):
        forever = read(FOREVER_COMPAT)
        turtle = read(TURTLE_COMPAT)
        for helper in ("MountSpecialAnimCompat", "TextEmoteAnimIdCompat", "TextEmoteRowIdCompat"):
            self.assertIn(helper, forever, "%s is missing on the Forever side" % helper)
            self.assertIn(helper, turtle, "%s is missing on the Turtle side" % helper)
        self.assertIn("MiscPackets.h", forever, "the Forever mount-special helper needs the typed packet")
        self.assertIn("em->EmoteID", forever)
        self.assertIn("em->textid", turtle)


if __name__ == "__main__":
    unittest.main()
