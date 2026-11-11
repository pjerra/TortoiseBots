"""Wiring pins for the threat_free boss-tactic kind (Onyxia walk 2f).

The rule is pure (ai/playerbot/ThreatCapPolicy.h: ThreatFreeApplies) and tested in tools/test_threat_cap_policy.cpp;
the loader and the table in tools/test_boss_tactics_policy.cpp; the row shape in tools/test_boss_tactics_sql.py. These
pins prove the live code calls it: ThreatValue::Calculate lifts the cap after the group check and before the threat
percentages are read, the lift is keyed and logged, the cut is at the value only (no trigger or multiplier knows the
kind), and the key is read, templated, documented and shipped with its row.

Run: python3 -m unittest tools/test_threat_free_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
CAP_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
VALUES_CPP = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
THREAT_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
GENERIC_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ThreatFreeWiring(unittest.TestCase):
    def test_value_lifts_the_cap_after_the_group_check(self):
        calculate = body(VALUES_CPP, "uint8 ThreatValue::Calculate(Unit* target)")
        lift = "if (ThreatFreeLifted(ai, bot, target))"
        self.assertIn(lift, calculate)
        # Only a bot in a group is measured at all, so the lift sits after that check and before either threat value is
        # read (the `aoe` branch reads the raw manager, the other the `my threat` / `tank threat` values).
        self.assertLess(calculate.index("Group* group = bot->GetGroup();"), calculate.index(lift))
        self.assertLess(calculate.index(lift), calculate.index('AI_VALUE2(float, "my threat"'))
        self.assertLess(calculate.index(lift), calculate.index("ThreatPctOf("))
        # A non-tank reads 0 there: the multiplier, the high/medium threat triggers and the wand rows all read this
        # value, so the lift and the log are the only glue.
        self.assertIn("return 0;", calculate)
        self.assertIn("LogThreatFree(ai, bot, target, target->GetPositionZ() - bot->GetPositionZ());", calculate)

    def test_lift_helper_is_the_single_rule(self):
        # ThreatValue::Calculate and the cast-threat veto (ThreatMultiplier) ask one question, so the veto cannot hold a
        # cast the value uncapped. The descent check ends the lift on the landing flight, before the gap closes.
        helper = body(VALUES_CPP, "bool ai::ThreatFreeLifted(PlayerbotAI* ai, Player* bot, Unit* subject)")
        for needle in ("sPlayerbotAIConfig.dungeonRunThreatFree",
                       'ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)',
                       "PlayerbotAI::IsTank(bot)",
                       "GetBossTactics().ThreatFreeGap(subject->GetMapId(), subject->GetEntry())",
                       "subject->IsInCombat()", "g_threatFreeZ.Reset(key)",
                       "ThreatFreeApplies(true, isTank, subject->GetPositionZ(), bot->GetPositionZ(), gap, descending)"):
            self.assertIn(needle, helper)
        self.assertIn("g_threatFreeZMutex", VALUES_CPP)
        self.assertIn("ai::ThreatFreeZHistories g_threatFreeZ;", VALUES_CPP)
        # The fight history is keyed by (instance id, raw guid): instanced copies of one creature share the raw guid,
        # and two raids run the lair at once, so a guid-only key would share one ring between them.
        self.assertIn("const ai::ThreatFreeZKey key{static_cast<std::uint32_t>(subject->GetInstanceId()),", helper)
        self.assertIn("subject->GetObjectGuid().GetRawValue()};", helper)
        self.assertIn("return instanceId != other.instanceId ? instanceId < other.instanceId : guid < other.guid;",
                      CAP_POLICY)
        self.assertIn("class ThreatFreeZHistories", CAP_POLICY)
        self.assertIn("histories_.erase(key);", CAP_POLICY)

    def test_log_is_throttled_and_named(self):
        log = body(VALUES_CPP, "void LogThreatFree(PlayerbotAI* ai, Player* bot, Unit* subject, float gapYd)")
        self.assertIn('if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))', log)
        self.assertIn("kThreatFreeLogGapMs = 10000", VALUES_CPP)
        self.assertIn("g_threatFreeLogMutex", VALUES_CPP)
        self.assertIn('"subject=%u,gap=%.1f"', log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsThreatFree", info1, "");', log)

    def test_cut_is_at_the_value_only(self):
        # Every reader of the value agrees, so no trigger branches on the kind; the cast-threat veto asks the lift
        # question instead of reading the raw threat on a subject the value just uncapped.
        self.assertNotIn("threat_free", THREAT_STRATEGY)
        for text in (GENERIC_TRIGGERS,):
            self.assertNotIn("ThreatFree", text)
            self.assertNotIn("threat_free", text)
        self.assertIn("inline bool ThreatFreeApplies(bool keyOn, bool isTank, float subjectZ, float botZ, float gapYd, "
                      "bool descending)", CAP_POLICY)
        self.assertIn("return keyOn && !isTank && !descending && gapYd > 0.0f && subjectZ - botZ >= gapYd;", CAP_POLICY)

    def test_cast_threat_veto_asks_the_same_lift(self):
        # ThreatCapCountsCastThreat reads `tank threat` and `my threat` directly and would veto a single-target nuke the
        # threat value already uncapped on the airborne subject.
        start = THREAT_STRATEGY.index("if (sPlayerbotAIConfig.threatCapCountsCastThreat &&")
        end = THREAT_STRATEGY.index('if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE)', start)
        cast_block = THREAT_STRATEGY[start:end]
        self.assertIn("if (ThreatFreeLifted(ai, bot, castTarget))", cast_block)
        self.assertIn("return 1.0f;", cast_block)

    def test_kind_is_parsed_and_validated(self):
        self.assertIn('if (text == "threat_free")\n            return BossTacticKind::ThreatFree;', POLICY)
        self.assertIn("ThreatFree = 29,", POLICY)
        loader = body(POLICY, "            // threat_free: the subject")
        for needle in ("row.param < 3.0f", "row.param > 40.0f", "row.spellId != 0", "row.priority != 0",
                       "RowGateSet(row)", "row.mode != TacticMode::Default"):
            self.assertIn(needle, loader)
        # Read straight from the table at the threat value: a gate could never be answered, so the loader refuses one.
        self.assertIn("row.kind == BossTacticKind::ThreatFree || row.kind == BossTacticKind::AddCrew))", POLICY)

    def test_table_keeps_the_gap_by_subject(self):
        self.assertIn("std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, float>> threatFreeGaps_;",
                      POLICY)
        self.assertIn("threatFreeGaps_[row.map].emplace(row.targetEntry != 0 ? row.targetEntry : row.bossEntry, "
                      "row.param);", POLICY)
        accessor = body(POLICY, "        float ThreatFreeGap(std::uint32_t map, std::uint32_t entry) const")
        self.assertIn("if (!sealed_.load(std::memory_order_acquire) || threatFreeGaps_.empty())", accessor)
        self.assertIn("auto const byMap = threatFreeGaps_.find(map);", accessor)
        self.assertIn("return it == byMap->second.end() ? 0.0f : it->second;", accessor)

    def test_config_key_is_read_templated_and_documented(self):
        load = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys(")
        self.assertIn('dungeonRunThreatFree = config.GetBoolDefault("AiPlayerbot.DungeonRunThreatFree", false);',
                      load)
        self.assertIn("bool dungeonRunThreatFree = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunThreatFree = 0$")
        # Documented without "recommended on": the key is unmeasured until walk 2f.
        self.assertIn("AiPlayerbot.DungeonRunThreatFree", DOCS)
        self.assertIn("threat_free", BOSS_DOCS)
        self.assertIn("AiPlayerbot.DungeonRunThreatFree", BOSS_DOCS)
        self.assertIn("threat_free", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
