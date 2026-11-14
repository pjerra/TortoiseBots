"""Spec 04: meals wait while the run fights, wherever the bot stands.

The run leader stamps eating and drinking bots once a second; the gate, the
wake check and the `.bot action rest` refusal all read the same mark.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ITEMS = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.h").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
PLAYERBOT_AI_HEADER = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
ACTION = (ROOT / "commands/Action.cpp").read_text()
FOOD = (ROOT / "ai/playerbot/strategy/generic/UseFoodStrategy.cpp").read_text()
GENERIC_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
VALUE_CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()
SURVIVE = (ROOT / "ai/playerbot/SurvivePolicy.h").read_text()

MARK = '"run fight until ms"'


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def span_after(text, marker, start=0):
    """From the marker through the closing brace of its block."""
    i = text.index(marker, start)
    brace = text.index("{", i)
    depth = 0
    for j in range(brace, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


def body_after(text, marker, start=0):
    i = text.index(marker, start)
    i = text.index("{", i)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


SRC_ITEMS = strip_comments(ITEMS)
SRC_AI = strip_comments(PLAYERBOT_AI)
SRC_AI_HEADER = strip_comments(PLAYERBOT_AI_HEADER)
SRC_COMMANDER = strip_comments(COMMANDER)
SRC_ACTION = strip_comments(ACTION)
SRC_FOOD = strip_comments(FOOD)
SRC_GENERIC_TRIGGERS = strip_comments(GENERIC_TRIGGERS)
SRC_TRIGGER_CONTEXT = strip_comments(TRIGGER_CONTEXT)
SRC_VALUE_CONTEXT = strip_comments(VALUE_CONTEXT)
SRC_SURVIVE = strip_comments(SURVIVE)


def model_low_mana_drink_useful(mana_pct, cleanse_pending):
    """Mirror of ai::LowManaDrinkUseful; tools/test_survive_policy.cpp pins it."""
    limit = int(re.search(r"LOW_MANA_DRINK_PCT\s*=\s*([0-9]+)", SRC_SURVIVE).group(1))
    return mana_pct <= limit and not cleanse_pending


def model_party_cleanse_pending(member_needs_cure, cast_ok):
    """Mirror of PartyCleansePending: a member to cure AND the cast allowed."""
    return member_needs_cure and cast_ok


class MealGateWiringTest(unittest.TestCase):
    def test_group_is_fighting_reads_the_run_mark(self):
        body = body_after(SRC_ITEMS, "inline bool GroupIsFighting")
        self.assertIn("MealBlocked(", body)
        self.assertIn(MARK, body)

    def test_policy_header_included_by_every_reader(self):
        for name, text in (
            ("UseItemAction.h", SRC_ITEMS),
            ("PlayerbotAI.cpp", SRC_AI),
            ("Commander.cpp", SRC_COMMANDER),
        ):
            self.assertIn("playerbot/policy/combat/MealGatePolicy.h", text, name)

    def test_wake_check_stands_a_seated_bot_for_the_run(self):
        body = span_after(SRC_AI, "if (aiInternalUpdateDelay > 0 && !isWaiting")
        flat = " ".join(body.split())
        self.assertIn(MARK, flat)
        # The member test and the run's mark are alternatives: `&&` would
        # never wake a bot the member test cannot see.
        self.assertRegex(flat, r"IsGroupMemberNearInCombat\(\) \|\| RunFightHolds\(")

    def test_meal_sleep_wake_runs_outside_the_seated_only_guard(self):
        """WHY paladin-addpickup: a bot stood up by damage is still asleep."""
        wake = " ".join(body_after(SRC_AI, "void PlayerbotAI::WakeFromMealSleep()").split())
        # Every fact the policy needs comes from the world here: its own
        # combat flag (a hit during the meal), the member test, the run mark.
        self.assertIn("MealSleepShouldWake(", wake)
        self.assertIn("IsSittingDown()", wake)
        # The meal term is a Food or Drink aura, not any aura standing would
        # break: a removed drink ends the sleep.
        self.assertIn("HasSeatedMeal(true) || HasSeatedMeal(false)", wake)
        self.assertNotIn("HasSeatedAura(bot)", wake)
        self.assertIn("IsInCombat(bot)", wake)
        self.assertIn("IsGroupMemberNearInCombat()", wake)
        self.assertIn("RunFightHolds(", wake)
        for step in ("SetStandState(UNIT_STAND_STATE_STAND)", "ResetAIInternalUpdateDelay()",
                     "m_mealSleep = false"):
            self.assertIn(step, wake)

        # Both branches of UpdateAI call it: the combat branch and the
        # non-combat branch. Not only the seated bot.
        update = body_after(SRC_AI, "void PlayerbotAI::UpdateAI(uint32 elapsed")
        combat = body_after(update, "if (sServerFacade.IsInCombat(bot))")
        self.assertIn("WakeFromMealSleep();", combat)
        after_combat = update.index(combat) + len(combat)
        non_combat = body_after(update, "else", after_combat)
        self.assertIn("WakeFromMealSleep();", non_combat)

        # And the call is outside every seated-only guard: the guard the live
        # bug hides behind, whose span_after block may never contain it.
        guard = "if (aiInternalUpdateDelay > 0 && !isWaiting && bot->IsSittingDown() &&"
        calls = [i for i in range(len(update)) if update.startswith("WakeFromMealSleep();", i)]
        self.assertGreaterEqual(len(calls), 2)
        start = 0
        while True:
            try:
                at = update.index(guard, start)
            except ValueError:
                break
            block = span_after(update, guard, at)
            for call in calls:
                self.assertFalse(at <= call < at + len(block),
                    "the meal sleep wake must not live inside the seated-only guard")
            start = at + 1

    def test_meal_actions_stamp_the_sleep(self):
        drink = body_after(SRC_ITEMS, "class DrinkAction")
        eat = body_after(SRC_ITEMS, "class EatAction")
        for name, text in (("drink", drink), ("food", eat)):
            self.assertEqual(text.count("ai->SetMealSleep(true);"), 4,
                "%s: every meal sleep duration is stamped" % name)
            # The real-item path has no SetDuration of its own: UseAction sets
            # the 24 s meal sleep on a successful use, and the flag must follow
            # it, or the wake check never sees an ordinary meal.
            after_used = " ".join(text.split())
            after_used = after_used[after_used.index("bool const used = UseAction::Execute(event);"):]
            self.assertIn("ai->SetMealSleep(true);", after_used,
                "%s: the real-item path stamps the sleep after a successful use" % name)

    def test_a_refused_meal_cast_arms_no_sleep(self):
        # Nightly priest low-mana drinks: the drink tried in the Fear Ward GCD
        # was refused, yet the action slept the AI for the meal's whole 22 s.
        for name, klass, spell, duration in (
            ("drink", "class DrinkAction", 24355, "drinkDuration"),
            ("food", "class EatAction", 24005, "eatDuration"),
        ):
            flat = " ".join(body_after(SRC_ITEMS, klass).split())
            guard = "if (!ai->CastSpell(%d, bot)) return false;" % spell
            # A failed cast returns before the duration and the sleep stamp,
            # so the next tick retries instead of idling out the meal.
            self.assertIn(
                guard + " SetDuration(%s); ai->SetMealSleep(true);" % duration, flat,
                "%s: only a successful cast arms the meal sleep" % name)
            self.assertNotIn("ai->CastSpell(%d, bot); SetDuration(%s);" % (spell, duration), flat,
                "%s: the cast's result is checked before the meal is armed" % name)

    def test_reset_of_the_action_clock_clears_the_meal_flag(self):
        reset = body_after(SRC_AI, "void PlayerbotAI::ResetAIInternalUpdateDelay()")
        self.assertIn("PlayerbotAIBase::ResetAIInternalUpdateDelay();", reset)
        self.assertIn("m_mealSleep = false;", reset)
        self.assertIn("void ResetAIInternalUpdateDelay() override;", SRC_AI_HEADER,
            "the wake, combat start and human orders all drop the clock through it")

    def test_sleep_end_and_combat_start_clear_the_flag(self):
        update_start = SRC_AI.index("void PlayerbotAI::UpdateAI(uint32 elapsed")
        clear = SRC_AI.index("m_mealSleep = false", update_start)
        branch = SRC_AI.index("bool isCasting = bot->IsNonMeleeSpellCasted(true);", clear)
        self.assertLess(clear, branch,
            "the flag clears when the delay runs out, before the branch")
        self.assertIn("m_mealSleep = false;", body_after(SRC_AI, "void PlayerbotAI::OnCombatStarted()"))

    def test_run_tick_marks_while_the_run_fights_and_clears_when_it_ends(self):
        after_monitor = SRC_COMMANDER.index("RefreshCombatMonitor(commanderRaw")
        step_before = SRC_COMMANDER.index("std::size_t const stepBeforeTick", after_monitor)
        region = SRC_COMMANDER[after_monitor:step_before]
        flat = " ".join(region.split())
        self.assertEqual(flat.count("if (facts.anyInCombat)"), 1,
            "the mark must be gated on the run fighting")
        # The stamp is the hold ahead of now, marked with `| 1` so a computed
        # 0 never reads as "never set".
        self.assertIn(
            "if (facts.anyInCombat) { uint32 const until = WorldTimer::getMSTime() + ai::RUN_FIGHT_HOLD_MS;",
            flat)
        self.assertIn('->GetValue<int32>("manual int", "run fight until ms")->Set(int32(until | 1));', flat)
        # The tick that sees no fight clears the same bots at once: the run's
        # own rest order goes out on the next tick and the live hold would
        # still refuse it.
        else_flat = flat[flat.index("} else"):]
        self.assertIn("} else for (Player* bot : bots)", else_flat)
        self.assertIn('->GetValue<int32>("manual int", "run fight until ms")->Set(0);', else_flat)

    def test_rest_refusal_names_the_same_gate(self):
        self.assertIn("in.groupFighting = GroupIsFighting(ai);", SRC_ACTION)
        self.assertNotIn("IsGroupMemberNearInCombat", SRC_ACTION)

    def test_low_mana_drink_outranks_the_self_buffs(self):
        """OPEN-3: at 5 % mana the first action was PW:Fortitude, not Drink."""
        flat = " ".join(body_after(SRC_FOOD, "void UseFoodStrategy::InitNonCombatTriggers").split())
        self.assertIn(
            '"low mana drink", NextAction::array(0, new NextAction("drink", ai::LOW_MANA_DRINK_PRIORITY), NULL)',
            flat)
        # The trigger exists, is registered, and reads the shared 20 % line.
        self.assertIn('creators["low mana drink"]', SRC_TRIGGER_CONTEXT)
        trigger = " ".join(body_after(SRC_GENERIC_TRIGGERS, "bool LowManaDrinkTrigger::IsActive()").split())
        self.assertIn("DrinkOutranksSelfBuffs(", trigger)
        self.assertIn("HasManaPool(bot)", trigger)
        self.assertIn("LOW_MANA_DRINK_PCT = 20", SRC_SURVIVE)

        # Every buff-maintenance row declared by the class strategies, with
        # the value that row carries. The highest are Soul Link and Righteous
        # Fury at 28.0; the drink must clear the top, not an older tail.
        priority = float(re.search(r"LOW_MANA_DRINK_PRIORITY\s*=\s*([0-9.]+)f", SRC_SURVIVE).group(1))
        competitors = [
            ("ai/playerbot/strategy/warlock/GenericWarlockNonCombatStrategy.cpp",
             'NextAction("soul link", 28.0f)', 28.0),
            ("ai/playerbot/strategy/paladin/PaladinBuffStrategies.cpp",
             'NextAction("righteous fury", ACTION_HIGH + 8)', 28.0),
            ("ai/playerbot/strategy/warlock/GenericWarlockNonCombatStrategy.cpp",
             'NextAction("demon armor", 27.0f)', 27.0),
            ("ai/playerbot/strategy/mage/GenericMageNonCombatStrategy.cpp",
             'NextAction("arcane intellect", 21.0f)', 21.0),
            ("ai/playerbot/strategy/mage/GenericMageNonCombatStrategy.cpp",
             'NextAction("arcane intellect on party", 20.0f)', 20.0),
            ("ai/playerbot/strategy/shaman/ElementalShamanStrategy.cpp",
             'NextAction("windfury totem", ACTION_HIGH)', 20.0),
            ("ai/playerbot/strategy/rogue/GenericRogueNonCombatStrategy.cpp",
             'NextAction("apply instant poison main hand", 20.0f)', 20.0),
            ("ai/playerbot/strategy/druid/GenericDruidNonCombatStrategy.cpp",
             'NextAction("mark of the wild", 14.0f)', 14.0),
            ("ai/playerbot/strategy/priest/PriestStrategy.cpp",
             'NextAction("prayer of fortitude on party", ACTION_NORMAL + 3)', 13.0),
            ("ai/playerbot/strategy/priest/PriestStrategy.cpp",
             'NextAction("power word: fortitude", ACTION_NORMAL + 1)', 11.0),
            ("ai/playerbot/strategy/priest/PriestStrategy.cpp",
             'NextAction("shadow protection", ACTION_NORMAL + 1)', 11.0),
        ]
        for path, fragment, value in competitors:
            self.assertIn(fragment, (ROOT / path).read_text(), path)
            self.assertLess(value, priority, "%s: %s must lose to the drink" % (path, fragment))
        # ACTION_MOVE (30) still owns a moving tick, and the real heal rows
        # (ACTION_LIGHT_HEAL, 60, and up) stay above.
        self.assertLess(priority, 30.0)

        # The named case: a priest at 5 % with missing Fortitude and Shadow
        # Protection. Both rows sit in the same non-combat engine as the
        # drink; the engine executes the highest relevance, so the drink is
        # chosen first.
        priest = strip_comments((ROOT / "ai/playerbot/strategy/priest/PriestStrategy.cpp").read_text())
        buffs = body_after(priest, "void PriestBuffStrategy::InitNonCombatTriggers")
        self.assertIn('"power word: fortitude"', buffs)
        self.assertIn('"shadow protection"', buffs)
        queued = [("drink", priority), ("power word: fortitude", 11.0), ("shadow protection", 11.0)]
        self.assertEqual("drink", max(queued, key=lambda row: row[1])[0])

    def test_low_mana_drink_yields_to_a_party_cleanse(self):
        """A druid at 15 % mana with a poisoned party member cleanses, then drinks."""
        trigger = " ".join(body_after(SRC_GENERIC_TRIGGERS, "bool LowManaDrinkTrigger::IsActive()").split())
        self.assertIn("LowManaDrinkUseful(", trigger)
        self.assertIn("PartyCleansePending(", trigger)

        # The cleanse gate reads the cure triggers' own scan in its
        # claim-free form, not a second party scan, for every dispel type a
        # party cure of this module removes.
        helper = " ".join(body_after(SRC_GENERIC_TRIGGERS, "bool PartyCleansePending(").split())
        self.assertIn('"party member to dispel peek"', helper)
        table = " ".join(span_after(SRC_GENERIC_TRIGGERS, "PartyCureSpell const kPartyCureSpells[]").split())
        for dispel in ("DISPEL_POISON", "DISPEL_DISEASE", "DISPEL_CURSE", "DISPEL_MAGIC"):
            self.assertIn(dispel, table)
        # Only a cure whose spell the bot knows may hold the drink back: the
        # known rank resolves, so a warrior, or a paladin without Cleanse,
        # still drinks.
        self.assertIn('"spell id"', helper)

        # The scenario. The druid's out-of-combat abolish poison row is 20.0
        # and the plain "high mana" drink row 6.0; the boosted low-mana row is
        # above both, which is exactly the defect the gate closes.
        druid = strip_comments((ROOT / "ai/playerbot/strategy/druid/GenericDruidNonCombatStrategy.cpp").read_text())
        self.assertIn('NextAction("abolish poison on party", 20.0f)', druid)
        food = " ".join(body_after(SRC_FOOD, "void UseFoodStrategy::InitNonCombatTriggers").split())
        self.assertIn('"high mana", NextAction::array(0, new NextAction("drink", 6.0f), NULL)', food)
        boosted = float(re.search(r"LOW_MANA_DRINK_PRIORITY\s*=\s*([0-9.]+)f", SRC_SURVIVE).group(1))
        self.assertGreater(boosted, 20.0, "a dry druid's drink used to outrank the cleanse")
        # The gate drops the boosted row while the member is poisoned, so the
        # cleanse is now the highest useful row; the plain drink follows it.
        queued = [("abolish poison on party", 20.0), ("drink", 6.0)]
        self.assertEqual("abolish poison on party", max(queued, key=lambda row: row[1])[0])

        # And the pure rule behind it: at 15 % the drink is wanted, but not
        # while the cleanse is pending.
        policy = " ".join(body_after(SRC_SURVIVE, "inline bool LowManaDrinkUseful").split())
        self.assertIn("DrinkOutranksSelfBuffs(manaPct)", policy)
        self.assertIn("!partyCleansePending", policy)

    def test_low_mana_drink_check_registers_no_dispel_claim(self):
        """The drink gate may not reserve a target the drinker cannot cure."""
        helper = " ".join(body_after(SRC_GENERIC_TRIGGERS, "bool PartyCleansePending(").split())
        # The non-claiming scan: never the value the cleanse decision claims.
        self.assertIn('"party member to dispel peek"', helper)
        self.assertNotIn('"party member to dispel"', helper)
        # Registered as a claim-free instance of the same scan.
        self.assertIn('creators["party member to dispel peek"]', SRC_VALUE_CONTEXT)
        self.assertRegex(
            " ".join(body_after(SRC_VALUE_CONTEXT, 'creators["party member to dispel peek"]').split()),
            r'PartyMemberToDispel\(ai, "party member to dispel peek",\s*false\)')

        # Only the actual cleanse decision claims and logs its pick.
        value = strip_comments((ROOT / "ai/playerbot/strategy/values/PartyMemberToDispel.cpp").read_text())
        calc = " ".join(body_after(value, "Unit* PartyMemberToDispel::Calculate()").split())
        self.assertIn("if (claim && target && target == predicate.GetPick()", calc)
        self.assertIn("if (claim && target && bot->GetGroup()) BuffClaimRegistry::Claim(", calc)

        # The read-only peek writes nothing to the decision trail either: the
        # row that excludes a member is the claiming cleanse's to report.
        self.assertIn("PartyMemberToDispelPredicate predicate(ai, dispelType, claimKey, dispelRows, claim)",
                      calc)
        predicate = " ".join(body_after(value, "virtual bool Check(Unit* unit)").split())
        self.assertIn("if (logExclusions) LogDispelExcluded(", predicate)

        # The gate decides castability with the module's real cast check, the
        # same one the cure action runs: a judged-only-known spell is not
        # castable while the bot is stunned, mounted, silenced or locked out.
        self.assertIn("ai->CanCastSpell(", helper)
        self.assertNotIn("ai::CureCastableNow(", helper)
        self.assertIn("CanCastSpell(spellId, member, 0, true, nullptr, true, false, false, &reason)", helper)

        # The model of the dry-but-castable case: the drink gate yields.
        self.assertFalse(model_low_mana_drink_useful(15, model_party_cleanse_pending(True, True)))

    def test_low_mana_drink_blocked_when_the_cure_is_castable(self):
        """A druid at 15 % mana can cast the cure: the drink yields to it."""
        helper = " ".join(body_after(SRC_GENERIC_TRIGGERS, "bool PartyCleansePending(").split())
        # The candidate the peek found is the one probed, and the probe is the
        # cure action's own check: no range gate here (the cure queues its
        # reach prerequisite), the real caster state decides.
        self.assertIn("CanCastSpell(spellId, member, 0, true, nullptr, true, false, false, &reason)", helper)

        # 15 % of 1000 mana, off cooldown, nothing blocking: the cleanse is
        # pending, so the boosted row is not useful and the cleanse row (20.0)
        # owns the tick. The plain drink row (6.0) follows it.
        self.assertFalse(model_low_mana_drink_useful(15, model_party_cleanse_pending(True, True)))
        queued = [("abolish poison on party", 20.0), ("drink", 6.0)]
        self.assertEqual("abolish poison on party", max(queued, key=lambda row: row[1])[0])

        # Silenced (or mounted, stunned, locked out of the school, in a form
        # the spell refuses): the member still needs the cure, but the cast is
        # refused, so the drink is allowed instead of idling on a cleanse that
        # cannot run. On the nominal gate alone this case held the drink.
        self.assertFalse(model_party_cleanse_pending(True, False))
        self.assertTrue(model_low_mana_drink_useful(15, model_party_cleanse_pending(True, False)))


if __name__ == "__main__":
    unittest.main()
