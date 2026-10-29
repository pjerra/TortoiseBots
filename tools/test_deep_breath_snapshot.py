"""Pins the Deep Breath on the dashboard (fight debugger, owner: show Onyxia's breath on the map).

The core casts Deep Breath TRIGGERED (src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp:522), and a triggered
non-channeled spell never reaches a cast slot (src/game/Spells/Spell.cpp:3640-3642, :3667), so the commander
snapshot's GetCurrentSpell read cannot see it. The snapshot reports the stand row's window spell instead, through the
same detection the stand sets open on (ai/playerbot/BossStandPolicy.h StandStoppedWindowOpen).

These pins prove the live code keeps that path:

 - the cast slots are read FIRST, and only when both are empty is the shared helper asked (a mutation that moves the
   helper call before the cast slots, or drops it, fails the order/call pins);
 - the helper is the same rule FindBossStand feeds (StoppedStandWindowSpell -> StandStoppedWindowOpen -> StandRowActive)
   and reads the stop/no-target state from the unit;
 - a reported breath carries cid, the spell's cast time from its casting time index, a countdown that starts when the
   window opened, no channel and no target;
 - the countdown is keyed by (map, instance, raw guid), pruned by every snapshot cycle, and a binding teardown forgets
   the keys that binding reported (the state itself is pinned by tools/test_stand_window_marks.cpp, which compiles);
 - the committed shape table has all 8 breath ids as a `line`, 60 yd wide, from the first to the last point of the
   core's own spell_target_position chain for that spell (the stand anchors are where the bots stand, not the damage).

Run: python3 -m unittest tools/test_deep_breath_snapshot.py
"""
import math
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/BossStandPolicy.h").read_text()
STAND_H = (ROOT / "ai/playerbot/BossTacticsStand.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
SHAPES = (ROOT / "runtime/SpellShapeData.h").read_text()
MARKS = (ROOT / "runtime/StandWindowMarks.h").read_text()

BREATHS = (17086, 18351, 18564, 18576, 18584, 18596, 18609, 18617)

# Each lane head and the first and last point of its tw_world.spell_target_position chain, read from the block at the
# end of ~/tortoise-wow src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp (map 249). 17086 is the long one: its chain
# runs on past the south hover point into the tunnel.
CHAIN_ENDS = {
    17086: (20.730539, -215.237610, -130.788300, -213.424026, 151.53),
    18351: (-68.834236, -215.036163, 20.730539, -215.237610, 89.565),
    18576: (-37.743851, -243.667923, -37.728523, -188.616806, 55.051),
    18609: (-37.728523, -188.616806, -37.743851, -243.667923, 55.051),
    18564: (-56.559654, -241.223923, 6.016711, -181.305771, 86.637),
    18584: (6.016711, -181.305771, -56.559654, -241.223923, 86.637),
    18596: (-58.250900, -189.020004, 12.120926, -243.439407, 88.959),
    18617: (12.120926, -243.439407, -58.250900, -189.020004, 88.959),
}


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def fill_body():
    return body(COMMANDER, "static void FillMobFacingAndCast(Unit* unit,")


def shape_rows():
    data = SHAPES[SHAPES.index('R"('):]
    rows = {}
    for line in data.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith(')"'):
            continue
        fields = [f.strip() for f in line.split(",")]
        if len(fields) < 10 or not fields[0].isdigit():
            continue
        rows[int(fields[0])] = fields
    return rows


class DeepBreathSnapshot(unittest.TestCase):
    def test_the_cast_slots_are_read_before_the_stand_window(self):
        fn = fill_body()
        generic = fn.index("CURRENT_GENERIC_SPELL")
        channeled = fn.index("CURRENT_CHANNELED_SPELL")
        read = fn.index("unit->GetCurrentSpell(type)")
        window = fn.index("ai::ActiveStandWindowSpell(unit)")
        self.assertLess(generic, channeled, "the generic cast slot is read first")
        self.assertLess(channeled, read)
        self.assertLess(read, window, "a real cast must win over the stand window")
        # The helper is asked only after the loop that returns on a real cast.
        self.assertLess(fn.index("return;\n    }\n    uint32 const windowSpell"), window)

    def test_the_reported_breath_is_a_full_cast(self):
        fn = fill_body()
        self.assertIn("mob.cast = SpellNameOf(windowSpell);", fn)
        self.assertIn("mob.castId = windowSpell;", fn)
        self.assertIn("s_standWindowMarks.CastLeft(ownerRaw, windowKey, WorldTimer::getMSTime(), totalMs)", fn)
        self.assertIn("mob.castTotalMs = totalMs;", fn)
        self.assertIn("mob.channel = false;", fn)
        self.assertIn("mob.castTarget = 0;", fn)
        self.assertIn("TortoiseBots::SpellShape::For(mob.castId)", fn)
        # A closed window forgets its mark; a real cast does too.
        self.assertGreaterEqual(fn.count("s_standWindowMarks.Forget(windowKey);"), 2)

    def test_the_total_comes_from_the_casting_time_index(self):
        fn = body(COMMANDER, "static uint32 SpellCastTimeMs(")
        self.assertIn("entry->CastingTimeIndex", fn)
        self.assertIn("sSpellCastTimesStore.LookupEntry(entry->CastingTimeIndex)", fn)
        self.assertIn("times->CastTime > times->MinCastTime ? times->CastTime : times->MinCastTime", fn)

    def test_the_countdown_is_keyed_by_instance_not_the_raw_guid(self):
        key = body(COMMANDER, "static StandWindow::Key StandWindowKeyOf(Unit* unit)")
        self.assertIn("unit->GetMapId()", key)
        self.assertIn("unit->GetMap()->GetInstanceId()", key)
        self.assertIn("unit->GetObjectGuid().GetRawValue()", key)
        self.assertIn('#include "../runtime/StandWindowMarks.h"', COMMANDER)
        # The state is the tested header, keyed by the unit's own map and instance (never a bare guid), with the binding
        # that reported each mark so a teardown can forget its own.
        self.assertIn("static StandWindow::OwnedMarks s_standWindowMarks;", COMMANDER)
        self.assertIn("s_standWindowMarks.CastLeft(ownerRaw, windowKey,", COMMANDER)
        self.assertNotIn("s_standWindowStart", COMMANDER)
        self.assertIn("constexpr uint32_t kStaleMs = 30000;", MARKS)
        self.assertIn("std::unordered_map<Key, Mark, KeyHash> marks_;", MARKS)
        self.assertIn("std::unordered_map<uint64_t, std::unordered_set<Key, KeyHash>> owners_;", MARKS)
        self.assertIn("void ForgetOwner(uint64_t owner)", MARKS)

    def test_a_window_that_never_closes_is_pruned(self):
        # Both snapshot cycles that can report a breath prune, and a binding teardown forgets what it reported even
        # when the mark is fresh (the prune only drops marks older than 30 s).
        collect = body(COMMANDER, "void CollectCommanderTelemetry(")
        self.assertIn("s_standWindowMarks.Prune(WorldTimer::getMSTime());", collect)
        self.assertIn("s_standWindowMarks.Prune(WorldTimer::getMSTime());",
                      body(COMMANDER, "void CollectDungeonMobs("))
        self.assertIn("s_standWindowMarks.ForgetOwner(commanderRaw);",
                      body(COMMANDER, "uint32 ReleaseBinding("))
        # Nothing bound: no mark can be needed.
        nothing = collect[collect.index("if (CommanderBindings().Empty())"):].split("return;")[0]
        self.assertIn("s_standWindowMarks.Clear();", nothing)

    def test_the_helper_and_the_stand_sets_share_one_rule(self):
        self.assertIn("std::uint32_t ActiveStandWindowSpell(Unit* boss);", STAND_H)
        helper = body(ACTIONS, "std::uint32_t ai::ActiveStandWindowSpell(")
        self.assertIn("GetBossStands().Rows(boss->GetMapId(), boss->GetEntry())", helper)
        self.assertIn("StoppedStandWindowSpell(*rows, boss->GetPositionX(), boss->GetPositionY(),", helper)
        self.assertIn("boss->HasAura(aura)", helper)
        self.assertIn("boss->IsStopped()", helper)
        self.assertIn("!boss->GetTargetGuid().IsEmpty()", helper)
        # The pure lister goes through the same row rule the seats do.
        lister = body(POLICY, "inline std::uint32_t StoppedStandWindowSpell(", end="\n    }\n")
        self.assertIn("StandStoppedNoTarget(stopped, hasTarget)", lister)
        self.assertIn("StandStoppedWindowOpen(row, bossX, bossY, row.aura != 0 && hasAura(row.aura), true)", lister)
        active = body(POLICY, "inline bool StandRowActive(", end="\n    }\n")
        self.assertIn("StandWindowOpen(row, castingSpell, stoppedNoTarget)", active)
        # FindBossStand keeps its own gate and its hold sequence (no behaviour change).
        find = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("const bool stoppedNoTarget = unit->IsStopped() && unit->GetTargetGuid().IsEmpty();", find)
        self.assertIn("hold, nowMs, stoppedNoTarget);", find)

    def test_the_shape_table_has_every_breath_chain(self):
        rows = shape_rows()
        for spell in BREATHS:
            self.assertIn(spell, rows, "breath %d has a shape row" % spell)
            spell_id, kind, angle, reach, width, x0, y0, x1, y1 = rows[spell][:9]
            source = ",".join(rows[spell][9:])
            self.assertEqual(kind, "line", "breath %d is a line" % spell)
            self.assertEqual(int(angle), 0, "a line has no arc")
            self.assertEqual(float(reach), 0.0, "a lane's length comes from its ends, not the reach column")
            self.assertEqual(float(width), 60.0, "the lane is 60 yd wide (30 yd either side)")
            want = CHAIN_ENDS[spell]
            for got, expected, what in (
                    (x0, want[0], "first x"), (y0, want[1], "first y"),
                    (x1, want[2], "last x"), (y1, want[3], "last y")):
                self.assertAlmostEqual(float(got), expected, delta=0.000001,
                    msg="breath %d %s is the chain's point (%s, got %s)" % (spell, what, expected, got))
            length = math.hypot(float(x1) - float(x0), float(y1) - float(y0))
            self.assertAlmostEqual(length, want[4], delta=0.01,
                msg="breath %d spans its whole chain (%s yd, got %.3f)" % (spell, want[4], length))
            self.assertIn("spell_target_position", source,
                "breath %d says its numbers are the core's chain" % spell)
            self.assertNotIn("ai_boss_stand", source,
                "breath %d is not sized by the stand anchors" % spell)

    def test_no_boss_name_or_entry_in_the_generic_code(self):
        for text in (POLICY, STAND_H):
            self.assertNotIn("Onyxia", text)
            self.assertNotIn("10184", text)


if __name__ == "__main__":
    unittest.main()
