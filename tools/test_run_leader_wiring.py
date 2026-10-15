"""The run leader's glue hands the policy what its retry rules match on.

runtime/DungeonRunPolicy.h retries a refused gather once and passes a refused
walk-back point by comparing the refused order's text with the order it sent
that tick (issue #999), so the glue in commands/BotCommands.cpp must report a
refusal with the order exactly as Tick() returned it. A dead lead bot released
by the regroup's `revive party` is a ghost off the plan's map; the glue must
not pause the run for that while a regroup (a resume or one the run started by
itself after a wipe) is on. `cmd move` must judge the point with
RemoteCommander::CheckMove from the lead bot's own 3D distance, which is what
lets the run leader's gather on the lead bot's own spot through (and only that
gather, not a player's order). During a walk back the step facts (hop pulls)
are read for the replayed move point.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/BotCommands.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunLeaderWiringTest(unittest.TestCase):
    def test_refusal_reports_the_order_as_sent(self):
        self.assertIn("std::string const order = TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);", COMMANDS)
        self.assertIn("bool const accepted = IssueRunOrder(commander, order, answer,\n"
                      "            TortoiseBots::DungeonRun::IsGatherOrder(it->second.run, order));", COMMANDS)
        self.assertIn("TortoiseBots::DungeonRun::OrderRefused(it->second.run, order, answer", COMMANDS)

    def test_ghost_lead_tolerated_during_a_regroup(self):
        # Any dead lead bot off the map (a ghost after a wipe too) lets the tick run; it starts the regroup.
        self.assertIn("bool const deadLeadAway = !lead->IsAlive();", COMMANDS)
        self.assertIn("if (lead->GetMapId() != slot.run.plan->map && !deadLeadAway)", COMMANDS)

    def test_cmd_move_checks_the_lead_distance(self):
        self.assertIn("facts.distanceFromLead = std::sqrt(dx * dx + dy * dy + dz * dz);", COMMANDS)
        self.assertIn("RemoteCommander::MoveVerdict verdict = RemoteCommander::CheckMove(facts);", COMMANDS)

    def test_only_the_run_leaders_gather_gets_the_lead_spot_pass(self):
        # A player's `cmd move` never sets it: only IssueRunOrder, for the run's own gather.
        self.assertIn("facts.leadSpotGather = s_runIssuing && s_runGather;", COMMANDS)
        issue = body(COMMANDS, "bool IssueRunOrder(", "\n}\n")
        self.assertIn("s_runGather = gather;", issue)
        self.assertIn("s_runGather = false;", issue)
        self.assertEqual(COMMANDS.count("s_runGather = gather;"), 1)

    def test_gather_stragglers_are_reported_by_name(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("member.name = bot->GetName();", facts)
        self.assertIn("member.yd = yd;", facts)
        self.assertIn("member.dz = bot->GetMap() == lead->GetMap() ? std::fabs(bot->GetPositionZ() - lead->GetPositionZ()) : 0.0f;", facts)
        # During a walk back out of sight the hop pulls are read around the next approach hop.
        self.assertIn("cx = f.approachX;", facts)
        self.assertIn("f.members.push_back(member);", facts)

    def test_boss_down_needs_his_corpse(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("if (bossEntry && fromPoint <= step->radius + kSeeYd)\n"
                      "                            f.bossDeadNearSpot = true;", facts)
        self.assertIn("if (bossEntry && lead->GetDistance(creature) <= kSeeYd)\n"
                      "                            f.bossDeadNearLead = true;", facts)

    def test_gather_anchor_path_is_read(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        # Read lazily: only after the policy asked for it.
        self.assertIn("if (slot.run.anchorPathWanted && slot.run.anchorKnown && lead->IsAlive() && lead->IsInWorld() &&", facts)
        self.assertIn("f.anchorPathRead = true;", facts)
        self.assertIn("f.anchorReachable = !(type & RemoteCommander::kPathNoPath) &&", facts)

    def test_direct_walk_back_checks_the_path_against_packs(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.directPathCrossesPack = PathCrossesUnclearedPack(*slot.run.plan, slot.run.step, whole, f.team,\n"
                      "                            slot.run.notCleared);", facts)

    def test_crowd_control_and_kill_first_facts(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        # The crowd control is a mage that knows Polymorph (the policy picks it from the members).
        self.assertIn("member.polymorph = bot->GetClass() == CLASS_MAGE && KnowsPolymorph(bot);", facts)
        self.assertIn("unit.polymorphed = Polymorphed(creature);", facts)
        # Kill-first units come from the bots' cached attackers lists: no grid or world scan (AGENTS.md).
        self.assertIn("ReadKillFirst(*slot.run.plan, lead, bots, f);", facts)
        kill = body(COMMANDS, "void ReadKillFirst(", "\n}\n")
        self.assertIn('ReadCachedGuidList(PlayerbotAIStorage::Instance().GetAI(bot), "attackers", guids);', kill)
        for scan in ("GetCreatureListWithEntryInGrid", "Cell::Visit", "VisitAllObjects", "HostileCreaturesAround"):
            self.assertNotIn(scan, kill)
        self.assertIn("fact.engaged = EngagedWithParty(unit, party);", kill)
        # A ward is read off its engaged owner at once (totems and guardians), not when a cached list takes it in.
        self.assertIn("CONTROLLED_TOTEMS | CONTROLLED_GUARDIANS", kill)
        self.assertIn("EngagedWithParty(summoner, party);", kill)
        loader = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
        self.assertIn("SELECT `plan`, `map`, `cc_priority`, `cc_always`, `kill_first` FROM `ai_dungeon_plan`", loader)
        self.assertIn("table.SetUnits(plan, fields[2].GetCppString(), fields[3].GetCppString(), fields[4].GetCppString())", loader)

    def test_crowd_control_keeps_to_the_runs_mark(self):
        cc = (ROOT / "ai/playerbot/strategy/values/CcTargetValue.cpp").read_text()
        # Never a totem or an NPC's summon (the healing ward), and in a run only the ordered mark.
        self.assertIn("bool const totem = static_cast<Creature*>(creature)->IsTotem();", cc)
        self.assertIn("TortoiseBots::BotCommands::IsRunKillFirst(bot, creature->GetEntry());", cc)
        self.assertIn("if (TortoiseBots::CcLeavesOut(totem, npcSummoned, inRun, runKillFirst))", cc)
        self.assertIn("inRun = TortoiseBots::BotCommands::IsInRunningDungeonRun(ai->GetBot());", cc)
        self.assertIn("if (inRun && !assignedTarget)\n            return;", cc)
        query = body(COMMANDS, "RunningRunOf(Player* bot)", "\n}\n")
        self.assertIn("State::Running", query)

    def test_spells_at_a_totem_must_hit_it(self):
        spells = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
        useful = body(spells, "bool CastSpellAction::isUseful()", "\n}\n")
        self.assertIn("static_cast<Creature*>(spellTarget)->IsTotem()", useful)
        self.assertIn("TortoiseBots::SpellWorksOnTotem(info->Effect, info->EffectApplyAuraName,", useful)
        self.assertIn("info->EffectImplicitTargetA, MAX_EFFECT_INDEX, info->IsPositiveSpell()))", useful)

    def test_walk_back_reads_facts_for_the_replayed_point(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("ai::PlanStep const* step = FactStep(slot.run);", facts)
        self.assertIn("ai::OwnedByLaterStep(*slot.run.plan, OwnerFrom(slot.run), unit->GetEntry(),", facts)
        self.assertNotIn("Current(slot.run)", facts)


if __name__ == "__main__":
    unittest.main()
