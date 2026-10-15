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


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotAreaTriggerWiringTest(unittest.TestCase):
    def test_update_ai_gates_and_throttles_the_check(self):
        fn = body(AI, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)", "\n}\n")
        block = body(fn, "if (!sPlayerbotAIConfig.botAreaTriggerIds.empty())", "\n    }\n")
        self.assertIn("areaTriggerCheckDelay = ai::kBotAreaTriggerCheckMs;", block)
        self.assertIn("ai::UpdateBotAreaTriggers(bot, areaTriggerState);", block)
        self.assertEqual(AI.count("UpdateBotAreaTriggers("), 1)

    def test_index_filter_uses_every_exclusion(self):
        fn = body(MGR, "BotAreaTriggerIndex const& GetIndex()", "\n    }\n}\n")
        self.assertIn("IsListedAreaTrigger(sPlayerbotAIConfig.botAreaTriggerIds, at.id)", fn)
        self.assertIn("sScriptMgr.GetAreaTriggerScriptId(at.id) != 0", fn)
        self.assertIn("map && map->IsNonRaidDungeon()", fn)
        self.assertNotIn("map->IsDungeon()", fn)
        self.assertIn("sObjectMgr.GetAreaTriggerTeleport(at.id) != nullptr", fn)
        self.assertIn("sObjectMgr.IsTavernAreaTrigger(at.id)", fn)
        self.assertIn("sObjectMgr.GetBattlegroundEntranceTrigger(at.id) != nullptr", fn)
        self.assertIn("if (built)", fn)

    def test_only_the_script_runs_and_no_world_scan(self):
        self.assertIn("sScriptMgr.OnAreaTrigger(bot, at);", MGR)
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
        self.assertIn("HasNetworkTransport()", fn)
        self.assertIn("IsWithinDist(bot, kBotAreaTriggerHumanRange)", fn)
        self.assertIn("return IsBotAreaTriggerPartyAllowed(commanded, humanNearby);", fn)
        self.assertIn("state.Step(bot->GetMapId(), bot->GetInstanceId(), insideNow, nearNow, partyAllowed)", fn)
        self.assertNotIn("state.Update(", fn)

    def test_config_default_is_off_everywhere(self):
        # New automation ships off (AGENTS.md): empty in code, template and doc.
        self.assertIn('LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.BotAreaTriggers", ""), botAreaTriggerIds);', CONF_CPP)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.BotAreaTriggers =$")
        self.assertIn("# Default: (empty)", TEMPLATE)
        doc = (ROOT / "docs/guides/configuration-tuning.md").read_text()
        self.assertIn("| `AiPlayerbot.BotAreaTriggers` | *(empty)* | `962` |", doc)

    def test_audited_ids_are_documented(self):
        # 962 is the opt-in example; every audited id says what its script does.
        doc = (ROOT / "docs/guides/configuration-tuning.md").read_text()
        audited = re.findall(r"(?m)^#   (\d+) - ", TEMPLATE)
        self.assertEqual(audited, ["962"])
        self.assertIn("Opt in with: AiPlayerbot.BotAreaTriggers = 962", TEMPLATE)
        self.assertIn("Opt in with `AiPlayerbot.BotAreaTriggers = 962`", doc)
        for tid in audited:
            self.assertIn("**%s**" % tid, doc)

    def test_source_is_built(self):
        self.assertIn('"${TORTOISEBOTS_ROOT}/ai/playerbot/BotAreaTriggers.cpp"', CMAKE)


if __name__ == "__main__":
    unittest.main()
