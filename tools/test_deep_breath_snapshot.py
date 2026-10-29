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
   window opened (a per-guid start, dropped when it closes), no channel and no target;
 - the committed shape table has all 8 breath ids as a `line`, 60 yd wide, as long as the anchor-to-release-anchor
   lane of the Onyxia stand rows.

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
MIGRATION = ROOT / "data/sql/world/20261029170000_world.sql"

BREATHS = (17086, 18351, 18564, 18576, 18584, 18596, 18609, 18617)


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
        if len(fields) < 6 or not fields[0].isdigit():
            continue
        rows[int(fields[0])] = fields
    return rows


def stand_lanes():
    """anchor id -> (x, y) and anchor -> release anchor, from the Onyxia stand rows."""
    anchors, release = {}, {}
    row = re.compile(r"^\(249, 10184, (\d+), (-?[\d.]+), (-?[\d.]+), ([\d.]+), '(\w+)', (\d+), (\d+), "
                     r"(\d+), (\d+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (\d+), "
                     r"(\d+), (\d+), '(\w+)',")
    for line in MIGRATION.read_text().splitlines():
        match = row.match(line)
        if not match:
            continue
        anchor = int(match.group(1))
        anchors[anchor] = (float(match.group(2)), float(match.group(3)))
        release[anchor] = int(match.group(17))
    return anchors, release


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
        self.assertIn("mob.castLeftMs = StandWindowCastLeft(guid, WorldTimer::getMSTime(), totalMs);", fn)
        self.assertIn("mob.castTotalMs = totalMs;", fn)
        self.assertIn("mob.channel = false;", fn)
        self.assertIn("mob.castTarget = 0;", fn)
        self.assertIn("CommanderTelemetry::SpellShape::For(mob.castId)", fn)
        # A closed window forgets its start; a real cast does too.
        self.assertGreaterEqual(fn.count("s_standWindowStart.erase(guid);"), 2)

    def test_the_total_comes_from_the_casting_time_index(self):
        fn = body(COMMANDER, "static uint32 SpellCastTimeMs(")
        self.assertIn("entry->CastingTimeIndex", fn)
        self.assertIn("sSpellCastTimesStore.LookupEntry(entry->CastingTimeIndex)", fn)
        self.assertIn("times->CastTime > times->MinCastTime ? times->CastTime : times->MinCastTime", fn)

    def test_the_countdown_starts_when_the_window_opened(self):
        fn = body(COMMANDER, "static uint32 StandWindowCastLeft(")
        self.assertIn("s_standWindowStart.emplace(guid, nowMs)", fn)
        self.assertIn("const uint32 elapsed = nowMs - it->second;", fn)
        self.assertIn("return elapsed < totalMs ? totalMs - elapsed : 0;", fn)

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

    def test_the_shape_table_has_every_breath_lane(self):
        rows = shape_rows()
        anchors, release = stand_lanes()
        self.assertEqual(len(anchors), 8, "the Onyxia stand rows hold the 8 anchors")
        for spell in BREATHS:
            self.assertIn(spell, rows, "breath %d has a shape row" % spell)
            spell_id, kind, angle, reach, width, source = rows[spell][:6]
            self.assertEqual(kind, "line", "breath %d is a line" % spell)
            self.assertEqual(int(angle), 0, "a line has no arc")
            self.assertEqual(float(width), 60.0, "the lane is 60 yd wide (30 yd either side)")
            self.assertGreaterEqual(float(reach), 60.0)
            self.assertLessEqual(float(reach), 95.0)
            self.assertTrue(source, "breath %d says where its numbers came from" % spell)
        # Every length is the anchor -> release-anchor lane of the stand rows (the hover point to the opposite one).
        lengths = {}
        for anchor, (x, y) in anchors.items():
            other, other_xy = release[anchor], anchors[release[anchor]]
            lengths.setdefault((anchor, other), math.hypot(x - other_xy[0], y - other_xy[1]))
        by_lane = {(a, b): round(d, 1) for (a, b), d in lengths.items()}
        # The five lane pairs of the migration (the row order is the core's point order).
        expected = {
            17086: by_lane[(8, 4)],
            18351: by_lane[(4, 8)],
            18576: by_lane[(2, 6)],
            18609: by_lane[(6, 2)],
            18564: by_lane[(3, 7)],
            18584: by_lane[(7, 3)],
            18596: by_lane[(5, 1)],
            18617: by_lane[(1, 5)],
        }
        for spell, length in expected.items():
            self.assertAlmostEqual(float(shape_rows()[spell][3]), length, delta=0.15,
                msg="breath %d spans its hover-point lane (%s yd)" % (spell, length))

    def test_no_boss_name_or_entry_in_the_generic_code(self):
        for text in (POLICY, STAND_H):
            self.assertNotIn("Onyxia", text)
            self.assertNotIn("10184", text)


if __name__ == "__main__":
    unittest.main()
