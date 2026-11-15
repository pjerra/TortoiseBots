"""The pack focus's glue: the run's skull used to come from the plan's kill_first list alone, so a plan whose list names
one entry (scarlet_library: 4291, its Scarlet Diviner) marked that entry and no other mob of the wing.

W1: the glue reads a mana user (a healer or a caster) on every engaged unit, the pack focus's first class
    (ReadEngagedWith, Facts::engagedUnits).
W2: the fight tick keeps PickKillFirst first and falls back to PickPackFocus, then to FocusGone/DropFocus, so the
    plan's own list is never outranked.
W3: a pull or move step's pack gets the fallback, and so does a boss step's adds: the boss itself, the units a
    kill_first/linked_death row names, an event and a rest fight return no pick.
W4: a mana user goes before the rest and only a unit of the fight, attackable, not evading, not held by the run's
    crowd control and within kKillFirstMaxYd of the lead bot is a candidate.
W5: a skull a player moved onto a living hostile of the fight is adopted as the focus (ahead of the plan's
    kill_first and the fallback), sent by guid without moving the player's mark.
W6: FocusGone also reads Facts::engagedUnits, so a pack focus whose unit left the fight is dropped.
W7: a run skull record is live only while its unit is a living hostile of the fight (SettleFocusIcons drops a record
    whose unit died, despawned or left the fight, and the previous mark once the new one landed); the tick reads a
    skull as the player's only when no live record matches it.
W8: a kill_first unit that is only passive (in no combat yet) keeps the run's skull: the record survives on a living,
    attackable kill_first unit (SkullUnitLives), PickKillFirst keeps the focused one, the pack's fallback waits for its
    death, and FocusGone does not end the focus on it.
W9: with AiPlayerbot.DungeonRunPackFocusSparesHoldKill (default 0) a boss step's fallback never skulls an entry a
    hold_kill row of the plan's map spares (HoldKillRowNames), and a bot whose explicit attack target died or left the
    world is free again (IsFreeDps); the peel's lapse rows get their own log bucket, and the peel trigger's claim test
    asks the map before any mutex or group lookup (GetBossTactics().HasMap).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
GENERIC_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF_DIST = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS_DOC = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
PLAY_DOC = (ROOT / "docs/play/boss-tactics.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PackFocusWiringTest(unittest.TestCase):
    def test_engaged_reads_a_mana_user(self):
        fn = body(COMMANDER, "void ReadEngagedWith(")
        self.assertIn("fact.caster = other->GetMaxPower(POWER_MANA) > 0;", fn)

    def test_tick_falls_back_after_the_plans_kill_first(self):
        fn = body(POLICY, "        if (const UnitFact* const h = PlayerSkullFocus(r, f))")
        # The plan's kill_first pick keeps its own branch; the fallback is the else-if after it.
        self.assertIn("else if (const UnitFact* k = PickKillFirst(r, f))", fn)
        self.assertIn("order = IssueKillFirstFocus(r, *k, FocusMovesSkull(", fn)
        fallback = fn[fn.index("else if (const UnitFact* p = PickPackFocus(r, f))"):]
        self.assertIn("IssueKillFirstFocus(r, *p, FocusMovesSkull(", fallback)
        # ... and DropFocus stays behind both (the fallback never replaces it).
        self.assertLess(fn.index("PickPackFocus"), fn.index("FocusGone(r, f)"))
        self.assertIn("DropFocus(r);", fn)

    def test_pull_move_and_boss_adds_get_the_fallback(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("bool const pullOrMove = s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Move ||",
                      fn)
        self.assertIn("bool const boss = s->kind == PlanStepKind::Boss;", fn)
        self.assertIn("if (!pullOrMove && !boss)", fn)
        self.assertIn("return nullptr;", fn)

    def test_a_boss_add_is_no_boss_and_no_tactic_row_unit(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("if (boss && (IsStepBoss(*s, u) || (r.plan && r.plan->KillFirstRank(u.entry) >= 0) ||", fn)
        self.assertIn("BossTacticOwnsEntry(r, *s, u.entry) ||", fn)
        own = body(POLICY, "inline bool BossTacticOwnsEntry(const Run& r, const PlanStep& s, std::uint32_t entry)\n{")
        self.assertIn("(row.kind == ai::BossTacticKind::KillFirst || row.kind == ai::BossTacticKind::LinkedDeath)",
                      own)
        self.assertIn("(entry == row.targetEntry || entry == row.bossEntry)", own)

    def test_a_players_skull_is_adopted(self):
        fn = body(POLICY, "inline const UnitFact* PlayerSkullFocus(")
        self.assertIn("std::uint64_t const skull = f.raidIcons[kFocusIcon];", fn)
        self.assertIn(
            "if (!skull || skull == r.focusIconGuid || skull == r.prevFocusIconGuid || skull == r.focusIconClear)",
            fn)
        self.assertIn("if (u.guid == skull && u.hostile)", fn)
        self.assertIn("return &u;", fn)
        tick = body(POLICY, "        SettleFocusIcons(r, f);")
        # The player's mark is adopted ahead of the run's own pick, and its order moves no skull.
        self.assertLess(tick.index("PlayerSkullFocus"), tick.index("PickKillFirst(r, f)"))
        self.assertIn("order = IssueKillFirstFocus(r, *h, false);", tick)

    def test_a_runs_skull_record_dies_with_its_unit(self):
        fn = body(POLICY, "inline void SettleFocusIcons(")
        self.assertIn("bool const landed = r.runMs >= r.focusIconMs && r.runMs - r.focusIconMs >= kFocusRefusedIgnoreMs;", fn)
        self.assertIn("if (r.focusIconGuid && skull != r.focusIconGuid && landed)", fn)
        self.assertIn("if (r.prevFocusIconGuid && skull != r.prevFocusIconGuid)", fn)
        self.assertIn("if (r.focusIconGuid && !SkullUnitLives(r, f, r.focusIconGuid))", fn)
        self.assertIn("if (r.prevFocusIconGuid && !SkullUnitLives(r, f, r.prevFocusIconGuid))", fn)
        tick = body(POLICY, "        SettleFocusIcons(r, f);")
        self.assertLess(tick.index("SettleFocusIcons(r, f)"), tick.index("PlayerSkullFocus(r, f)"))
        issue = body(POLICY, "inline std::string IssueKillFirstFocus(")
        self.assertIn("r.focusIconMs = r.runMs;", issue)

    def test_a_passive_kill_first_target_keeps_the_skull(self):
        lives = body(POLICY, "inline bool SkullUnitLives(")
        self.assertIn("EngagedHostile(f, guid) != nullptr", lives)
        self.assertIn("LivingKillFirstUnit(r, f, guid) != nullptr", lives)
        living = body(POLICY, "inline const UnitFact* LivingKillFirstUnit(")
        self.assertIn("u.guid == guid && u.hostile && u.attackable && !u.evading && !u.stealthed &&", living)
        self.assertIn("KillFirstEntry(r, u.entry)", living)
        self.assertIn("if (const UnitFact* const u = find(f.killFirst))", living)
        self.assertIn("return find(f.units);", living)
        pick = body(POLICY, "inline const UnitFact* PickKillFirst(")
        self.assertIn("bool const heldEntry = u.guid == r.focusGuid && (rank >= 0 || KillFirstEntry(r, u.entry));", pick)
        self.assertIn("!u.auraOnEngaged && !heldEntry", pick)
        fallback = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("if (LivingKillFirstUnit(r, f, r.focusGuid))", fallback)
        gone = body(POLICY, "inline bool FocusGone(")
        self.assertIn("if (LivingKillFirstUnit(r, f, r.focusGuid))", gone)
        self.assertIn("return false;", gone)

    def test_priority_and_the_filters(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("int const cls = u.caster ? 0 : 1;", fn)
        self.assertIn("!u.hostile || !u.attackable || u.evading || !u.engaged || u.polymorphed || r.ccSent.count(u.guid)",
                      fn)
        self.assertIn("r.runMs < refused->second", fn)
        self.assertIn("u.distLead > kKillFirstMaxYd", fn)

    def test_focus_gone_reads_the_engaged_units(self):
        fn = body(POLICY, "inline bool FocusGone(")
        self.assertIn("for (const UnitFact& u : f.killFirst)", fn)
        self.assertIn("for (const UnitFact& u : f.engagedUnits)", fn)


class PackFocusSparesHoldKill(unittest.TestCase):
    """The pack focus's hold_kill spare (Onyxia walk 2l3): a boss step's fallback must not skull an entry a hold_kill
    row spares, or the focus order commits every bot that takes it and the hold and the healer peel have no free DPS."""

    def test_the_key_is_off_by_default(self):
        self.assertIn('GetIntDefault("AiPlayerbot.DungeonRunPackFocusSparesHoldKill", 0)', CONFIG)
        self.assertIn("\nAiPlayerbot.DungeonRunPackFocusSparesHoldKill = 0\n", CONF_DIST)
        self.assertIn("uint32 dungeonRunPackFocusSparesHoldKill = 0;", CONFIG_H)
        # Read by the reloadable DungeonRun* loader, next to the peel key it completes.
        self.assertLess(CONFIG.index('GetIntDefault("AiPlayerbot.DungeonRunHoldKillHealerPeel"'),
                        CONFIG.index('GetIntDefault("AiPlayerbot.DungeonRunPackFocusSparesHoldKill"'))

    def test_the_facts_bit_reaches_the_policy(self):
        self.assertIn("bool packFocusSparesHoldKill = false;", POLICY)
        self.assertIn("f.packFocusSparesHoldKill = "
                      "sPlayerbotAIConfig.dungeonRunPackFocusSparesHoldKill != 0;", COMMANDER)

    def test_the_pack_focus_asks_the_rows(self):
        self.assertIn("inline bool HoldKillRowNames(const Run& r, std::uint32_t entry)", POLICY)
        helper = body(POLICY, "inline bool HoldKillRowNames(")
        self.assertIn("r.liveTactics->Rows(r.plan->map, entry)", helper)
        self.assertIn("row.kind == ai::BossTacticKind::HoldKill && row.bossEntry == entry", helper)
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("(f.packFocusSparesHoldKill && HoldKillRowNames(r, u.entry))", fn)

    def test_a_dead_explicit_order_is_no_order(self):
        fn = body(ACTIONS, "bool IsFreeDps(")
        self.assertIn('context->GetValue<ObjectGuid>("explicit attack target")->Get();', fn)
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunPackFocusSparesHoldKill)\n        return false;", fn)
        self.assertIn("Unit* const unit = ObjectAccessor::GetUnit(*bot, ordered);", fn)
        self.assertIn("return !unit || !unit->IsInWorld() || !unit->IsAlive();", fn)

    def test_the_peel_trigger_asks_the_map_first(self):
        single = body(GENERIC_TRIGGERS, "bool NotDpsTargetActiveTrigger::IsActive()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunHoldKillHealerPeel &&", single)
        self.assertIn("GetBossTactics().HasMap(bot->GetMapId())", single)
        self.assertLess(single.index("GetBossTactics().HasMap(bot->GetMapId())"),
                        single.index("HoldKillPeelOwns(ai, bot, target);"))
        aoe = body(GENERIC_TRIGGERS, "bool NotDpsAoeTargetActiveTrigger::IsActive()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunHoldKillHealerPeel && "
                      "GetBossTactics().HasMap(bot->GetMapId())", aoe)
        self.assertLess(aoe.index("GetBossTactics().HasMap(bot->GetMapId())"),
                        aoe.index("HoldKillPeelOwns(ai, bot, target);"))

    def test_the_key_is_documented(self):
        self.assertIn("### `AiPlayerbot.DungeonRunPackFocusSparesHoldKill`", SETTINGS_DOC)
        self.assertIn("DungeonRunPackFocusSparesHoldKill", PLAY_DOC)


if __name__ == "__main__":
    unittest.main()
