"""The island escape's glue (Zul'Farrak r3-19z20, DungeonRunPolicy.h IslandEscape).

The tank stood 13 minutes on a 3 x 3 yd navmesh island while the walk back
passed every point. The pure rules (the island reading and its second sign,
the ring spot, the straight line's check) are tested in
tools/test_dungeon_run_policy.cpp; this pins where commands/Commander.cpp
calls them, so the glue cannot drift away silently:
- the run's "escape ..." order never goes out as a `cmd`: IssueRunOrder hands
  it to RunIslandEscape, whose sscanf formats match the order the policy writes;
- RunIslandEscape refuses with "wait: ..." (not a try) when the lead bot moved
  since the read or a party bot is in combat, the combat check right before the
  first change to the lead bot;
- every spot passes the straight line's check (floor, liquid, hostiles:
  CheckEscapeSegment) before the lead bot is put on stay, reset or moved;
- the island's facts come from the approach hop's own path, and the second
  sign (a) is read only after a reading said island.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def at(text, needle):
    i = text.find(needle)
    if i < 0:
        raise AssertionError("missing: " + needle)
    return i


class IslandEscapeWiringTest(unittest.TestCase):
    def setUp(self):
        self.escape = body(COMMANDS, "bool RunIslandEscape(Player* commander, std::string const& order, std::string& answer)\n{",
                           "\n}\n")

    def test_issue_run_order_hands_the_escape_to_the_glue(self):
        f = body(COMMANDS, "bool IssueRunOrder(", "\n}\n")
        route = at(f, 'if (text.compare(0, 7, "escape ") == 0)\n        return RunIslandEscape(commander, text, answer);')
        cmd = at(f, "HandleCommanderOrder(handler.get(), text.c_str());")
        self.assertLess(route, cmd)

    def test_order_text_matches_the_policy(self):
        f = self.escape
        at(f, '"escape from %f %f %f to %f %f %f toward %f %f %f"')
        at(f, '"escape from %f %f %f toward %f %f %f"')
        p = body(POLICY, "inline void IslandEscape(", "\n}\n")
        at(p, 'order = "escape from " + Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ) + " " +')
        at(p, '(good ? "to " + Num(r.goodLeadX)')
        at(p, '"toward " + Num(tx) + " " + Num(ty) + " " + Num(tz);')

    def test_checks_come_before_the_first_change_to_the_lead_bot(self):
        f = self.escape
        moved = at(f, "if (ai::PlanDistance(lx, ly, lz, fx, fy, fz) > kEscapeMovedYd)")
        good = at(f, "bool found = good && GoodSpotFits(lx, ly, lz, gx, gy, gz) && fits(gx, gy, ground) && lineFits(gx, gy, ground, false);")
        ring = at(f, "if (!lineFits(px, py, pz, true))")
        none = at(f, 'answer = "no-spot: no spot within "')
        fight = at(f, "bool fight = lead->IsInCombat();")
        supersede = at(f, "SupersedeCommanderTasks(commander->GetObjectGuid().GetRawValue(), 0, RemoteCommander::LaterOrder::Move);")
        drop = at(f, "leadAi->DropQueuedMovementOrders();")
        reset = at(f, "leadAi->Reset();")
        stay = at(f, 'leadAi->SetMovementStrategy("stay");')
        anchor = at(f, "SetStayAnchor(leadAi, x, y, ground, lead->GetMapId());")
        clear = at(f, "lead->GetMotionMaster()->Clear();")
        move = at(f, "lead->GetMotionMaster()->MovePoint(0, x, y, ground, MOVE_RUN_MODE);")
        self.assertTrue(moved < good < ring < none < fight < supersede < drop < reset < stay < anchor < clear < move)
        # Nothing touches the lead bot's movement before the checks.
        self.assertEqual(f.count("GetMotionMaster()"), 2)
        self.assertEqual(f.count("leadAi->Reset()"), 1)

    def test_wait_refusals(self):
        f = self.escape
        self.assertRegex(f, r"> kEscapeMovedYd\)\s*\{\s*answer = \"wait: ")
        self.assertRegex(f, r"for \(Player\* bot : CommandedBots\(group, lead\)\)\s*fight = fight \|\| \(bot && bot->IsInWorld\(\) && bot->IsAlive\(\) && bot->IsInCombat\(\)\);")
        self.assertRegex(f, r"if \(fight\)\s*\{\s*answer = \"wait: ")
        p = body(POLICY, "inline void OrderRefused(", "\n}\n")
        at(p, 'bool const wait = answer.compare(0, 4, "wait") == 0;')

    def test_the_line_check_reads_floor_liquid_and_hostiles(self):
        f = body(self.escape, "auto lineFits = ", "\n    };\n")
        samples = at(f, "EscapeSegmentSamples(lx, ly, lz, px, py, pz)")
        height = at(f, "GetHeightCompat(map, sample.x, sample.y, sample.z + kSegmentGroundYd, true,")
        liquid = at(f, "getLiquidStatus(sample.x, sample.y, floor + 0.5f, MAP_ALL_LIQUIDS,")
        at(f, "!= LIQUID_MAP_NO_WATER;")
        hostiles = at(f, "HostileCreaturesAround(lead, GroundDistance(lx, ly, px, py) + kEscapeHostileYd, hostiles);")
        verdict = at(f, "return CheckEscapeSegment(lz, samples, hostileNear, dropOk) == SegmentVerdict::Ok;")
        self.assertTrue(samples < height < liquid < hostiles < verdict)

    def test_ring_spots_lead_on(self):
        leads = at(self.escape, "if (!RingSpotLeadsOn((probeType & PATHFIND_NOPATH) != 0, (probeType & PATHFIND_INCOMPLETE) != 0,")
        # The larger step down (kSegmentDropYd) only for a ring spot past RingSpotLeadsOn; never for the last good spot.
        self.assertLess(leads, at(self.escape, "if (!lineFits(px, py, pz, true))"))
        self.assertEqual(self.escape.count("lineFits(px, py, pz, true)"), 1)
        self.assertEqual(self.escape.count("lineFits("), 2)

    def test_island_facts(self):
        f = body(COMMANDS, "bool ApproachOnPath(", "\n}\n")
        note = at(f, "NoteLeadWay(path, lead, x, y, z, *way);")
        nopath = at(f, "if (PathTypeCompat(path) & PATHFIND_NOPATH)")
        self.assertLess(note, nopath)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        at(facts, "if (f.leadIsland && lead->IsAlive())\n        f.goodSpotCut = GoodSpotCutOff(lead, slot.run);")

    def test_a_move_point_in_sight_is_read_forward_too(self):
        # r6-19z24: the forward run's step 64/65 points were in sight (6-9 yd)
        # and nothing read the lead bot's path to them; the reading is not tied
        # to a walk back any more, and MoveStep asks IslandConfirmed before its walk.
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        read = at(facts, "else if (step->kind == ai::PlanStepKind::Move && slot.run.phase != Phase::Walk &&\n"
                         "                !slot.run.regroup && lead->IsAlive() && lead->GetMapId() == slot.run.plan->map && toPoint > kArriveYd &&\n"
                         "                (slot.run.walkBack ||\n"
                         "                    TortoiseBots::DungeonRun::ForwardMoveProbeWanted(slot.run, true, f.seen, f.anyInCombat, toPoint)))")
        hit = at(facts, "if (forward && TortoiseBots::DungeonRun::MoveProbeHit(slot.moveProbe, slot.run.step, lead->GetPositionX(),\n"
                        "                        lead->GetPositionY(), step->x, step->y, step->z, slot.run.runMs))")
        cached = at(facts, "f.leadIsland = slot.moveProbe.island;\n                    f.leadPathGood = slot.moveProbe.good;")
        note = at(facts, "NoteLeadWay(path, lead, step->x, step->y, step->z, f);")
        store = at(facts, "if (forward)\n                        TortoiseBots::DungeonRun::StoreMoveProbe(slot.moveProbe, slot.run.step, lead->GetPositionX(),\n"
                          "                            lead->GetPositionY(), step->x, step->y, step->z, slot.run.runMs, f.leadIsland, f.leadPathGood);")
        self.assertTrue(read < hit < cached < note < store)
        # The combat fact the gate reads is set before the step's facts.
        self.assertLess(at(facts, "f.anyInCombat = true;"), read)
        at(COMMANDS, "mutable TortoiseBots::DungeonRun::MoveProbeCache moveProbe;")
        self.assertNotIn("slot.run.walkBack && step->kind == ai::PlanStepKind::Move && slot.run.phase", facts)
        move = body(POLICY, "inline void MoveStep(", "\n}\n")
        island = at(move, "IslandConfirmed(r, f, s.x, s.y, s.z, GoodSpotFresh(r))")
        self.assertLess(island, at(move, "WalkTo(r, f, s.x, s.y, s.z, s.kind == PlanStepKind::MoveDirect, dt, order)"))
        self.assertLess(island, at(move, "if (r.detour)"))
        at(move, "IslandConfirmed(r, f, s.x, s.y, s.z, GoodSpotFresh(r)))")
        at(move, "r.escapeFromMove = r.escapeOn;")
        refused = body(POLICY, "inline void OrderRefused(", "\n}\n")
        at(refused, "if (!wait && ((r.walkBack && r.walkBackIdx < r.step) || fromMove))")

    def test_the_detour_candidate_leads_on(self):
        # r6-19z24: NearestReachablePoint took ring points whose partial paths
        # ended on the lead bot's island; every candidate goes through the pure rule.
        f = body(COMMANDS, "bool NearestReachablePoint(", "\n}\n")
        check = at(f, "if (!TortoiseBots::DungeonRun::DetourPointReachable((type & PATHFIND_NOPATH) != 0,\n"
                      "                (type & PATHFIND_INCOMPLETE) != 0, ai::PlanDistance(end.x, end.y, end.z, px, py, ground),\n"
                      "                DistanceTo(lead, end.x, end.y, end.z), DistanceTo(lead, px, py, ground), PathLengthCompat(path)))\n            return false;")
        self.assertLess(check, at(f, "ox = px;"))
        self.assertNotIn("> 5.0f)", f)
        # r13-19z30 Uldaman: the candidate's own probe toward the plan's point leads on too (a closed pit).
        probe = at(f, "PathCalculateCompat(probe, Vector3(px, py, ground), Vector3(x, y, z), false);")
        leads = at(f, "if (!TortoiseBots::DungeonRun::DetourCandidateLeadsOn((probeType & PATHFIND_NOPATH) != 0,")
        self.assertTrue(check < probe < leads < at(f, "ox = px;"))
        at(f, "DistanceTo(lead, probeEnd.x, probeEnd.y, probeEnd.z), ai::PlanDistance(px, py, ground, x, y, z),\n"
              "                ai::PlanDistance(probeEnd.x, probeEnd.y, probeEnd.z, x, y, z), ground - z))\n            return false;")
        q = body(POLICY, "inline bool DetourCandidateLeadsOn(", "\n}\n")
        # r22-19z40 review: a candidate close to the point is asked too (complete, reaching, <= 4 yd up or down).
        at(q, "if (candidateFromPointYd <= kArriveYd)\n        return !probeNoPath && !probeIncomplete && probeEndFromPointYd <= kArriveYd &&")
        at(q, "RingSpotLeadsOn(probeNoPath, probeIncomplete, probeEndFromCandidateYd, probeEndFromLeadYd)")
        p = body(POLICY, "inline bool DetourPointReachable(", "\n}\n")
        at(p, "RingSpotLeadsOn(pathNoPath, pathIncomplete, endFromLeadYd, endFromLeadYd)")
        # r9-19z26 step 124: a ring point 1.3 yd from the lead bot on its own island was taken.
        at(p, "return pointFromLeadYd > kArriveYd && endFromPointYd <= kDetourEndYd &&")

    def test_a_no_path_reading_is_an_island_reading(self):
        # r9-19z26 Uldaman: from the ledge every path came back NOPATH (a partial
        # one inside the smooth path's slop); the reading must hand the NOPATH bit
        # to LeadOnIsland, not only the INCOMPLETE one.
        f = body(COMMANDS, "void NoteLeadWay(", "\n}\n")
        nopath = at(f, "bool const noPath = (type & PATHFIND_NOPATH) != 0;")
        island = at(f, "f.leadIsland = TortoiseBots::DungeonRun::LeadOnIsland(noPath, incomplete, endFromLead, DistanceTo(lead, x, y, z));")
        good = at(f, "f.leadPathGood = TortoiseBots::DungeonRun::LeadPathGood(noPath, incomplete, endFromLead);")
        self.assertTrue(nopath < island < good)
        p = body(POLICY, "inline bool LeadOnIsland(", "\n}\n")
        self.assertLess(at(p, "if (pathNoPath)\n        return toPointYd > kArriveYd;"), at(p, "return pathIncomplete &&"))

    def test_a_refused_step_move_without_a_detour_hops_off_first(self):
        # Uldaman r16-19z34 step 39: the step's own move was refused unreachable from
        # a ramp's dead-end tip, no ring point passed, and the run paused although the
        # escape had not run. The glue asks RefusedFromIsland after the detour search
        # and before OrderRefused (the pause), and only for an own-move path refusal.
        # Since r22-19z40 the order lives in DungeonRun::MoveRefusedUnreachable, which the glue asks
        # before OrderRefused (test_run_leader_wiring.py pins the glue's side).
        f = body(COMMANDS, "        if (!accepted && !stallDirectRefused && !legTaken)\n        {", "\n        }\n")
        detour = at(f, "NearestReachablePoint(lead, step->x, step->y, step->z, TortoiseBots::DungeonRun::MoveFallbackYd(*step),")
        handled = at(f, "TortoiseBots::DungeonRun::MoveRefusedUnreachable(it->second.run, order, answer, found, dx, dy, dz)))")
        refused = at(f, "TortoiseBots::DungeonRun::OrderRefused(it->second.run, order, answer +")
        self.assertTrue(detour < handled < refused)
        self.assertEqual(f.count("OrderRefused("), 1)
        m = body(POLICY, "inline bool MoveRefusedUnreachable(", "\n}\n")
        use = at(m, "if (!again && found && UseDetour(r, dx, dy, dz))")
        island = at(m, "if (!again && RefusedFromIsland(r, order, answer))")
        self.assertTrue(use < island < at(m, "NextStep(r,"))
        p = body(POLICY, "inline bool PathRefusalIslandSign(", "\n}\n")
        at(p, "LeadNoProgressRefusal(answer)")
        # The source-side evidence: CommanderMove marks the no-progress refusal (RemoteCommander::NoProgressFromLead).
        move = body(COMMANDS, "static bool CommanderMove(", "\n}\n")
        nopath = at(move, "else if (verdict == RemoteCommander::MoveVerdict::NoPath)")
        mark = at(move, "if (RemoteCommander::NoProgressFromLead(facts))\n                message += std::string(\" \") + TortoiseBots::DungeonRun::kLeadNoProgressMark + \".\";")
        self.assertLess(nopath, mark)
        self.assertLess(mark, at(move, "SendActionError(handler, intent, RemoteCommander::MoveVerdictCode(verdict), message.c_str());"))
        at(p, "r.islandEscapes < kIslandEscapes && r.stepEscapes < kStepEscapes && r.islandRefusals < kStepEscapes;")
        refusal = body(POLICY, "inline void OrderRefused(", "\n}\n")
        guard = at(refusal, "if (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false))")
        self.assertLess(guard, at(refusal, "if (PathRefusalIslandSign(r, answer))"))


if __name__ == "__main__":
    unittest.main()
