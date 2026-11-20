"""Wiring pins for `.tb ignore` (issue #13, the leave-it-alone list).

Run:
  python3 -m unittest forever/tools/test_ignore13_wiring.py
  python3 forever/tools/test_ignore13_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
IGNORE_H = (ROOT / "forever/src/host/TbIgnore.h").read_text()
IGNORE_CPP = (ROOT / "forever/src/host/TbIgnore.cpp").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_I13.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-i13.h").read_text()
SHIM_F = (ROOT / "forever/src/compat/forever-compat-shim.h").read_text()
SHIM_T = (ROOT / "ai/cmangos-compat-shim.h").read_text()
H3 = (ROOT / "forever/src/host/TbH3.cpp").read_text()
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()
CC = (ROOT / "forever/src/host/TbCc.cpp").read_text()
ISVALID = (ROOT / "ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp").read_text()
RTI = (ROOT / "ai/playerbot/strategy/values/RtiTargetValue.h").read_text()
DPS = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
GRIND = (ROOT / "ai/playerbot/strategy/values/GrindTargetValue.cpp").read_text()
TVAL_H = (ROOT / "ai/playerbot/strategy/values/TargetValue.h").read_text()
TVAL_CPP = (ROOT / "ai/playerbot/strategy/values/TargetValue.cpp").read_text()
CAST = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
WARRIOR = (ROOT / "ai/playerbot/strategy/warrior/WarriorActions.h").read_text()
ROGUE = (ROOT / "ai/playerbot/strategy/rogue/RogueActions.h").read_text()
DOCS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()
POLICY = (ROOT / "ai/playerbot/policy/combat/IgnoreMobPolicy.h").read_text()


def check_wiring(scripts, ignore_h, ignore_cpp, compat_f, compat_t,
                 shim_f, shim_t, h3, orders, cc,
                 isvalid, rti, dps, grind, tval_h, tval_cpp,
                 cast, warrior, rogue, docs, policy):
    if '"ignore", TortoiseBots::i13::HandleIgnore' not in scripts:
        raise AssertionError('TbScripts.cpp missing "ignore" row -> i13::HandleIgnore')
    for decl in ("bool HandleIgnore(", "bool IsIgnored(", "bool NoteExplicitTarget(", "void SelfTestRecords("):
        if decl not in ignore_h:
            raise AssertionError("TbIgnore.h missing " + decl)
    if "NotBuilt(handler, args, \"ignore\")" not in ignore_cpp:
        raise AssertionError('TbIgnore.cpp OFF build must answer "ignore" with engine-not-built')
    if "bool HandleIgnore(" not in ignore_cpp:
        raise AssertionError("TbIgnore.cpp missing the slice HandleIgnore")
    if "IsIgnoredMobCompat" not in compat_f or "i13::IsIgnored(" not in compat_f:
        raise AssertionError("TbCompat_I13.h must forward IsIgnoredMobCompat to i13::IsIgnored")
    if "IsIgnoredMobCompat" not in compat_t or "return false" not in compat_t:
        raise AssertionError("ai/cmangos-compat-i13.h must answer IsIgnoredMobCompat false")
    if "cmangos-compat-i13.h" not in shim_t:
        raise AssertionError("ai/cmangos-compat-shim.h missing the i13 twin")
    if "TbCompat_I13.h" not in shim_f:
        raise AssertionError("forever-compat-shim.h missing TbCompat_I13.h")
    if "i13::SelfTestRecords(reply);" not in h3:
        raise AssertionError("TbH3.cpp must call i13::SelfTestRecords in the grammar selftest")
    if orders.count("i13::NoteExplicitTarget(bot, ") < 3:
        raise AssertionError("TbOrders.cpp must un-ignore on attack/assist, pull and focus (3 call sites)")
    if '"ignored"' not in cc or "IsIgnoredMobCompat(bot, unit)" not in cc:
        raise AssertionError("TbCc.cpp ApplyCcOrder must refuse a listed mob with code ignored")

    # The engine honours the list: every target pick skips it, area spells and
    # the two swing-splash buffs spare it. Turtle behaviour is unchanged (the
    # twin answers false there).
    if "IsIgnoredMobCompat(player, target->getObjectGuid())" not in isvalid:
        raise AssertionError("PossibleAttackTargetsValue::IsValid must refuse ignored mobs")
    if "IsIgnoredMobCompat(bot, unit->GetObjectGuid())" not in rti:
        raise AssertionError("RtiTargetValue::CalculateMark must skip ignored mobs")
    if "IsIgnoredMobCompat(bot, guid)" not in dps:
        raise AssertionError("DpsTargetValue runner finish must skip ignored mobs")
    if "IsIgnoredMobCompat(bot, unit->getObjectGuid())" not in grind:
        raise AssertionError("GrindTargetValue must skip ignored mobs")
    if "bool IgnoredMobNear(WorldObject* blastCenter, float blastRadius);" not in tval_h:
        raise AssertionError("TargetValue.h must declare CcGuard::IgnoredMobNear")
    if "bool CcGuard::IgnoredMobNear(" not in tval_cpp or "AoEBlockedByIgnored" not in tval_cpp:
        raise AssertionError("TargetValue.cpp must implement IgnoredMobNear on the AoE rule")
    if "ccGuard.IgnoredMobNear(" not in cast:
        raise AssertionError("GenericSpellActions.cpp BreaksCcInArea must block AoE near ignored mobs")
    if "ccGuard.IgnoredMobNear(bot, radius)" not in warrior:
        raise AssertionError("WarriorActions.h must cancel Sweeping Strikes near ignored mobs")
    if "ccGuard.IgnoredMobNear(bot, radius)" not in rogue:
        raise AssertionError("RogueActions.h must cancel Blade Flurry near ignored mobs")

    # The decision half is pure and pinned: eight mobs, 30 min, ~30 yd.
    if "kIgnoreMobMax = 8" not in policy or "kIgnoreMobExpirySec = 30u * 60u" not in policy:
        raise AssertionError("IgnoreMobPolicy.h must pin 8 mobs and 30 min")
    if "kIgnoreMobAoERadiusYd = 30.0f" not in policy:
        raise AssertionError("IgnoreMobPolicy.h must pin the ~30 yd AoE radius")

    if "## 14. The leave-it-alone list" not in docs or "`.tb ignore" not in docs:
        raise AssertionError("TB-COMMANDS.md missing the .tb ignore section")
    if "`ignored`" not in docs:
        raise AssertionError("TB-COMMANDS.md missing the ignored error code")


class IgnoreWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check_wiring(SCRIPTS, IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T,
                     SHIM_F, SHIM_T, H3, ORDERS, CC,
                     ISVALID, RTI, DPS, GRIND, TVAL_H, TVAL_CPP,
                     CAST, WARRIOR, ROGUE, DOCS, POLICY)

    def test_mutations_fail(self):
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS.replace('"ignore", TortoiseBots::i13::HandleIgnore', '"ignore", TortoiseBots::i13::HandleNope'),
                         IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T, SHIM_F, SHIM_T, H3, ORDERS, CC,
                         ISVALID, RTI, DPS, GRIND, TVAL_H, TVAL_CPP, CAST, WARRIOR, ROGUE, DOCS, POLICY)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T, SHIM_F, SHIM_T, H3,
                         ORDERS.replace("i13::NoteExplicitTarget(bot, ", "i13::NoteNothing(bot, "),
                         CC, ISVALID, RTI, DPS, GRIND, TVAL_H, TVAL_CPP, CAST, WARRIOR, ROGUE, DOCS, POLICY)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T, SHIM_F, SHIM_T, H3, ORDERS, CC,
                         ISVALID.replace("IsIgnoredMobCompat(player, target->getObjectGuid())", "false"),
                         RTI, DPS, GRIND, TVAL_H, TVAL_CPP, CAST, WARRIOR, ROGUE, DOCS, POLICY)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T, SHIM_F, SHIM_T, H3, ORDERS, CC,
                         ISVALID, RTI, DPS, GRIND, TVAL_H, TVAL_CPP,
                         CAST.replace("ccGuard.IgnoredMobNear(", "ccGuard.ProtectedEnemyNear("),
                         WARRIOR, ROGUE, DOCS, POLICY)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_wiring(SCRIPTS, IGNORE_H, IGNORE_CPP, COMPAT_F, COMPAT_T, SHIM_F, SHIM_T, H3,
                     ORDERS.replace("i13::NoteExplicitTarget(bot, ", "i13::NoteNothing(bot, "),
                     CC, ISVALID, RTI, DPS, GRIND, TVAL_H, TVAL_CPP, CAST, WARRIOR, ROGUE, DOCS, POLICY)
        sys.exit(0)
    unittest.main()
