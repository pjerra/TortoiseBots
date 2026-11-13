"""Wiring pins for tank_first (r13, AiPlayerbot.DungeonRunAddTankFirst).

The rule itself is ai/playerbot/TankFirstPolicy.h (tools/test_tank_first_policy.cpp). These pins prove the live code
reaches it and that the key-off path stays today's code:

 - the key is read with the other DungeonRun keys, defaults to off and is documented in the template;
 - BossTankFirstHolds filters cheapest-first: the key, the map flag, a tank, the strategy, the unit, the row;
 - the per-add record's mutex is taken after the fight scan (it is a leaf), and the clock is read under it, in the read
   and in the sweep;
 - the hold-kill action, HeldDpsTarget, FindBossHoldKill and FindBossSeekKill ask the hold, and the pet and the bot's
   own swing/shot stop in PlayerbotAI::UpdateAI (any non-tank, ordered bots and healers included);
 - an override's release row starts on the phase (one per add and reason), and the take's why is built only after the
   key;
 - the instance's records and its sweep clock are pruned once nobody read that instance for ten minutes;

Run: python3 -m unittest tools/test_tank_first_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
POLICY = (ROOT / "ai/playerbot/TankFirstPolicy.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
DPS_TARGET = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def index_in(text, needle):
    return text.index(needle)


class TankFirstWiring(unittest.TestCase):
    def test_the_key_is_read_off_by_default_and_documented(self):
        keys = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('dungeonRunAddTankFirst = config.GetBoolDefault("AiPlayerbot.DungeonRunAddTankFirst", false);',
                      keys)
        self.assertIn("bool dungeonRunAddTankFirst = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunAddTankFirst = 0", TEMPLATE)
        self.assertIn("# Tank the add first", TEMPLATE)

    def test_the_read_filters_cheapest_first(self):
        holds = body(ACTIONS, "bool ai::BossTankFirstHolds(PlayerbotAI* ai, Player* bot, Unit* unit)")
        order = ["!sPlayerbotAIConfig.dungeonRunAddTankFirst",
                 "GetBossTactics().HasTankFirstMap(bot->GetMapId())",
                 "PlayerbotAI::IsTank(bot)",
                 'ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)',
                 "unit->ToCreature()",
                 "unit->IsInCombat()"]
        positions = [index_in(holds, needle) for needle in order]
        self.assertEqual(positions, sorted(positions), "the read's cheapest-first order moved")
        # The row and the victim read live behind the per-tick cache, in TankFirstRead.
        read = body(ACTIONS, "bool TankFirstRead(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("candidate->kind == BossTacticKind::TankFirst && candidate->targetEntry == unit->GetEntry()",
                      read)
        self.assertIn("TankFirstTickDue(slot.record, nowMs)", read)
        self.assertIn("TankFirstTick(slot.record, rules, in)", read)
        self.assertIn("TankFirstHolds(slot.record, overrideWhy)", read)
        self.assertIn("TankFirstSweep(ai, bot);", read)
        # The clock is sampled under the record's lock, after the scan and the seek read (the mutex is a leaf).
        self.assertLess(index_in(read, "ScanBossTactics("), index_in(read, "g_tankFirstMutex"))
        self.assertLess(index_in(read, "ReadSeekFightCached("), index_in(read, "g_tankFirstMutex"))
        self.assertLess(index_in(read, "std::lock_guard<std::mutex> lock(g_tankFirstMutex);"),
                        index_in(read, "WorldTimer::getMSTime()"))

    def test_the_ages_survive_the_clock_wrap(self):
        # Two hazards: a read's clock sample can predate a stamp another thread wrote, and any stamp can predate the
        # clock's ~49.7-day wrap. The age is the modulo difference (WorldTimer::getMSTimeDiff's arithmetic) and a stamp
        # newer than the read clamps to 0.
        self.assertIn("inline std::uint32_t TankFirstAgeMs(std::uint32_t nowMs, std::uint32_t stampMs)", POLICY)
        self.assertIn("const std::uint32_t age = nowMs - stampMs;", POLICY)
        self.assertIn("return age > UINT32_MAX / 2 ? 0 : age;", POLICY)
        for raw in ("now - record.", "nowMs - r.", "nowMs - it->second"):
            self.assertNotIn(raw, POLICY)
        self.assertIn("record.tankMs += elapsed;", POLICY)
        self.assertIn("record.tankMs >= rules.dwellMs", POLICY)
        self.assertIn("TankFirstAgeMs(now, record.nonTankSinceMs) >= kTankFirstFlipMs", POLICY)
        self.assertIn("rules.capMs + rules.dwellMs", POLICY)
        self.assertIn("TankFirstAgeMs(now, record.episodeMs) >= rules.capMs", POLICY)
        self.assertIn("TankFirstAgeMs(nowMs, r.tickMs) >= kTankFirstTickMs", POLICY)
        self.assertIn("return TankFirstAgeMs(nowMs, seenMs) > kTankFirstStaleMs;", POLICY)
        sweep = body(ACTIONS, "void TankFirstSweep(PlayerbotAI* ai, Player* bot)")
        self.assertLess(index_in(sweep, "std::lock_guard<std::mutex> lock(g_tankFirstMutex);"),
                        index_in(sweep, "WorldTimer::getMSTime()"))
        self.assertIn("TankFirstStale(nowMs, it->second.record.seenMs)", sweep)
        self.assertIn("end.lifeMs = TankFirstAgeMs(nowMs, it->second.record.firstMs);", sweep)

    def test_the_hooks_ask_the_hold(self):
        self.assertIn("if (BossTankFirstHolds(ai, bot, unit))", body(DPS_TARGET, "bool HeldDpsTarget("))
        holdKill = body(ACTIONS, "Unit* ai::FindBossHoldKill(")
        self.assertIn("if (BossTankFirstHolds(ai, bot, current))", holdKill)
        # The drop comes before the seek block (the seek hold's own read).
        self.assertLess(index_in(holdKill, "BossTankFirstHolds("), index_in(holdKill, "if (sPlayerbotAIConfig.dungeonRunSeekAura)"))
        seekKill = body(ACTIONS, "Unit* ai::FindBossSeekKill(")
        self.assertIn("if (BossTankFirstHolds(ai, bot, state.source))", seekKill)
        self.assertLess(index_in(seekKill, "SeekHoldsUnit(state, state.source)"),
                        index_in(seekKill, "BossTankFirstHolds("))
        action = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("const bool tankFirstHeld = BossTankFirstHolds(ai, bot, held);", action)
        self.assertEqual(2, action.count("!seekHeld && !tankFirstHeld"))
        self.assertIn("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL)", action)
        self.assertIn("bot->InterruptNonMeleeSpells(false)", action)
        self.assertIn("if (tankFirstHeld)", action)
        # The override's release row starts on the phase, never on the hold (an override holds nothing): one row per
        # add and reason.
        read = body(ACTIONS, "bool TankFirstRead(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("overrideStarted = TankFirstOverrideStarted(overrideWhy, slot.overrideWhy, slot.record.phase);",
                      read)
        self.assertIn("slot.overrideWhy = overrideWhy;", read)

    def test_the_pet_hold_runs_in_the_update_loop(self):
        update = body(PLAYERBOT_AI, "void PlayerbotAI::UpdateAI(")
        self.assertIn("ai::HoldTankFirstPet(this, bot);", update)
        self.assertIn("ai::HoldTankFirstDamage(this, bot);", update)
        self.assertLess(index_in(update, "ai::ReleaseWalkoutPet(bot);"), index_in(update, "ai::HoldTankFirstPet(this, bot);"))
        self.assertLess(index_in(update, "ai::HoldTankFirstPet(this, bot);"),
                        index_in(update, "ai::HoldTankFirstDamage(this, bot);"))
        self.assertLess(index_in(update, "ai::HoldTankFirstDamage(this, bot);"), index_in(update, "++aiUpdateTick;"))
        pet = body(ACTIONS, "void ai::HoldTankFirstPet(")
        self.assertIn("pet->AttackStop();", pet)
        self.assertIn('LogHoldKillVeto(ai, bot, "tank_first_pet", victim);', pet)

    def test_the_bot_drop_reaches_every_non_tank(self):
        # An explicit attack order and a healer keep swinging or shooting a held add: the drop runs for every non-tank
        # from the update loop, before the engine and outside the free-DPS pick.
        drop = body(ACTIONS, "void ai::HoldTankFirstDamage(")
        self.assertIn("PlayerbotAI::IsTank(bot)", drop)
        self.assertIn('context->GetValue<Unit*>("current target")', drop)
        self.assertIn("if (!target || !BossTankFirstHolds(ai, bot, target))", drop)
        self.assertIn("bot->AttackStop();", drop)
        self.assertIn("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL)", drop)
        self.assertIn("bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)", drop)
        for gate in ("IsFreeDps", "IsHeal", "IsBattleDps", "FindBossHoldKill"):
            self.assertNotIn(gate, drop)
        self.assertNotIn("HoldTankFirstDamage", body(ACTIONS, "Unit* ai::FindBossHoldKill("))
        self.assertIn("void HoldTankFirstDamage(PlayerbotAI* ai, Player* bot);", ACTIONS_H)

    def test_the_instance_records_are_pruned(self):
        self.assertIn("constexpr std::uint32_t kTankFirstPruneMs = 600000;", POLICY)
        sweep = body(ACTIONS, "void TankFirstSweep(PlayerbotAI* ai, Player* bot)")
        self.assertIn("TankFirstAgeMs(nowMs, it->second) <= kTankFirstPruneMs", sweep)
        self.assertIn("it = g_tankFirstSweepMs.erase(it);", sweep)
        self.assertIn("rec = rec->first.first == gone ? g_tankFirst.erase(rec) : std::next(rec);", sweep)

    def test_the_multiplier_names_the_new_flags(self):
        multiplier = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("const bool tankFirst = sPlayerbotAIConfig.dungeonRunAddTankFirst && "
                      "GetBossTactics().HasTankFirstMap(bot->GetMapId()) && !PlayerbotAI::IsTank(bot) && "
                      'ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT);', multiplier)
        first_return = multiplier[multiplier.index("if (other && !GetBossTactics().HasHoldMap"):]
        first_return = first_return[:first_return.index("return 1.0f;")]
        self.assertIn("&& !tankFirst", first_return)
        second_return = multiplier[multiplier.index("if (!dpsAssist && !tankAssist && !aoe"):]
        second_return = second_return[:second_return.index("return 1.0f;")]
        self.assertIn("!tfSingle && !tfCast", second_return)
        self.assertIn("const bool tfSingle = tankFirst && !area && IsSingleTargetDamageAction(action);", multiplier)
        self.assertIn("const bool tfCast = tankFirst && IsHarmfulTargetedAction(action) &&", multiplier)
        self.assertIn("|| tankFirst) && IsAreaDamageAction(ai, action);", multiplier)
        # The clause sits after the seek hold's and before the hold_kill clauses.
        clause_start = multiplier.index("// tank_first (AiPlayerbot.DungeonRunAddTankFirst): no bot but a tank damages")
        clause_end = multiplier.index("// A held subject is left alone by every bot that is not a tank or a healer")
        self.assertLess(clause_start, clause_end)
        self.assertIn("if (IsBattleDps(bot) && (area || attackerDebuff))", multiplier)
        self.assertLess(clause_end, index_in(multiplier, "if (IsBattleDps(bot) && (area || attackerDebuff))"))
        self.assertLess(index_in(multiplier, "if (seeksRow && (dpsAssist"), clause_start)
        clause = multiplier[clause_start:clause_end]
        self.assertNotIn("tankAssist", clause)
        self.assertIn('LogHoldKillVeto(ai, bot, "tank_first", heldAdd);', clause)
        self.assertIn("BossTankFirstHeldNear(ai, bot,", clause)

    def test_the_take_notes_its_taunt(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        note = take.index("BossTankFirstNoteTake(ai, bot, task.source, takeSrc.c_str(), cast")
        cast = take.index("const bool cast = ai->CastSpell(taunt, task.source);")
        self.assertLess(cast, note)
        # ... and before the cast, the take that will cast nothing says why (the gate's, the tank's own refusal, the
        # cast result). The why reads auras, cooldowns and the result, so it is built only after the key and the map
        # flag.
        self.assertIn("TankFirstTauntWhyNow(ai, bot, taunt, gateRefused, castResult, tankFirstLease", take)
        self.assertLess(index_in(take, "const bool tankFirstTake = sPlayerbotAIConfig.dungeonRunAddTankFirst"),
                        index_in(take, "TankFirstTauntWhyNow(ai, bot, taunt, gateRefused, castResult, tankFirstLease"))
        why = body(ACTIONS, "const char* TankFirstTauntWhyNow(")
        self.assertIn('return "gate";', why)
        self.assertIn('return "no_taunt";', why)
        self.assertIn('return "control";', why)
        self.assertIn('return "stance";', why)
        self.assertIn("TankFirstTauntWhyOf(static_cast<int>(result), leased, spellCooldown)", why)
        self.assertIn('snprintf(noWord, noWordSize, "res=%d", static_cast<int>(result));', why)
        # R4: the stance refusal is a cast of its own, the taunt's next tick; R2: an out-of-range or out-of-sight taunt
        # with the lease walks (or paths), the tick spent.
        self.assertIn('std::strcmp(takeWhy, "stance") == 0 && TankFirstTakeForm(ai, bot)', take)
        self.assertIn('std::strcmp(takeWhy, "walk") == 0', take)
        self.assertIn("return ChaseTo(task.source, ATTACK_DISTANCE - 1.0f);", take)
        form = body(ACTIONS, "bool TankFirstTakeForm(")
        self.assertIn('ai->CastSpell("defensive stance", bot)', form)
        self.assertIn('ai->CastSpell("bear form", bot) || ai->CastSpell("dire bear form", bot)', form)
        # R2: the cast in reach stops and turns first (the core's facing check), before the taunt goes out.
        stop = take.index("ai->StopMoving();", take.index("R2: the taunt needs the Son in front"))
        face = take.index("sServerFacade.SetFacingTo(bot, task.source, true);", take.index("R2: the taunt needs the Son in front"))
        self.assertLess(stop, face)
        self.assertLess(face, cast)

    def test_the_lease_excuses_the_take_chase(self):
        # R1: the take's own read and its lease come before the walk and the Attack's chase, and both the chase gate
        # and the anchor's refusal read TankFirstChaseExempt beside MeetSonChaseExempt. Hakkar's tank is kept out by the
        # boss-victim test, which is the pure policy's (TankFirstChaseLeaseOk).
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(")
        holding = take.index("BossTankFirstTakeHolding(ai, bot, task.source)")
        lease = take.index("TankFirstChaseLeaseOk(true, !chaseRefused,", holding)
        grant = take.index("TankFirstGrantChase(bot, task.source);", lease)
        self.assertLess(holding, lease)
        self.assertLess(lease, grant)
        self.assertLess(grant, take.index("ChaseTo(task.source, ATTACK_DISTANCE - 1.0f);"))
        self.assertIn("task.boss != nullptr && task.boss->GetVictim() == bot", take)
        refusal = body(ACTIONS, "bool ai::BossAnchorRefusesChase(")
        self.assertIn("if (MeetSonChaseExempt(ai, bot, unit) || TankFirstChaseExempt(bot, unit))", refusal)
        movement = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        chase = body(movement, "bool MovementAction::ChaseTo(")
        self.assertLess(index_in(chase, "MeetSonChaseExempt(ai, bot, chased)"),
                        index_in(chase, "TankFirstChaseExempt(bot, chased)"))
        self.assertIn("const bool anchored = LiveBossAnchor(ai, bot, anchor) &&", chase)
        # The lease file: its own leaf mutex, and the age helper for the lapsed test.
        grant_body = body(ACTIONS, "void ai::TankFirstGrantChase(")
        self.assertIn("std::lock_guard<std::mutex> lock(g_tankFirstLeaseMutex);", grant_body)
        exempt = body(ACTIONS, "bool ai::TankFirstChaseExempt(")
        self.assertIn("TankFirstChaseLeaseLive(it->second.writtenMs, WorldTimer::getMSTime())", exempt)
        self.assertIn("g_tankFirstLease.erase(it);", exempt)
        self.assertIn("bool BossTankFirstTakeHolding(PlayerbotAI* ai, Player* bot, Unit* add);", ACTIONS_H)
        self.assertIn("void TankFirstGrantChase(Player* bot, Unit* add);", ACTIONS_H)
        self.assertIn("bool TankFirstChaseExempt(Player* bot, Unit* target);", ACTIONS_H)

    def test_the_tank_advances_the_record(self):
        # R8: a take, cast or not, advances the add's record (the victim read, the throttle, the row), so a landed
        # taunt settles without a non-tank's read and without waiting for one.
        note = body(ACTIONS, "void ai::BossTankFirstNoteTake(")
        self.assertIn("TankFirstRead(ai, bot, add);", note)
        holding = body(ACTIONS, "bool ai::BossTankFirstTakeHolding(")
        self.assertIn("TankFirstRead(ai, bot, add);", holding)
        self.assertIn("TankFirstPhaseHolds(it->second.record.phase)", holding)
        # The phase only, never the hold: an override frees the add from damage, not from the take's own walk.
        self.assertNotIn("TankFirstHolds(", holding)

    def test_the_policy_header_names_no_boss(self):
        for needle in ("11357", "14834", "24324"):
            self.assertNotIn(needle, POLICY)
        self.assertIn("constexpr std::uint32_t kTankFirstTickMs = 250;", POLICY)
        self.assertIn("constexpr std::uint32_t kTankFirstStaleMs = 10000;", POLICY)
        self.assertIn("constexpr float kTankFirstTankReachYd = 100.0f;", POLICY)
        self.assertIn("constexpr int kTankFirstSiphonMs = 15000;", POLICY)
        self.assertIn("constexpr float kTankFirstLossMinHpPct = 20.0f;", POLICY)

    def test_the_row_kind_is_registered(self):
        # The table (BossTacticsPolicy.h) parses the word, refuses a bad shape and keeps the map flag and the entry's
        # boss list the run's healer guard reads (part W).
        policy = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
        self.assertIn('if (text == "tank_first")', policy)
        self.assertIn("TankFirst = 32,", policy)
        self.assertIn("bool HasTankFirstMap(std::uint32_t map) const", policy)
        self.assertIn("const std::vector<std::uint32_t>& TankFirstBosses(std::uint32_t map, std::uint32_t entry) const",
                      policy)
        add = body(policy, "bool Add(const BossTacticRow& row)")
        self.assertIn("row.targetEntry == row.bossEntry", add)
        self.assertIn("row.param < 1.0f || row.param > 10.0f", add)
        self.assertIn("row.windowSec < 5.0f || row.windowSec > 60.0f", add)
        self.assertIn("tankFirstMaps_[row.map] = true;", add)
        self.assertIn("tankFirstBosses_[row.map][row.targetEntry].push_back(row.bossEntry);", add)
        # The declarations the two other translation units call.
        self.assertIn("bool BossTankFirstHolds(PlayerbotAI* ai, Player* bot, Unit* unit);", ACTIONS_H)
        self.assertIn("Unit* BossTankFirstHeldNear(PlayerbotAI* ai, Player* bot, Unit* around, float yd);", ACTIONS_H)
        self.assertIn("void BossTankFirstNoteTake(", ACTIONS_H)
        self.assertIn("void HoldTankFirstPet(PlayerbotAI* ai, Player* bot);", ACTIONS_H)


if __name__ == "__main__":
    unittest.main()
