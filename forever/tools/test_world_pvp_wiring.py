#!/usr/bin/env python3
"""Wiring pins for open-world PvP seek (TBF #163 P3).

TB's feature: every bot gets the `pvp` combat strategy, a share of free pool bots gets the `world pvp` seek role
(`enemy player near` -> `attack enemy player`), and the flagged share of the pool carries the /pvp preference. The
target rules live in WorldPvpSeekChecks (PvP-flagged only, prohibited zones, graveyards, level-gap odds). This round
compiles those files into the Forever slice, registers their keys and adds the host role and flag share. The pins:

  * the five sources are on the slice list and the stubs are gone;
  * every key is in the generator and the generated context registers it with the full build's own creator;
  * the slice factory gives every bot `pvp`;
  * the host rolls the role per bot and sets the /pvp preference at login, for free bots only (hires and owned bots
    have a master, so they never reach the policy);
  * the seek target test still refuses an unflagged player, and the compat pair keeps the Turtle challenge rule.

Run: python3 -m unittest forever/tools/test_world_pvp_wiring.py
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
STUBS = "forever/src/slice/SliceLinkStubs.cpp"
HOST = "forever/src/host/TbHostWorldPvp.cpp"
TICK = "forever/src/TbBotMgr.cpp"
BRAIN = "forever/src/host/TbEngineBrain.cpp"
LOGIN = "forever/src/TbScripts.cpp"
SEEK = "ai/playerbot/strategy/values/WorldPvpSeekChecks.cpp"
FOREVER_COMPAT = "forever/src/compat/TbCompat_PvP.h"
TURTLE_COMPAT = "ai/cmangos-compat-pvp.h"

SOURCES = [
    "ai/playerbot/strategy/generic/WorldPvpStrategy.cpp",
    "ai/playerbot/strategy/generic/AttackEnemyPlayersStrategy.cpp",
    "ai/playerbot/strategy/values/EnemyPlayerValue.cpp",
    "ai/playerbot/strategy/values/WorldPvpSeekChecks.cpp",
    "ai/playerbot/strategy/triggers/EnemyPlayerNearTrigger.cpp",
]

KEYS = [
    ("strategy", "pvp"),
    ("strategy", "world pvp"),
    ("action", "attack enemy player"),
    ("trigger", "enemy player near"),
    ("value", "enemy player target"),
    ("value", "enemy player targets"),
    ("value", "has enemy player targets"),
]


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


class SliceMembership(unittest.TestCase):
    def test_the_sources_are_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        for source in SOURCES:
            self.assertIn(source, paths)

    def test_the_pvp_stubs_are_gone(self):
        text = read(STUBS)
        self.assertNotIn("WorldPvpSeekChecks::", text)
        self.assertNotIn("EnemyPlayerValue::GetMaxAttackDistance", text)


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


class Kits(unittest.TestCase):
    def test_the_combat_kit_gets_pvp(self):
        text = read(FACTORY)
        start = text.index("void AiFactory::AddDefaultCombatStrategies")
        block = text[start:text.index("void AiFactory::AddDefaultNonCombatStrategies", start)]
        self.assertIn('combatEngine->addStrategy("pvp");', block)
        # TB gives it inside and outside a battleground alike.
        self.assertLess(block.index('addStrategy("pvp")'), block.index('if (!player->InBattleGround())'))


class Host(unittest.TestCase):
    def test_every_bot_gets_a_role_pass(self):
        self.assertIn("tbhost::worldpvp::Tick(me);", read(TICK))

    def test_login_sets_the_flag_through_the_host_service(self):
        text = read(LOGIN)
        self.assertIn("tbhost::worldpvp::OnBotLogin(player);", text)
        self.assertLess(text.index("#if defined(TORTOISEBOTS_SLICE1)"), text.index("worldpvp::OnBotLogin"))

    def test_only_free_bots_are_considered(self):
        text = read(HOST)
        # A master binding clears `free`, so a hire or an owned bot is never eligible.
        self.assertIn("IsFreeBot", text)
        self.assertIn("WorldPvpSeekEligible", text)
        self.assertIn("PoolBotInPvpFlaggedShare", text)

    def test_the_role_needs_engine_eligibility_on_every_pass(self):
        text = read(HOST)
        self.assertIn("WorldPvpSeekRerollDue", text)
        self.assertIn("bot->IsInCombat()", text)
        # A kit rebuild drops a strategy the kit does not name: the granted role is put back by the rebuild hook, not
        # every tick (so a player's own `nc -world pvp` is not undone at once).
        self.assertIn("role=regrant", text)
        self.assertIn("IsFreeBot", text)
        brain = read(BRAIN)
        self.assertIn("tbhost::worldpvp::ApplyGrantedRole(bot);", brain)
        self.assertIn("tbhost::worldpvp::ApplyGrantedRole(brain->bot);", brain)


class TargetRules(unittest.TestCase):
    def test_an_unflagged_player_is_never_a_seek_target(self):
        text = read(SEEK)
        self.assertIn("if (!target->IsPvP())", text)
        self.assertIn('WorldPvpLevelGapOdds(gap)', text)

    def test_the_challenge_rule_stays_on_the_turtle_side(self):
        self.assertIn("HasChallenge(CHALLENGE_HARDCORE)", read(TURTLE_COMPAT))
        self.assertIn("HasPvpChallengeModeCompat", read(FOREVER_COMPAT))
        forever = read(FOREVER_COMPAT)
        self.assertIn("return false;", forever)  # no challenge modes on this core


if __name__ == "__main__":
    unittest.main()
