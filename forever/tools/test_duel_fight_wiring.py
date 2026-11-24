#!/usr/bin/env python3
"""Wiring pins for the duel fight (TBF #163 P2).

The duel strategy is TB's (strategy/generic/DuelStrategy.cpp): `duel requested` -> `accept duel` (inert here, the host
answers) and `no attackers` -> `attack duel opponent` on `duel target`. This round compiles the strategy, registers its
keys through forever/tools/slice_gen_context.py and puts `duel` in both default kits outside a battleground, so the
class kit fights the duel instead of the grind. The pins:

  * DuelStrategy.cpp is on the slice list the Forever build reads;
  * every duel key is in the generator and the generated SliceAiObjectContext.cpp registers it with the full build's
    own creator;
  * the slice factory adds `duel` to the combat and the non-combat engine, outside a battleground only, for free and
    hired bots alike (the free kit policy itself is untouched);
  * `duel target` was already there for `attack duel opponent` to read.

Run: python3 -m unittest forever/tools/test_duel_fight_wiring.py
"""
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402
import slice_gen_context as gen  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"
FACTORY = "forever/src/slice/SliceAiFactory.cpp"
KIT = "ai/playerbot/policy/world/FreeKitPolicy.h"

SOURCE = "ai/playerbot/strategy/generic/DuelStrategy.cpp"

# (kind, key): every key the duel rows need, in the full build's kind.
KEYS = [
    ("strategy", "duel"),
    ("action", "attack duel opponent"),
    ("trigger", "no attackers"),
    ("trigger", "duel requested"),
    ("value", "duel target"),
]


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


class SliceMembership(unittest.TestCase):
    def test_the_strategy_source_is_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        self.assertIn(SOURCE, paths)


class Registered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()
        cls.registered = chk.slice_registry()

    def test_generator_lists_every_key(self):
        for kind, key in KEYS:
            self.assertIn(key, gen.KEYS[kind], "%s '%s' is not in slice_gen_context.py" % (kind, key))

    def test_generated_context_registers_every_key_with_the_full_builds_creator(self):
        for kind, key in KEYS:
            self.assertIn(key, self.registered[kind], "%s '%s' is not in SliceAiObjectContext.cpp" % (kind, key))
            self.assertEqual(self.registered[kind][key]["expr"], self.full[kind][key]["expr"], key)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_gen_context.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_the_accept_action_stays_out(self):
        # The bot answers through the host: its session never receives the duel packet `duel requested` marks.
        self.assertIsNone(self.registered["action"].get("accept duel"))


class Kits(unittest.TestCase):
    def test_the_combat_kit_gets_the_duel_strategy_outside_a_battleground(self):
        text = read(FACTORY)
        start = text.index("void AiFactory::AddDefaultCombatStrategies")
        block = text[start:text.index("void AiFactory::AddDefaultNonCombatStrategies", start)]
        self.assertIn('if (!player->InBattleGround())\n        combatEngine->addStrategy("duel");', block)

    def test_the_non_combat_kit_gets_it_for_free_and_hired_bots(self):
        text = read(FACTORY)
        start = text.index("void AiFactory::AddDefaultNonCombatStrategies")
        block = text[start:text.index("void AiFactory::AddDefaultDeadStrategies", start)]
        self.assertIn('if (player && !player->InBattleGround())\n            nonCombatEngine->addStrategy("duel");', block)
        # Before the free bot's early return, so a free bot gets it too.
        self.assertLess(block.index('addStrategy("duel")'), block.index("sRandomBotFacade.IsFreeBot"))

    def test_the_free_kit_policy_is_untouched(self):
        self.assertNotIn('"duel"', read(KIT))


if __name__ == "__main__":
    unittest.main()
