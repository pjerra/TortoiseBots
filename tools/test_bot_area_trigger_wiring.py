"""Bots fire scripted area triggers (issue #984), wired the way the brief says.

ai/playerbot/BotAreaTriggerPolicy.h holds the pure parts (tested in
tools/test_bot_area_trigger_policy.cpp). This checks the glue: the per-bot
check sits behind a non-empty AiPlayerbot.BotAreaTriggers allowlist and its own
timer, the index is filtered to listed, scripted, non-teleport, non-tavern,
non-BG triggers on dungeon maps (not raids), only a commander/run party or one
with a human within 40 yd fires, only the script runs (sScriptMgr.OnAreaTrigger, never the opcode
handler), no grid visit happens, and the key's code default equals the
template.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PB = ROOT / "ai/playerbot"
AI = (PB / "PlayerbotAI.cpp").read_text()
MGR = (PB / "BotAreaTriggers.cpp").read_text()
CONF_CPP = (PB / "PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (PB / "aiplayerbot.conf.dist.in").read_text()
CMAKE = (ROOT / "TortoiseBots.cmake").read_text()
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))


def squash(text):
    return re.sub(r"\s+", " ", text).strip()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotAreaTriggerWiringTest(unittest.TestCase):
    def test_update_ai_gates_and_throttles_the_check(self):
        fn = body(AI, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)", "\n}\n")
        block = body(fn, "if (areaTriggerCheckDelay > elapsed)", "\n    }\n")
        # The run's question is asked once per throttled check, never per tick; a check runs only with
        # a listed trigger or one the bot's run has armed.
        throttled = block[block.index("areaTriggerCheckDelay = ai::kBotAreaTriggerCheckMs;"):]
        self.assertIn("std::vector<uint32_t> const* runArmed = TortoiseBots::BotCommands::RunArmedAreaTriggers(bot);", throttled)
        self.assertRegex(throttled, r"if \(!sPlayerbotAIConfig\.botAreaTriggerIds\.empty\(\) \|\| runArmed\)\s*"
                                    r"ai::UpdateBotAreaTriggers\(bot, areaTriggerState, runArmed\);")
        self.assertEqual(AI.count("UpdateBotAreaTriggers("), 1)
        self.assertEqual(AI.count("RunArmedAreaTriggers("), 1)

    def test_run_arms_its_move_triggers_until_fired(self):
        # Beyond the allowlist a bot fires only what its active run armed (DungeonRun::RunArmedTriggersOf:
        # move steps' triggers from the step until fired once), read through the O(1) run index.
        fn = body(COMMANDS, "std::vector<uint32_t> const* RunArmedAreaTriggers(Player* bot)", "\n}\n")
        self.assertEqual(squash(fn[fn.index("{"):]),
                         "{ return TortoiseBots::DungeonRun::RunArmedTriggersOf(RunningRunOf(bot, true));")
        fired = body(COMMANDS, "void NoteRunAreaTriggerFired(Player* bot, uint32_t id)", "\n}\n")
        self.assertIn("if (DungeonRunSlot* slot = RunSlotOf(bot, true))", fired)
        self.assertIn("TortoiseBots::DungeonRun::NoteTriggerFired(slot->run, id);", fired)
        query = (ROOT / "runtime/CommandedGroupQuery.h").read_text()
        self.assertIn("std::vector<uint32_t> const* RunArmedAreaTriggers(Player* bot);", query)
        self.assertIn("void NoteRunAreaTriggerFired(Player* bot, uint32_t id);", query)
        update = body(MGR, "void ai::UpdateBotAreaTriggers(", "\n}\n")
        loop = body(update, "for (BotAreaTriggerShape const& shape : itr->second)", "\n    }\n")
        self.assertLess(loop.index("if (!IsBotAreaTriggerArmed(shape.listed, shape.id, runArmed))"),
                        loop.index("IsInsideAreaTriggerShape("))
        # Codex round 2: the script runs first; a run-armed trigger is noted fired only when it ran, and a
        # refusal is retried (state.Refused, every kBotAreaTriggerRetryMs). The armed ids are a copy taken
        # before any script, never runArmed after one.
        fire = update[update.index("std::vector<uint32_t> const armedIds = runArmed ? *runArmed : std::vector<uint32_t>();"):]
        self.assertLess(fire.index("std::vector<uint32_t> const armedIds"), fire.index("state.Step("))
        self.assertNotIn("runArmed", fire[fire.index("state.Step("):])
        self.assertIn("bool const armedByRun = std::find(armedIds.begin(), armedIds.end(), id) != armedIds.end();", fire)
        self.assertRegex(squash(fire), r"bool const ran = ScriptMgrW3\(\)\.OnAreaTrigger\(bot, at\);.*if \(armedByRun\) \{ "
                         r"if \(ran\) TortoiseBots::BotCommands::NoteRunAreaTriggerFired\(bot, id\); else state\.Refused\(id\); \}")
        self.assertEqual(MGR.count("NoteRunAreaTriggerFired("), 1)
        self.assertEqual(MGR.count("ScriptMgrW3().OnAreaTrigger("), 1)
        self.assertIn("void ai::UpdateBotAreaTriggers(Player* bot, BotAreaTriggerState& state, std::vector<uint32_t> const* runArmed)", MGR)
        # The run arms in its own tick, before and after the step body.
        policy = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
        tick = body(policy, "inline std::string Tick(Run& r, const Facts& raw,", "\n}\n")
        self.assertEqual(tick.count("ArmReachedTriggers(r);"), 2)
        self.assertLess(tick.index("ArmReachedTriggers(r);"), tick.index("order = TickBody("))
        self.assertGreater(tick.rindex("ArmReachedTriggers(r);"), tick.rindex("order = TickBody("))

    def test_run_of_a_bot_is_one_index_lookup(self):
        # Codex round 1: no scan over every run per bot. RunSlotOf returns before any lookup without a
        # group, then asks RunGroups once; RebuildRunGroups is the one writer, called by the run update and
        # right after a run is stored.
        slot = body(COMMANDS, "static DungeonRunSlot* RunSlotOf(Player* bot, bool activeOnly)", "\n}\n")
        self.assertNotIn("for (", slot)
        self.assertLess(slot.index("if (!group)\n        return nullptr;"), slot.index("RunOfGroup(RunGroups(), groupId)"))
        self.assertEqual(slot.count("RunGroups()"), 1)
        self.assertIn("binding->groupId != groupId", slot)
        running = body(COMMANDS, "static TortoiseBots::DungeonRun::Run const* RunningRunOf(", "\n}\n")
        self.assertIn("RunSlotOf(bot, activeOnly)", running)
        self.assertNotIn("DungeonRuns()", running)
        rebuild = body(COMMANDS, "void RebuildRunGroups()", "\n}\n")
        self.assertIn("RunGroups().Clear();", rebuild)
        self.assertIn("RunGroups().Add(binding->groupId, entry.first);", rebuild)
        self.assertEqual(COMMANDS.count("RunGroups().Clear()") + COMMANDS.count("RunGroups().Add("), 2)
        update = body(COMMANDS, "static void UpdateDungeonRuns(uint32 diff)", "\n}\n")
        self.assertTrue(squash(update[update.index("{"):]).startswith("{ RebuildRunGroups();"))
        self.assertRegex(COMMANDS, r"DungeonRunSlot& stored = DungeonRuns\(\)\[commanderRaw\] = slot;\s*RebuildRunGroups\(\);")
        self.assertEqual(COMMANDS.count("RebuildRunGroups();"), 2)

    def test_index_filter_uses_every_exclusion(self):
        fn = body(MGR, "BotAreaTriggerIndex const& GetIndex()", "\n    }\n}\n")
        # Every scripted dungeon trigger is indexed with its listed flag; the per-check arming decides.
        self.assertIn("IsBotIndexableAreaTrigger(ScriptMgrW3().GetAreaTriggerScriptId(at.id) != 0,", fn)
        self.assertIn("shape.listed = IsListedAreaTrigger(sPlayerbotAIConfig.botAreaTriggerIds, at.id);", fn)
        self.assertIn("ScriptMgrW3().GetAreaTriggerScriptId(at.id) != 0", fn)
        self.assertIn("map && map->IsNonRaidDungeon()", fn)
        self.assertNotIn("map->IsDungeon()", fn)
        self.assertIn("sObjectMgr.GetAreaTriggerTeleport(at.id) != nullptr", fn)
        self.assertIn("sObjectMgr.IsTavernAreaTrigger(at.id)", fn)
        self.assertIn("sObjectMgr.GetBattlegroundEntranceTrigger(at.id) != nullptr", fn)
        self.assertIn("if (built)", fn)

    def test_only_the_script_runs_and_no_world_scan(self):
        self.assertIn("ScriptMgrW3().OnAreaTrigger(bot, at);", MGR)
        self.assertNotIn("HandleAreaTriggerOpcode", MGR)
        self.assertNotIn("CMSG_AREATRIGGER", MGR)
        self.assertNotRegex(MGR, r"Cell::|Visit\(|GridNotifiers|WorldDatabase|CharacterDatabase")
        fn = body(MGR, "void ai::UpdateBotAreaTriggers(", "\n}\n")
        for guard in ("bot->GetGroup()", "bot->IsAlive()", "bot->IsTaxiFlying()",
                      "bot->IsBeingTeleported()", "bot->GetMap()->IsNonRaidDungeon()"):
            self.assertIn(guard, fn)

    def test_party_gate(self):
        fn = body(MGR, "void ai::UpdateBotAreaTriggers(", "\n}\n")
        self.assertIn("TortoiseBots::BotCommands::IsInCommandedGroup(bot)", fn)
        self.assertIn("HasNetworkTransportCompat(member->GetSession())", fn)
        self.assertIn("IsWithinDist(bot, kBotAreaTriggerHumanRange)", fn)
        self.assertIn("return IsBotAreaTriggerPartyAllowed(commanded, humanNearby);", fn)
        self.assertIn("state.Step(bot->GetMapId(), bot->GetInstanceId(), insideNow, nearNow, partyAllowed)", fn)
        self.assertNotIn("state.Update(", fn)

    def test_config_default_is_off_everywhere(self):
        # New automation ships off (AGENTS.md): empty in code, template and doc.
        self.assertIn('LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.BotAreaTriggers", ""), botAreaTriggerIds);', CONF_CPP)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.BotAreaTriggers =$")
        self.assertIn("# Default: (empty)", TEMPLATE)

    def test_audited_ids_are_documented(self):
        # 962 is the opt-in example; every audited id says what its script does.
        audited = re.findall(r"(?m)^#   (\d+) - ", TEMPLATE)
        self.assertEqual(audited, ["962"])
        self.assertIn("Opt in with: AiPlayerbot.BotAreaTriggers = 962", TEMPLATE)

    def test_source_is_built(self):
        self.assertIn('"${TORTOISEBOTS_ROOT}/ai/playerbot/BotAreaTriggers.cpp"', CMAKE)


if __name__ == "__main__":
    unittest.main()
