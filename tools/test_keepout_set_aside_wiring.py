"""Every pick path that bypasses the attackers list asks the keep-out set-aside book.

AttackersValue::IgnoreTarget filters the attackers list, but the group tank's
victim and the raid marks are picked without it. Before review round 2 of the
creature aura keep-out, the tank's victim handed a set-aside mob straight back
(hold 3 s, set aside, re-pick, all fight). The loop itself is simulated in
tools/test_creature_aura_keepout_policy.cpp; this test pins the wiring.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALUES = ROOT / "ai/playerbot/strategy/values"


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class KeepoutSetAsideWiringTest(unittest.TestCase):
    def test_group_tank_target_refuses_set_aside(self):
        src = (VALUES / "DpsTargetValue.cpp").read_text()
        fn = body(src, "Unit* GetGroupTankTarget(", "\n    }\n}")
        self.assertIn("AttackersValue::IsKeepoutSetAside(target, bot)", fn)

    def test_rti_branches_refuse_set_aside(self):
        src = (VALUES / "DpsTargetValue.cpp").read_text()
        for start in ("Unit* DpsTargetValue::Calculate()", "Unit* DpsAoeTargetValue::Calculate()"):
            fn = body(src, start, "\n}\n")
            self.assertRegex(fn, r"if \(rti && !AttackersValue::IsKeepoutSetAside\(rti, bot\)\) return rti;", start)
        tank = body((VALUES / "TankTargetValue.cpp").read_text(), "Unit* TankTargetValue::Calculate()", "\n}\n")
        self.assertIn("AttackersValue::IsKeepoutSetAside(rti, bot)", tank)

    def test_focus_rti_and_ignore_target_ask_the_book(self):
        src = (VALUES / "AttackersValue.cpp").read_text()
        focus = body(src, '"focus rti targets"', "return result;")
        self.assertIn("!IsKeepoutSetAside(rtiTarget, bot)", focus)
        ignore = body(src, "bool AttackersValue::IgnoreTarget(", "\n}\n")
        self.assertIn("IsKeepoutSetAside(target, playerToCheckAgainst)", ignore)
        book = body(src, "bool AttackersValue::IsKeepoutSetAside(", "\n}\n")
        self.assertIn("CreatureAuraSetAsideHolds(", book)

    def test_lift_does_not_depend_on_the_bot_distance(self):
        # Owner case: the tank drags the mob out on the far side, or it runs
        # to a healer 20-30 yd back. A mob with no listed creature within the
        # ring is unringed at any distance, so the book must judge it by the
        # creature-to-mob distance only (no bot-distance guard).
        src = (VALUES / "AttackersValue.cpp").read_text()
        book = body(src, "bool AttackersValue::IsKeepoutSetAside(", "\n}\n")
        self.assertIn('"possible triggers"', book)
        self.assertIn("CreatureAuraSetAsideRinged(*row,", book)
        self.assertNotIn("kMaxAoeAvoidRadiusYd", book)
        self.assertNotIn("bot->GetPositionX()", book)
        self.assertNotIn("GetDistance", book)

    def test_set_aside_writes_the_book_and_an_order_erases_it(self):
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        hold = body(move, "bool MovementAction::HoldChaseOutsideKeepout(", "\n}\n")
        self.assertIn('"keepout set aside"', hold)
        self.assertNotIn('"unreachable targets"', hold)
        order = body((ROOT / "ai/playerbot/strategy/actions/AttackAction.cpp").read_text(),
                     "void CommitExplicitAttackTarget(", "\n}\n")
        self.assertRegex(order, r'"keepout set aside"\)->Get\(\)\.erase\(guid\)')

    def test_set_aside_leaves_a_trace(self):
        # Live run 2 (2026-10-13) could not prove the set-aside fired: its
        # only message was a debug-move whisper. One action-log row and one
        # detail log line per set-aside, never per tick.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        hold = body(move, "bool MovementAction::HoldChaseOutsideKeepout(", "\n}\n")
        self.assertIn('"KEEPOUT_SET_ASIDE"', hold)
        self.assertIn('[BOT CHASE] %s: set aside', hold)

    def test_gap_closers_and_end_points_ask_the_ring(self):
        # Charge / Intercept / Feral Charge are CastReachTargetSpellAction.
        reach = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
        cls = body(reach, "class CastReachTargetSpellAction", "\n    };\n")
        self.assertIn("GapCloserIntoKeepout()", cls)
        self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(", cls)
        warrior = (ROOT / "ai/playerbot/strategy/warrior/WarriorActions.h").read_text()
        for spell in ("CastChargeAction", "CastInterceptAction"):
            self.assertRegex(warrior, r"REACH_ACTION\(" + spell + r",")
        # Intercept on the enemy healer or the snare target is gated the same way.
        for cls_name in ("CastInterceptOnEnemyHealerAction", "CastInterceptOnSnareTargetAction"):
            self.assertNotRegex(warrior, r"_ACTION\(" + cls_name + r",")
            gated = body(warrior, "class " + cls_name, "\n    };\n")
            self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), GetTarget())", gated)
        self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), ", cls)
        # The shared veto: cached list first, then the bot's hazard snapshot
        # (Charge fires past the 15 yd list).
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        veto = body(values, "bool PossibleTriggersValue::GapCloserIntoKeepout(", "\n}\n")
        self.assertIn('"explicit attack target"', veto)
        self.assertIn("CreatureAuraGapCloserVetoed(", veto)
        self.assertLess(veto.index("KeepoutRingAt(ai, lx, ly)"), veto.index("SnapshotRingedAt(ai, lx, ly, target)"))
        self.assertNotIn("Cell::", veto)
        self.assertIn("class CastFeralChargeBearAction : public CastReachTargetSpellAction",
                      (ROOT / "ai/playerbot/strategy/druid/DruidActions.h").read_text())
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        for start in ("bool SetBehindTargetAction::Execute(", "bool RearFlankAction::Execute(",
                      "bool MoveOutOfCollisionAction::Execute("):
            self.assertIn("EndPointInCreatureAuraKeepout(", body(move, start, "\n}\n"), start)

    def test_gap_closer_lookup_waits_for_a_castable_spell(self):
        # Review #882 round 2: the engine asks isUseful() before the stance
        # prerequisite and isPossible(), every tick the trigger fires. The
        # landing checks sit behind the readiness gate; the grid visit lives
        # only in the per-bot hazard snapshot.
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        veto = body(values, "bool PossibleTriggersValue::GapCloserIntoKeepout(", "\n}\n")
        gate = veto.index("if (!GapCloserCouldFire(ai, spell, target))\n        return false;")
        self.assertLess(veto.index("if (isTank || ordered)"), gate)
        for later in ("KeepoutFor(target)", "KeepoutRingAt(ai, lx, ly)", "SnapshotRingedAt("):
            self.assertLess(gate, veto.index(later), later)
        # The gate mirrors isPossible's refusals and leaves the stance to the
        # prerequisite (gating on it would switch stance, then veto).
        ready = body(values, "bool PossibleTriggersValue::GapCloserCouldFire(", "\n}\n")
        for probe in ('"spell id", spell', "UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL", "sServerFacade.IsSpellReady(bot, spellId)",
                      "Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr)", "IsNonCombatSpell()",
                      "IsInCombat()", "ai->GetSpellRange(spell, &maxRange, &minRange)",
                      "CreatureAuraGapCloserCouldFire(r)"):
            self.assertIn(probe, ready)
        self.assertNotIn("GetErrorAtShapeshiftedCast", ready)
        self.assertNotIn("Cell::", ready)
        header = (VALUES / "PossibleTargetsValue.h").read_text()
        self.assertNotIn("LandingMemo", header)
        self.assertNotIn("never per tick", header)

    def test_end_points_and_landings_read_one_hazard_snapshot(self):
        # Reviews of #995: nothing per bot tick scans the world (AGENTS.md).
        # End points past the 15 yd list and gap-closer landings read a
        # snapshot shared per (map, instance, cell), refreshed by at most one
        # grid visit per window, counted on the BOTPERF line.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        end = body(move, "bool MovementAction::EndPointInCreatureAuraKeepout(", "\n}\n")
        self.assertIn("PossibleTriggersValue::EndPointRinged(ai, x, y, ", end)
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        ring = body(values, "bool PossibleTriggersValue::EndPointRinged(", "\n}\n")
        self.assertLess(ring.index("KeepoutRingAt(ai, x, y, extra)"), ring.index("SnapshotRingedAt(ai, x, y, extra)"))
        self.assertNotIn("Cell::", ring)
        ringed = body(values, "bool PossibleTriggersValue::SnapshotRingedAt(", "\n}\n")
        self.assertIn("HazardSnapshot(ai)", ringed)
        self.assertIn('"explicit attack target"', ringed)
        self.assertIn("CreatureAuraSnapshotCovers(", ringed)
        self.assertNotIn("Cell::", ringed)
        snap = body(values, "CreatureAuraHazardSnapshot const* PossibleTriggersValue::HazardSnapshot(", "\n}\n")
        # No visit on a map without a listed row; the key carries the
        # instance; the one visit sits inside the shared cache's refresh.
        self.assertLess(snap.index("if (maxHazard <= 0.0f)"), snap.index("CreatureAuraSnapshotCacheGet("))
        self.assertIn("CreatureAuraSnapshotKeyFor(mapId, bot->GetInstanceId(), ", snap)
        self.assertIn("CreatureAuraSnapshotCacheGet(KeepoutSnapshotCache(), key, ", snap)
        self.assertLess(snap.index("CreatureAuraSnapshotCacheGet("), snap.index("Cell::VisitAllObjects("))
        self.assertEqual(snap.count("Cell::VisitAllObjects("), 1)
        self.assertIn("Cell::VisitAllObjects(cx, cy, map, searcher, radius)", snap)
        self.assertIn("hazardSnapshotRefreshCount.fetch_add(1", snap)
        self.assertIn("KeepoutFor(unit)", snap)
        self.assertEqual(values.count("Cell::VisitAllObjects("), 3)  # two FindUnits + the snapshot
        self.assertNotIn("AllCreaturesOfEntryInRange", values)
        self.assertIn("static CreatureAuraSnapshotCache cache;", values)
        manager = (ROOT / "runtime/BotManager.cpp").read_text()
        self.assertIn("PossibleTriggersValue::hazardSnapshotRefreshCount.exchange(0", manager)
        self.assertIn("keepoutScans=%u", manager)


if __name__ == "__main__":
    unittest.main()
