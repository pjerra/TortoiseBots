"""Wiring pins for the hold_kill healer peel (Onyxia walk 2l, AiPlayerbot.DungeonRunHoldKillHealerPeel).

Walk 2k: healers died to the whelps 5.75 times a pull, the first healer death of any kind came a median 29 s after
take-off, and 93 % of the P2 deaths followed it. The healer guard sends ranged DPS at the whelps, but one order at a
time and 2 s apart, and when the order ends the bot is free again: the hold_kill rows then send it back to Cindarion.
The peel is bot-side, inside the hold: a ranged free DPS claims one whelp that eats a group healer and the hold is
lifted for it alone.

The rules live in ai/playerbot/BossTacticsPolicy.h (Peelable, PeelClaimLive, PickHealerPeel, KillHeldFor) and are
unit-tested in tools/test_boss_tactics_policy.cpp. These pins only prove the live code: the key and its documentation,
the trigger and the action registered and hung at ACTION_HIGH + 2, every call site of the lift, the trigger's own key
gate, and the one log row.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (STRATEGY / "actions/BossTacticsActions.h").read_text()
TRIGGERS = (STRATEGY / "triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (STRATEGY / "triggers/BossTacticsTriggers.h").read_text()
TACTICS_STRATEGY = (STRATEGY / "generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (STRATEGY / "actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (STRATEGY / "triggers/TriggerContext.h").read_text()
DPS_TARGET = (STRATEGY / "values/DpsTargetValue.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF_DIST = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS_DOC = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
PLAY_DOC = (ROOT / "docs/play/boss-tactics.md").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_boss_tactics_policy.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def node(text, trigger_name):
    match = re.search(r'"%s",\s*NextAction::array\(0,(.*?)\)\)\);' % re.escape(trigger_name), text, re.S)
    assert match is not None, trigger_name
    return match.group(1)


class PeelKey(unittest.TestCase):
    """The key is off by default and documented where the other dungeon-run keys are (the shipped templates must agree
    with the code's fallback: tools/verify_config_defaults.py reads both)."""

    def test_the_key_is_off_by_default(self):
        self.assertIn('GetIntDefault("AiPlayerbot.DungeonRunHoldKillHealerPeel", 0)', CONFIG)
        self.assertIn("\nAiPlayerbot.DungeonRunHoldKillHealerPeel = 0\n", CONF_DIST)
        self.assertIn("uint32 dungeonRunHoldKillHealerPeel = 0;", CONFIG_H)
        # Read by the reloadable DungeonRun* loader, next to the healer guard it extends.
        self.assertLess(CONFIG.index('GetBoolDefault("AiPlayerbot.DungeonRunRaidHealerGuard"'),
                        CONFIG.index('GetIntDefault("AiPlayerbot.DungeonRunHoldKillHealerPeel"'))

    def test_the_templates_document_the_key(self):
        conf = CONF_DIST[CONF_DIST.index("AiPlayerbot.DungeonRunHoldKillHealerPeel = 0") - 1600:
                         CONF_DIST.index("AiPlayerbot.DungeonRunHoldKillHealerPeel = 0")]
        self.assertIn("# 0 - off", conf)
        self.assertIn("# N - at most N live claims per map instance", conf)
        self.assertIn("Recommended on at 4.", conf)
        self.assertIn("`AiPlayerbot.DungeonRunHoldKillHealerPeel`", SETTINGS_DOC)
        self.assertIn("### `AiPlayerbot.DungeonRunHoldKillHealerPeel`", SETTINGS_DOC)


class PeelRuns(unittest.TestCase):
    """The trigger and the action are registered and hung at ACTION_HIGH + 2, level with the hold-kill action it
    qualifies and above the kill-first pick (HIGH + 1)."""

    def test_the_trigger_and_action_are_registered(self):
        self.assertIn('creators["boss tactics healer peel"] = '
                      '[](PlayerbotAI* ai) { return new BossTacticsHealerPeelTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics healer peel"] = '
                      '[](PlayerbotAI* ai) { return new BossTacticsHealerPeelAction(ai); };', ACTION_CONTEXT)
        self.assertIn('Trigger(ai, "boss tactics healer peel", 1)', TRIGGERS_H)
        self.assertIn('AttackAction(ai, "boss tactics healer peel")', ACTIONS_H)

    def test_the_node_sits_at_high_plus_two(self):
        self.assertIn('new NextAction("boss tactics healer peel", ACTION_HIGH + 2)',
                      node(TACTICS_STRATEGY, "boss tactics healer peel"))

    def test_the_trigger_is_false_with_the_key_off(self):
        fn = body(TRIGGERS, "bool BossTacticsHealerPeelTrigger::IsActive()")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunHoldKillHealerPeel || !Ready(ai, bot))", fn)
        self.assertLess(fn.index("dungeonRunHoldKillHealerPeel"), fn.index("ScanBossTactics("))
        self.assertIn("ScanBossHealerPeel(ai, bot, fight)", fn)
        # Already on the picked unit: the read that kept the claim refreshes it, so there is nothing to switch to.
        self.assertIn('return AI_VALUE(Unit*, "current target") != scan.units[scan.pick];', fn)


class TheHoldIsLiftedForTheClaimantOnly(unittest.TestCase):
    """The lift goes through HoldKillPeelOwns at every reader of the hold: the current target's test, the dps target's
    held filter and both single-target hold clauses of the multiplier. Nothing else reads it, so the flag off leaves
    today's behaviour exactly."""

    def test_find_boss_hold_kill_asks_the_claim(self):
        fn = body(ACTIONS, "Unit* ai::FindBossHoldKill(")
        self.assertIn("if (!KillHeld(fight.rows, current->GetEntry(), countNear))\n        return nullptr;", fn)
        self.assertIn("if (HoldKillPeelOwns(ai, bot, current))\n        return nullptr;", fn)
        self.assertLess(fn.index("KillHeld(fight.rows"), fn.index("HoldKillPeelOwns(ai, bot, current)"))

    def test_the_dps_target_filter_passes_the_claim(self):
        fn = body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")
        self.assertIn("DpsLeavesAlone(true, active, unit->GetEntry(), static_cast<int>(unit->GetHealthPercent()), "
                      "countNear,\n                              countFighting, HoldKillPeelOwns(ai, bot, unit));", fn)

    def test_the_multiplier_lifts_only_the_single_target_clauses(self):
        fn = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        # The assist row's pick and the current target, both through KillHeldFor with the bot's own claim.
        self.assertIn("const bool peelOwned = !aoe && picked && HoldKillPeelOwns(ai, bot, picked);", fn)
        self.assertIn("KillHeldFor(fight.rows, picked->GetEntry(), countNear, peelOwned)", fn)
        self.assertIn("KillHeldFor(fight.rows, current->GetEntry(), countNear, HoldKillPeelOwns(ai, bot, current))", fn)
        # The AoE clause that stays is the pile one, and the area / attacker-debuff vetos still read KillHeld.
        self.assertIn("HeldUnitNear(ai, current, fight, countNear)", fn)
        self.assertNotIn("HeldUnitNear(ai, current, fight, countNear, peelOwned)", fn)

    def test_the_switch_never_lands_on_another_bots_claim(self):
        fn = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("PickNearestUnheld(fight.rows, candidates, countNear, 0, claimedByOther)", fn)
        self.assertIn("claim->second.botGuid != botGuid", fn)
        self.assertIn("if (claimedByOther && claimedByOther(c.guid))\n                continue;", POLICY)


class TheClaimStore(unittest.TestCase):
    """One claim per unit and map instance, one per bot, the stamp refreshed on every read and lapsed after
    kHoldKillPeelClaimMs. The store is written by the action and read by every bot, hence the mutex; its map rules are
    BossTacticsPolicy's HoldKillPeelClaims, unit-tested, and the action takes its claim through the one atomic call."""

    def test_the_store_is_keyed_by_instance_and_unit(self):
        self.assertIn("using Key = std::pair<std::uint32_t, std::uint64_t>;", POLICY)
        self.assertIn("struct HoldKillPeelClaims", POLICY)
        self.assertIn("std::mutex g_holdKillPeelMutex;", ACTIONS)
        self.assertIn("HoldKillPeelClaims g_holdKillPeelClaims;", ACTIONS)
        claim = body(POLICY, "bool TryClaim(", "\n        }\n")
        self.assertIn("Prune(nowMs);", claim)
        self.assertIn("claim.second.botGuid != botGuid", claim)      # one claim per bot
        self.assertIn("it->second.botGuid != botGuid", claim)        # never another bot's live claim
        self.assertIn("LiveCount(instanceId) >= cap", claim)         # the cap, on the live store
        prune = body(POLICY, "void Prune(", "\n        }\n")
        self.assertIn("!PeelClaimLive(it->second.botGuid, it->second.stampMs, nowMs)", prune)
        self.assertIn("return bot != 0 && (stampMs > nowMs || nowMs - stampMs <= kHoldKillPeelClaimMs);", POLICY)
        # The clock is read under the lock, so a stamp from a racing write cannot look lapsed.
        take = body(ACTIONS, "bool TryClaimHoldKillPeel(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_holdKillPeelMutex);", take)
        self.assertIn("WorldTimer::getMSTime()", take)
        claims_of = body(ACTIONS, "std::map<std::uint64_t, HoldKillPeelClaims::Claim> HoldKillPeelClaimsOf(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_holdKillPeelMutex);", claims_of)
        self.assertIn("const std::uint32_t clockMs = WorldTimer::getMSTime();", claims_of)
        self.assertIn("*nowMs = clockMs;", claims_of)
        self.assertIn("g_holdKillPeelClaims.Prune(clockMs);", claims_of)

    def test_the_claim_refreshes_only_while_the_victim_stays_a_healer(self):
        fn = body(ACTIONS, "bool ai::HoldKillPeelOwns(")
        self.assertIn("it->second.botGuid != botGuid", fn)          # never another bot's claim
        self.assertIn("PeelClaimLive(it->second.botGuid, it->second.stampMs, nowMs)", fn)
        self.assertIn("refresh = victimHealer;", fn)
        # The healer test reads another bot's strategy state, so it runs before the peel lock.
        self.assertLess(fn.index("const bool victimHealer = PeelVictimIsGroupHealer(bot, unit);"),
                        fn.index("std::lock_guard<std::mutex> lock(g_holdKillPeelMutex);"))
        self.assertLess(fn.index("std::lock_guard<std::mutex> lock(g_holdKillPeelMutex);"),
                        fn.index("LogHoldKillPeel(ai, bot, \"keep\""))
        healer = body(ACTIONS, "bool PeelVictimIsGroupHealer(")
        self.assertIn("PlayerbotAI::IsHeal(owner)", healer)
        self.assertIn("group->IsMember(owner->GetObjectGuid())", healer)

    def test_the_action_claims_its_pick_and_drops_a_lapsed_one(self):
        fn = body(ACTIONS, "bool BossTacticsHealerPeelAction::Execute(")
        self.assertIn("const BossHealerPeelScan scan = ScanBossHealerPeel(ai, bot, fight);", fn)
        self.assertIn("DropHoldKillPeelClaim(bot, instanceId, own.guid)", fn)
        self.assertIn("TryClaimHoldKillPeel(instanceId, pickGuid, bot->GetObjectGuid().GetRawValue(), "
                      "pick->GetEntry(),", fn)
        # The attack is behind the granted claim, never on the scan's own guess.
        granted = fn.index("TryClaimHoldKillPeel(")
        self.assertLess(fn.index("return false;", granted), fn.index("return Attack(bot, pick);"))
        self.assertNotIn("ClaimHoldKillPeel(bot", fn)
        # The candidates come from the entries the fight still holds, through the hold's own grid search; an entry the
        # hold has released contributes nothing.
        scan = body(ACTIONS, "ai::BossHealerPeelScan ai::ScanBossHealerPeel(")
        self.assertIn("HoldKillPeelClaimsOf(bot->GetInstanceId(), &nowMs)", scan)
        self.assertIn("for (std::uint32_t entry : PeelEntries(fight.rows, countNear))", scan)
        self.assertNotIn("row->bossEntry", scan)
        self.assertIn('"kill first grid units"', scan)
        self.assertIn("PickHealerPeel(scan.candidates, botGuid, nowMs, static_cast<int>(scan.liveClaimsOthers),", scan)
        self.assertIn("IsFreeDps(ai, bot) && ai->IsRanged(bot)", scan)
        switch = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("HoldKillPeelClaimsOf(bot->GetInstanceId(), nullptr)", switch)


class ThePeelIsLogged(unittest.TestCase):
    """Walk 2l reads the peel off bot_events.csv: one BossTacticsHoldKill row with peel=claim|keep|lapse and the unit,
    its victim and the instance's claim count. Its own 2 s bucket, because a claim only lives 3 s."""

    def test_the_row_is_written(self):
        log = body(ACTIONS, "void LogHoldKillPeel(")
        self.assertIn('snprintf(info1, sizeof(info1), "peel=%s", word);', log)
        self.assertIn('"unit=%u:%u,victim=%s,claims=%u"', log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsHoldKill", info1, info2);', log)
        self.assertIn("HoldKillLogDue(bot, HoldKillLogKind::Peel)", log)
        self.assertIn("constexpr uint32 kHoldKillPeelLogGapMs = 2000;", ACTIONS)
        self.assertIn("return kind == HoldKillLogKind::Peel ? kHoldKillPeelLogGapMs : kHoldKillLogGapMs;", ACTIONS)

    def test_every_claim_end_is_one_of_the_three_words(self):
        fn = body(ACTIONS, "bool BossTacticsHealerPeelAction::Execute(")
        self.assertIn('LogHoldKillPeel(ai, bot, "lapse"', fn)
        self.assertIn('LogHoldKillPeel(ai, bot, "claim"', fn)
        self.assertIn('LogHoldKillPeel(ai, bot, "keep"', ACTIONS)
        for word in ('"peel=claim"', '"peel=keep"', '"peel=lapse"'):
            self.assertNotIn(word, ACTIONS)     # the words are built from the argument, never spelled twice

    def test_the_play_doc_names_the_rule(self):
        self.assertIn("DungeonRunHoldKillHealerPeel", PLAY_DOC)
        self.assertIn("healer peel", PLAY_DOC)


class ThePolicyIsUnitTested(unittest.TestCase):
    def test_the_failing_case_is_named(self):
        self.assertIn("static void HoldKillPeelLiftsClaimedHealerAttacker()", POLICY_TEST)
        self.assertIn("static void HealerPeelPick_OneHealerAddPerBot()", POLICY_TEST)
        self.assertIn("static void HealerPeelClaims_TryClaimIsAtomic()", POLICY_TEST)
        self.assertIn("static void HealerPeelEntries_OnlyHeldEntriesArePeeled()", POLICY_TEST)
        self.assertIn("HoldKillPeelLiftsClaimedHealerAttacker();", POLICY_TEST)
        self.assertIn("HealerPeelPick_OneHealerAddPerBot();", POLICY_TEST)
        self.assertIn("HealerPeelClaims_TryClaimIsAtomic();", POLICY_TEST)
        self.assertIn("HealerPeelEntries_OnlyHeldEntriesArePeeled();", POLICY_TEST)
        self.assertIn("if (!KillHeld(rows, row->bossEntry, countNear))", POLICY)
        # The lift is the policy's, and only through KillHeldFor: KillHeld itself never learns about claims.
        killheld = body(POLICY, "    inline bool KillHeld(", "\n    }\n")
        self.assertNotIn("peelOwned", killheld)
        self.assertIn("return !peelOwned && KillHeld(rows, entry, countNear);", POLICY)


if __name__ == "__main__":
    unittest.main()
