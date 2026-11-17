"""Wiring pins for the add_crew boss-tactic kind (Onyxia walk 2e).

The decision is pure (ai/playerbot/BossTacticsPolicy.h: AddCrewStateFor, AddCrewMember, AddCrewRank, the loader and the
table) and tested in tools/test_boss_tactics_policy.cpp; the Onyxia glue (OnyxiaWhelpDutyFor, ShouldReturnToOnyxia) is
in tools/test_onyxia_whelp_egg_policy.cpp and the row shape in tools/test_boss_tactics_sql.py. These pins prove the live
code calls the rules: the value reads the key first, the switch unit through the tank-post grid search, the sighting's
encounter through the add_crew row's boss (out of combat at full health or a new pull starts it over; the bot's own
combat drop does not), the crew's own place through the separate `add crew keeps duty` value (the raid's AoE ranged
bots through their own AIs, whatever the switch unit's life); the whelp duty and the return to the boss read the value
only under the key; the trigger, the action and the values are registered; and the key is read, templated, documented
without "recommended on", and shipped with its row.

Run: python3 -m unittest tools/test_add_crew_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
VALUES_H = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.h").read_text()
VALUES_CPP = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.cpp").read_text()
TRIGGERS_H = (ROOT / "ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
VALUE_CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()
WHELP_POLICY = (ROOT / "ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class AddCrewWiring(unittest.TestCase):
    def test_kind_is_parsed_and_validated(self):
        self.assertIn('if (text == "add_crew")\n            return BossTacticKind::AddCrew;', POLICY)
        self.assertIn("AddCrew = 28,", POLICY)
        loader = body(POLICY, "            // add_crew: the switch unit whose death")
        for needle in ("row.targetEntry == 0", "row.targetEntry == row.bossEntry", "row.spellId != 0",
                       "row.priority > 5", "row.param != 0.0f", "RowGateSet(row)",
                       "(row.mode != TacticMode::Default && row.mode != TacticMode::Early)"):
            self.assertIn(needle, loader)
        # Read straight from the table: a gate on it could never be answered, so the loader refuses one.
        self.assertIn("row.kind == BossTacticKind::TankIgnore ||\n                    row.kind == "
                      "BossTacticKind::ThreatFree || row.kind == BossTacticKind::AddCrew))", POLICY)

    def test_table_keeps_the_switch_unit_and_the_crew_size(self):
        self.assertIn("struct AddCrewRow", POLICY)
        self.assertIn("std::unordered_map<std::uint64_t, AddCrewRow> addCrew_;", POLICY)
        self.assertIn("addCrew_[Key(row.map, row.bossEntry)] = AddCrewRow{row.targetEntry, row.priority,",
                      POLICY)
        self.assertIn("row.mode == TacticMode::Early};", POLICY)
        self.assertIn("bool early = false;", POLICY)
        # The early mode's own readers: the row's flag, and the boss of the row a kill_first or hold_kill row names by
        # its switch unit.
        self.assertIn("bool AddCrewEarlyOf(std::uint32_t map, std::uint32_t bossEntry) const", POLICY)
        self.assertIn("bool AddCrewEarlyBossOf(std::uint32_t map, std::uint32_t switchEntry, std::uint32_t& bossEntry) "
                      "const", POLICY)
        self.assertIn("enum class AddCrewEarly : std::uint8_t { None = 0, Whelps = 1, Boss = 2, Switch = 3 };",
                      POLICY)
        self.assertIn("inline AddCrewEarly AddCrewEarlyDuty(bool keyOn, bool early, SwitchUnitState switchState, "
                      "bool airborne, bool tank,", POLICY)
        self.assertIn("inline AddCrewEarly AddCrewEarlyPlace(bool tank, bool healer, bool ranged, int aoeRank,", POLICY)
        # Walk 2o's split: how many ranks past the crew leave the switch unit for the boss while it lives.
        self.assertIn("inline bool AddCrewBossWhileSwitch(int aoeRank, std::uint8_t crewSize, std::uint32_t n)",
                      POLICY)
        self.assertIn("if (n >= 40)\n            return true;", POLICY)
        self.assertIn("return aoeRank >= crewSize && aoeRank < int(crewSize) + int(n);", POLICY)
        duty = POLICY[POLICY.index("inline AddCrewEarly AddCrewEarlyDuty(bool keyOn"):]
        self.assertIn("if (!keyOn || !early || !airborne || switchState == SwitchUnitState::NeverSeen)\n"
                      "            return AddCrewEarly::None;", duty)
        self.assertIn("if (switchState == SwitchUnitState::Dead)\n            return place;", duty)
        # Walk 2o round 1: a ranged damage dealer past the crew the split does not cover stays on the switch unit
        # (Switch), never on today's duty that would send it to the boss's adds.
        self.assertIn("if (place == AddCrewEarly::Boss)\n            return AddCrewBossWhileSwitch(aoeRank, crewSize, "
                      "bossWhileSwitch) ? place : AddCrewEarly::Switch;", duty)
        accessor = body(POLICY, "        bool AddCrewOf(std::uint32_t map, std::uint32_t bossEntry,")
        self.assertIn("if (!sealed_.load(std::memory_order_acquire) || addCrew_.empty())", accessor)
        self.assertIn("auto const it = addCrew_.find(Key(map, bossEntry));", accessor)
        self.assertIn("switchEntry = it->second.switchEntry;", accessor)
        self.assertIn("crewSize = it->second.crewSize;", accessor)
        self.assertIn("return true;", accessor)

    def test_value_reads_the_key_and_the_tank_post_search(self):
        self.assertIn("class AddCrewValue : public CalculatedValue<std::uint8_t>, public Qualified", VALUES_H)
        self.assertIn('CalculatedValue<uint8>(ai, "add crew", 2)', VALUES_H)
        value = body(VALUES_CPP, "uint8 AddCrewValue::Calculate()")
        # The key first: with it off no table or grid read runs.
        self.assertLess(value.index("if (!sPlayerbotAIConfig.dungeonRunAddCrew)"),
                        value.index("AddCrewOf("))
        self.assertIn("if (!GetBossTactics().AddCrewOf(bot->GetMapId(), QualifierEntry(qualifier), switchEntry, "
                      "crewSize))", value)
        # The switch unit's sighting is the separate value: the tank-post grid search remembers a unit seen alive, and
        # only a sighting that died opens the crew (an empty search before the unit appears reads NeverSeen).
        self.assertIn("class AddCrewSwitchValue : public CalculatedValue<std::uint8_t>, public Qualified", VALUES_H)
        self.assertIn('CalculatedValue<uint8>(ai, "add crew switch", 1)', VALUES_H)
        sighting = body(VALUES_CPP, "uint8 AddCrewSwitchValue::Calculate()")
        # The sighting lasts the boss's encounter, not the bot's: a map or instance change starts it over, never the
        # bot's own combat (a Feign Death, a death and a battle rez keep it), so the boss is found through the add_crew
        # row's reverse lookup and only its out-of-combat-at-full-health or new-pull reads reset the sighting.
        self.assertIn("bot->GetMapId() != mapId_ ||", sighting)
        self.assertNotIn("!bot->IsInCombat()", sighting)
        self.assertIn("GetBossTactics().AddCrewBossOf(bot->GetMapId(), entry, bossEntry)", sighting)
        self.assertIn("boss->GetHealth() >= boss->GetMaxHealth()", sighting)
        self.assertIn("SwitchSightingResets(bossKnown, bossAlive, bossInCombat, bossFullHealth, bossInCombat_)",
                      sighting)
        self.assertIn("bossInCombat_ = bossInCombat;", sighting)
        self.assertIn('AI_VALUE2(std::vector<TankPostUnit>, "tank post units", static_cast<int32>(entry))', sighting)
        self.assertIn("if (unit.alive)", sighting)
        self.assertIn("SwitchUnitStateNext(static_cast<SwitchUnitState>(seen_), alive)", sighting)
        # The rule itself, and the reverse lookup that finds the sighting's encounter: plain and early rows both name it.
        self.assertIn("inline bool SwitchSightingResets(bool bossKnown, bool bossAlive, bool bossInCombat, "
                      "bool bossFullHealth,", POLICY)
        self.assertIn("if (!bossKnown || !bossAlive)\n            return false;", POLICY)
        self.assertIn("if (!bossInCombat)\n            return bossFullHealth;\n        return !bossWasInCombat;", POLICY)
        boss_of = body(POLICY, "        bool AddCrewBossOf(std::uint32_t map, std::uint32_t switchEntry, "
                               "std::uint32_t& bossEntry) const")
        self.assertIn("entry.second.switchEntry != switchEntry", boss_of)
        self.assertIn("bossEntry = static_cast<std::uint32_t>(entry.first & 0xFFFFFFFFu);", boss_of)
        self.assertIn('creators["add crew switch"] = [](PlayerbotAI* ai) { return new AddCrewSwitchValue(ai); };',
                      VALUE_CONTEXT)
        # The crew's place is the separate value: it stands whatever the switch unit's life.
        self.assertIn('AI_VALUE2(bool, "add crew keeps duty", qualifier)', value)
        self.assertIn('AI_VALUE2(uint8, "add crew switch", static_cast<int32>(switchEntry))', value)
        self.assertIn("AddCrewStateFor(true, true, static_cast<SwitchUnitState>(switchState), member)", value)
        self.assertIn('creators["add crew"] = [](PlayerbotAI* ai) { return new AddCrewValue(ai); };', VALUE_CONTEXT)

    def test_crew_place_reads_the_raid_whatever_the_switch(self):
        self.assertIn("class AddCrewKeepsDutyValue : public BoolCalculatedValue, public Qualified", VALUES_H)
        self.assertIn('BoolCalculatedValue(ai, "add crew keeps duty", 2)', VALUES_H)
        value = body(VALUES_CPP, "bool AddCrewKeepsDutyValue::Calculate()")
        # Key and row first: with either off there is no crew to apply, so today's duty stands.
        self.assertLess(value.index("if (!sPlayerbotAIConfig.dungeonRunAddCrew)"), value.index("AddCrewOf("))
        self.assertIn("return true;", value)
        # The raid's AoE ranged bots: alive group members on this map and instance, not tanks or healers, ranged by
        # their own AI and in the crew's class order.
        self.assertIn("AddCrewMember(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot),", value)
        self.assertIn('AI_VALUE(std::int32_t, "add crew rank")', value)
        # The rank is the raid walk both crew values share: the AoE ranged bots through their own AIs.
        rank = body(VALUES_CPP, "std::int32_t AddCrewRankValue::Calculate()")
        self.assertIn("for (Player* member : LiveGroupMembers(bot->GetGroup()))", rank)
        self.assertIn("member->GetMapId() != bot->GetMapId()", rank)
        self.assertIn("member->GetInstanceId() != bot->GetInstanceId()", rank)
        self.assertIn("PlayerbotAI::IsTank(member) || PlayerbotAI::IsHeal(member)", rank)
        self.assertIn("PlayerbotAI* const memberAi = GET_PLAYERBOT_AI(member);", rank)
        self.assertIn("!memberAi->IsRanged(member)", rank)
        self.assertIn("AddCrewClassOrder(member->GetClass())", rank)
        self.assertIn("AddCrewRank(candidates, bot->GetObjectGuid().GetRawValue())", rank)
        self.assertIn('creators["add crew rank"] = [](PlayerbotAI* ai) { return new AddCrewRankValue(ai); };',
                      VALUE_CONTEXT)
        self.assertIn('creators["add crew keeps duty"] = [](PlayerbotAI* ai) { return new AddCrewKeepsDutyValue(ai); };',
                      VALUE_CONTEXT)

    def test_early_crew_reads_the_key_the_mode_and_the_flight(self):
        self.assertIn("class AddCrewEarlyValue : public CalculatedValue<std::uint8_t>, public Qualified", VALUES_H)
        self.assertIn('CalculatedValue<uint8>(ai, "add crew early", 2)', VALUES_H)
        value = body(VALUES_CPP, "uint8 AddCrewEarlyValue::Calculate()")
        # Both keys first: a default install reads no table, no value and no boss.
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunAddCrewEarly || !sPlayerbotAIConfig.dungeonRunAddCrew)",
                      value)
        self.assertLess(value.index("dungeonRunAddCrewEarly"), value.index("AddCrewOf("))
        self.assertIn("!GetBossTactics().AddCrewEarlyOf(bot->GetMapId(), bossEntry))", value)
        # The switch unit's sighting is the shared value (NeverSeen leaves today's duty), the flight is the boss's core
        # flag.
        self.assertIn('AI_VALUE2(uint8, "add crew switch", static_cast<int32>(switchEntry))', value)
        self.assertIn("boss->IsFlying()", value)
        self.assertIn("AddCrewEarlyDuty(true, true, static_cast<SwitchUnitState>(switchState), airborne,", value)
        # Walk 2o's split key: the duty's last input.
        self.assertIn("sPlayerbotAIConfig.dungeonRunAddCrewBossWhileSwitch", value)
        self.assertIn('creators["add crew early"] = [](PlayerbotAI* ai) { return new AddCrewEarlyValue(ai); };',
                      VALUE_CONTEXT)
        # The generic sites read it: the pick drops the row on the switch unit (a bot the row leaves on the boss takes
        # no kill_first pick there), and the hold's budget clause is lifted for a bot on the crew's whelp duty.
        actions = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
        pick = body(actions[actions.rindex("Unit* PickBossKillFirst(PlayerbotAI* ai, Player* bot, const "
                                           "BossTacticsFight& fight, KillFirstPickMode mode,"):], "Unit* "
                    "PickBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, "
                    "KillFirstPickMode mode,")
        # Walk 2o round 1: only the row on the switch unit is dropped, so the fight's other kill_first rows still pick;
        # a gate_hold row holding the boss off lifts even that (the boss duty trigger's own order). The add_crew row is
        # found through the kill_first row's switch unit, never the row's own boss entry.
        self.assertIn("ActiveBossRows rows = fight.rows;", pick)
        self.assertIn("rows = AddCrewEarlyKillFirstRows(GetBossTactics(), bot->GetMapId(), fight.rows,", pick)
        self.assertIn('AI_VALUE2(uint8, "add crew early", static_cast<int32>(bossEntry))', pick)
        # The gate's boss is read before the lambda: AI_VALUE expands to a lookup on the picker's context, which the
        # lambda does not capture (the module build refused it inside).
        self.assertIn('Unit* const onyxiaBoss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', pick)
        self.assertIn("Unit* const boss = bossEntry == kOnyxiaEntry ? onyxiaBoss : EngagedBossOfEntry(bot, "
                      "bossEntry);", pick)
        gate_lambda = pick[pick.index("[ai, bot, onyxiaBoss]"):]
        self.assertNotIn("AI_VALUE", gate_lambda[:gate_lambda.index("GateHoldsUnit")])
        self.assertIn("return GateHoldsUnit(ai, gateRows, boss);", pick)
        self.assertIn("PickKillFirst(rows, candidates)", pick)
        self.assertIn("PickCarrierOverride(rows, candidates", pick)
        self.assertNotIn("return nullptr;\n", pick[pick.index("AddCrewEarlyKillFirstRows"):])
        self.assertNotIn("row->bossEntry)) ==", pick)
        rows_policy = body(POLICY, "    inline ActiveBossRows AddCrewEarlyKillFirstRows(const BossTacticTable& table")
        self.assertIn("table.AddCrewEarlyBossOf(map, row->targetEntry, bossEntry)", rows_policy)
        self.assertIn("earlyDutyOf(bossEntry) == static_cast<std::uint8_t>(AddCrewEarly::Boss)", rows_policy)
        self.assertIn("if (bossDuty && !gateHeldOf(bossEntry))", rows_policy)
        lift = body(actions, "bool ai::AddCrewEarlyLiftsHoldKill(PlayerbotAI* ai, Player* bot, const ActiveBossRows& "
                             "rows, Unit* held)")
        self.assertIn("row->kind != BossTacticKind::HoldKill || row->bossEntry != held->GetEntry()", lift)
        self.assertIn("GetBossTactics().AddCrewEarlyBossOf(bot->GetMapId(), row->targetEntry, bossEntry)", lift)
        self.assertIn('AI_VALUE2(uint8, "add crew early", static_cast<int32>(bossEntry))', lift)
        self.assertIn("static_cast<uint8>(AddCrewEarly::Whelps))", lift)
        # The fight scan's form hands its rows to the same rule; the action sites keep reading the fight form.
        fight_form = body(actions, "bool AddCrewEarlyKillsHeldAdd(PlayerbotAI* ai, Player* bot, const "
                                   "BossTacticsFight& fight, Unit* held)")
        self.assertIn("return AddCrewEarlyLiftsHoldKill(ai, bot, fight.rows, held);", fight_form)
        self.assertEqual(5, actions.count("AddCrewEarlyKillsHeldAdd(ai, bot, fight,"),
                         "the budget lift is read at the hold-kill action's drop, the area subject, the pick, the "
                         "current target and the AoE pile")
        drop = body(actions[actions.index("Unit* ai::FindBossHoldKill("):], "Unit* ai::FindBossHoldKill(")
        self.assertIn("if (AddCrewEarlyKillsHeldAdd(ai, bot, fight, current))\n        return nullptr;", drop)
        # The DPS pickers ask the same rule over the candidate's own rows, so the crew can target the whelps it is
        # sent to kill: the lift reaches DpsLeavesAlone's kill hold and leaves gate_hold standing.
        dps = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
        held_target = body(dps, "bool HeldDpsTarget(", "\n    }\n")
        self.assertIn("const bool crewKill = AddCrewEarlyLiftsHoldKill(ai, bot, active, unit);", held_target)
        self.assertIn("HoldKillPeelOwns(ai, bot, unit), crewKill);", held_target)

    def test_whelp_duty_reads_the_value_only_under_the_key(self):
        duty = body(ACTIONS, "bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)")
        self.assertIn("bool const today = IsOnyxiaWhelpHunter(bot->GetClass(), PlayerbotAI::IsTank(bot), "
                      "PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));", duty)
        self.assertLess(duty.index("if (!sPlayerbotAIConfig.dungeonRunAddCrew)"), duty.index('"add crew"'))
        self.assertIn("AiObjectContext* context = ai->GetAiObjectContext();", duty)
        self.assertIn('uint8 const crewState = AI_VALUE2(uint8, "add crew", '
                      "static_cast<int32>(kOnyxiaEntry));", duty)
        self.assertIn("OnyxiaWhelpDutyFor(today, crewState)", duty)
        self.assertIn("inline bool OnyxiaWhelpDutyFor(bool todayDuty, std::uint8_t crewState)", WHELP_POLICY)
        self.assertIn("return crewState == 1 ? true : crewState == 2 ? false : todayDuty;", WHELP_POLICY)

    def test_other_ranged_return_to_the_boss(self):
        self.assertIn("inline bool ShouldReturnToOnyxia(bool keyOn, std::uint8_t crewState, bool rangedDps, "
                      "bool whelpPhase,", WHELP_POLICY)
        self.assertIn("return keyOn && crewState == 2 && rangedDps && whelpPhase && onWhelp && !explicitOrder && "
                      "bossValid;", WHELP_POLICY)
        trigger = body(TRIGGERS, "bool OnyxiaBossDutyTrigger::IsActive()")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)", trigger)
        self.assertIn('ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT)', trigger)
        self.assertIn("ScanOnyxiaWhelps(ai, bot)", trigger)
        self.assertIn("scan.whelpPhase", trigger)
        self.assertNotIn("scan.bossAirborne", trigger)
        self.assertIn("current->GetEntry() == kOnyxianWhelpEntry", trigger)
        self.assertIn('AI_VALUE(ObjectGuid, "explicit attack target")', trigger)
        self.assertIn("HoverReachTakerIsRangedDps(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), "
                      "ai->IsRanged(bot))", trigger)
        self.assertIn('ShouldReturnToOnyxia(true, AI_VALUE2(uint8, "add crew", '
                      "static_cast<int32>(kOnyxiaEntry)),", trigger)
        # Both the trigger and the action it fires take her from the engagement value, never the attacker list that
        # "focus rti targets" collapses to a marked whelp.
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', trigger)
        self.assertIn("PossibleAttackTargetsValue::IsValid(boss, bot)", trigger)
        action = body(ACTIONS, "bool OnyxiaAttackBossAction::Execute(Event& event)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)", action)
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', action)
        self.assertNotIn('"attackers"', action)
        self.assertIn("PossibleAttackTargetsValue::IsValid(boss, bot)", action)
        self.assertIn("return Attack(bot, boss);", action)
        self.assertIn('AttackAction(ai, "onyxia attack boss")', ACTIONS_H)
        self.assertIn('OnyxiaBossDutyTrigger(PlayerbotAI* ai, std::string name = "onyxia boss duty", '
                      "int checkInterval = 2)", TRIGGERS_H)

    def test_trigger_action_and_strategy_row(self):
        self.assertIn('creators["onyxia boss duty"] = [](PlayerbotAI* ai) '
                      '{ return new OnyxiaBossDutyTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["onyxia attack boss"] = [](PlayerbotAI* ai) '
                      '{ return new OnyxiaAttackBossAction(ai); };', ACTION_CONTEXT)
        self.assertIn('"onyxia boss duty"', STRATEGY)
        self.assertIn('new NextAction("onyxia attack boss", ACTION_HIGH + 1)', STRATEGY)

    def test_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunAddCrew = config.GetBoolDefault("AiPlayerbot.DungeonRunAddCrew", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunAddCrew = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAddCrew = 0$")
        self.assertIn("AiPlayerbot.DungeonRunAddCrew", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunAddCrew`"):]
        section = section[:section.index("### `", 4)]
        # The brief's rule for this key: documented without "recommended on".
        self.assertNotIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)
        block = TEMPLATE[TEMPLATE.index("# The add_crew boss tactic"):]
        block = block[:block.index("AiPlayerbot.DungeonRunAddCrew = 0")]
        self.assertNotIn("recommended on", block)
        self.assertIn("`add_crew`", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrew", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrew", CHANGELOG)
        self.assertIn("dungeonRunAddCrewEarly =\n        static_cast<uint32>(std::min(1, std::max(0, "
                      'config.GetIntDefault("AiPlayerbot.DungeonRunAddCrewEarly", 0))));', CONFIG_CPP)
        self.assertIn('config.GetIntDefault("AiPlayerbot.DungeonRunAddCrewEarly", 0)', CONFIG_CPP)
        self.assertIn("uint32 dungeonRunAddCrewEarly = 0;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAddCrewEarly = 0$")
        self.assertIn("### `AiPlayerbot.DungeonRunAddCrewEarly`", DOCS)
        self.assertIn("DungeonRunAddCrewEarly", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrewEarly", CHANGELOG)
        # Walk 2o's split key: read in the same loader, clamped 0-40, default 0, recommended on, documented.
        self.assertIn("static_cast<uint32>(std::min(40, std::max(0,", CONFIG_CPP)
        self.assertIn('config.GetIntDefault("AiPlayerbot.DungeonRunAddCrewBossWhileSwitch", 0))));', CONFIG_CPP)
        self.assertIn("uint32 dungeonRunAddCrewBossWhileSwitch = 0;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAddCrewBossWhileSwitch = 0$")
        block = TEMPLATE[TEMPLATE.index("# Early add crew: the boss duty past the crew"):]
        block = block[:block.index("AiPlayerbot.DungeonRunAddCrewBossWhileSwitch = 0")]
        self.assertIn("recommended on", block)
        self.assertIn("### `AiPlayerbot.DungeonRunAddCrewBossWhileSwitch`", DOCS)
        self.assertIn("DungeonRunAddCrewBossWhileSwitch", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrewBossWhileSwitch", CHANGELOG)
        section = DOCS[DOCS.index("### `AiPlayerbot.DungeonRunAddCrewBossWhileSwitch`"):]
        section = section[:section.index("### `", 4)]
        self.assertIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)

    def test_row_ships_with_its_migration(self):
        migration = (ROOT / "data/sql/world/20261111160000_world.sql").read_text()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 "
                      "AND `kind` = 'add_crew';", migration)
        self.assertIn("(249, 10184, 'add_crew', 50143, 0, 2, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0,", migration)
        # Walk 2n's UPDATE: the same row, mode early and crew 3, scoped to its 2e shape so a re-run is a no-op.
        update = (ROOT / "data/sql/world/20261116120100_world.sql").read_text()
        self.assertIn("UPDATE `ai_boss_tactic` SET `tactic_mode` = 'early', `priority` = 3", update)
        self.assertIn("AND `target_entry` = 50143 AND `priority` = 2;", update)
        # Walk 2o's UPDATE: the crew goes back to 2, scoped to its 2n shape so a re-run is a no-op.
        walk2o = (ROOT / "data/sql/world/20261116180000_world.sql").read_text()
        self.assertIn("UPDATE `ai_boss_tactic` SET `priority` = 2", walk2o)
        self.assertIn("AND `target_entry` = 50143 AND `priority` = 3;", walk2o)


if __name__ == "__main__":
    unittest.main()
