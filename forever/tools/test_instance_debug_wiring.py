#!/usr/bin/env python3
"""Wiring pins for the instance admission and the three debug shims.

The ForeverCore bodies are the core's own calls now, the Turtle twins keep the original behaviour, and the four feature
rows are gone from the closed table and from UNPORTED.md. Run:
  python3 -m unittest forever/tools/test_instance_debug_wiring.py
  python3 forever/tools/test_instance_debug_wiring.py --mutate   # must exit non-zero (every mutation caught)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text()


FEATURES = read("ai/playerbot/UnportedFeatures.h")
UNPORTED = read("forever/docs/UNPORTED.md")
F_UNPORTED = read("forever/src/compat/TbCompat_Unported.h")
F_S3 = read("forever/src/compat/TbCompat_S3.h")
T_UNPORTED = read("ai/cmangos-compat-unported.h")
T_S3 = read("ai/cmangos-compat-s3.h")
PLAYERBOT_AI = read("ai/playerbot/PlayerbotAI.cpp")

PORTED = ("InstanceBind", "AuraHolders", "SpeedCheat", "DebugVisuals")
UNPORTED_ROWS = ("instance bind", "aura holders (debug apply)", "speed cheat", "debug spell visuals")


def check_rows(features, unported):
    for name in PORTED:
        if "X(%s," % name in features:
            raise AssertionError("UnportedFeatures.h still lists " + name)
        if "Feature::" + name in F_UNPORTED + F_S3:
            raise AssertionError("a ForeverCore helper still counts Feature::" + name)
        if "Feature::" + name in T_UNPORTED:
            raise AssertionError("the Turtle twin still names Feature::" + name)
    if features.count("    X(") != 53:
        raise AssertionError("the boot line is not 53 features")
    for row in UNPORTED_ROWS:
        if "| " + row + " |" in unported:
            raise AssertionError("UNPORTED.md still has the row " + row)


def check_instance(text):
    if "tbcompat::Feature::InstanceBind" in text:
        raise AssertionError("the instance helper still counts InstanceBind")
    if '#include "InstanceLockMgr.h"' not in text:
        raise AssertionError("TbCompat_Unported.h does not include the instance-lock header")
    for needle in ("area->GetMapId()", "sMapMgr.FindInstanceIdForPlayer(mapId, bot)",
                   "instance->GetPlayersCountExceptGMs() >= instance->GetMaxPlayers()",
                   "script && script->IsEncounterInProgress()",
                   "sInstanceLockMgr.CanJoinInstanceLock(bot->GetGUID(), MapDb2Entries(mapEntry, map->GetMapDifficulty()), lock)"):
        if needle not in text:
            raise AssertionError("CanEnterAreaCompat misses " + needle)
    if "if (bot->GetMapId() == mapId)" not in text:
        raise AssertionError("CanEnterAreaCompat checks the bot's own map")


def check_aura(f_unported, t_unported, ai):
    for needle in ("tbcompat::SpellCore(spellInfo)", "effect.IsAreaAuraEffect()", "SPELL_EFFECT_PERSISTENT_AREA_AURA",
                   "effMask |= 1u << effect.EffectIndex;", "unit->AddAura(core, effMask, unit)"):
        if needle not in f_unported:
            raise AssertionError("ApplyDebugAuraCompat misses " + needle)
    for needle in ("CreateSpellAuraHolder(spellInfo, unit, unit, unit)", "holder->AddAura(aur, SpellEffectIndex(i))",
                   "unit->AddSpellAuraHolder(holder)"):
        if needle not in t_unported:
            raise AssertionError("the Turtle ApplyDebugAuraCompat misses " + needle)
    if "return ApplyDebugAuraCompat(unit, spellInfo);" not in ai:
        raise AssertionError("PlayerbotAI::AddAura does not use the helper")
    if "UnportedBlockResultCompat(tbcompat::Feature::AuraHolders" in ai:
        raise AssertionError("PlayerbotAI::AddAura still runs the unported block")


def check_speed(f_unported, t_unported):
    for move in ("MOVE_WALK", "MOVE_RUN", "MOVE_SWIM"):
        if "bot->SetSpeedRate(%s, 10.0f);" % move not in f_unported:
            raise AssertionError("SpeedCheatCompat misses " + move)
    if "bot->UpdateSpeed(MOVE_RUN, true, 10);" not in t_unported:
        raise AssertionError("the Turtle speed cheat changed")


def check_visual(f_s3, t_s3):
    if "unit->SendPlaySpellVisualKit(kitId, 0, 0);" not in f_s3:
        raise AssertionError("DebugSpellVisualCompat does not send the kit")
    if "unit->SendPlaySpellVisual(kitId);" not in t_s3:
        raise AssertionError("the Turtle debug visual changed")


class InstanceDebugWiringTest(unittest.TestCase):
    def test_rows(self):
        check_rows(FEATURES, UNPORTED)

    def test_instance(self):
        check_instance(F_UNPORTED)

    def test_aura(self):
        check_aura(F_UNPORTED, T_UNPORTED, PLAYERBOT_AI)

    def test_speed(self):
        check_speed(F_UNPORTED, T_UNPORTED)

    def test_visual(self):
        check_visual(F_S3, T_S3)

    def test_mutations_fail(self):
        mutations = {
            "row back in the table": lambda: check_rows(FEATURES + "\n    X(SpeedCheat, \"speed cheat\", \"\")\n", UNPORTED),
            "row back in the doc": lambda: check_rows(FEATURES, UNPORTED + "\n| speed cheat | x | 1 | y | z |\n"),
            "feature counted again": lambda: check_instance(F_UNPORTED + "\ntbcompat::Unported(tbcompat::Feature::InstanceBind);\n"),
            "no capacity check": lambda: check_instance(F_UNPORTED.replace("instance->GetPlayersCountExceptGMs() >= instance->GetMaxPlayers()", "false")),
            "no lock check": lambda: check_instance(F_UNPORTED.replace("sInstanceLockMgr.CanJoinInstanceLock(", "0 && CanJoinInstanceLock(")),
            "no aura mask": lambda: check_aura(F_UNPORTED.replace("effMask |= 1u << effect.EffectIndex;", ""), T_UNPORTED, PLAYERBOT_AI),
            "unported block back": lambda: check_aura(F_UNPORTED, T_UNPORTED, PLAYERBOT_AI + "\nUnportedBlockResultCompat(tbcompat::Feature::AuraHolders, false, [&](auto* unit) -> bool\n"),
            "cheat not forced": lambda: check_speed(F_UNPORTED.replace("bot->SetSpeedRate(MOVE_RUN, 10.0f);", ""), T_UNPORTED),
            "nothing drawn": lambda: check_visual(F_S3.replace("unit->SendPlaySpellVisualKit(kitId, 0, 0);", ""), T_S3),
        }
        for name, mutate in mutations.items():
            with self.assertRaises(AssertionError, msg=name):
                mutate()


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        missed = []
        test = InstanceDebugWiringTest("test_mutations_fail")
        try:
            test.test_mutations_fail()
        except AssertionError:
            missed.append("mutations are not all caught")
        print("all mutations caught" if not missed else ", ".join(missed))
        sys.exit(1 if missed else 0)
    unittest.main()
