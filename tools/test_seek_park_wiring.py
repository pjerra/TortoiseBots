"""Wiring pins for the seek park (AiPlayerbot.DungeonRunSeekPark, r11-1).

The pure rules live in ai/playerbot/SeekParkPolicy.h and are tested in tools/test_seek_park_policy.cpp. These pins hold
the call sites: the tank pick reads the key before today's body and vetoes the parked source, the hold answers for the
parked source before the meet and the 20 s cap (so the DoT and area vetoes and the damage hold cover it, the park bot
included), the fight read steps the park before the burn's lead, the trigger never consumes what its action needs, the
strategy queues the action once in the combat block, and both contexts register the name.
Run: python3 -m unittest tools/test_seek_park_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/SeekParkPolicy.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
DUNGEON_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/DungeonTriggers.cpp").read_text()
POSSIBLE_TARGETS = (ROOT / "ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp").read_text()
TAKE_SRC = (ROOT / "tools/test_seek_take_src_wiring.py").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekParkWiring(unittest.TestCase):
    def test_the_key_off_is_todays_walk_out(self):
        # The key is read before the row, so with it off no seek_park row is even looked up, the fight read resets the
        # record and never steps the machine, and the burn lead is the seek_pull row's own.
        rules = body(ACTIONS, "SeekParkRules SeekParkRulesOf(std::uint32_t mapId, const BossTacticRow* seekRow)")
        self.assertLess(rules.index("dungeonRunSeekPark"), rules.index("SeekParkRow("))
        self.assertIn("return rules;", rules)
        read = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertLess(read.index("if (!rules.on)"), read.index("SeekParkCommit("))
        # The pure rules keep today's lead with the key off, whatever the phase says (TestParkNeedsRoom,
        # TestParkHeldUntilReleaseLead in tools/test_seek_park_policy.cpp).
        self.assertIn("if (!rulesOn || !parkedIsPick)", POLICY)

    def test_the_tank_pick_reads_the_key_before_todays_body(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertLess(fn.index("dungeonRunSeekPark"), fn.index("FindBossSeekTankUnparked("))
        self.assertIn("SeekParkVetoesTankTask(", fn)

    def test_todays_tank_body_does_not_read_the_key(self):
        fn = body(ACTIONS, "SeekTankTask FindBossSeekTankUnparked(PlayerbotAI* ai, Player* bot, std::uint64_t")
        self.assertNotIn("dungeonRunSeekPark", fn)

    def test_the_hold_answers_for_the_parked_source_first(self):
        fn = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertLess(fn.index("SeekParkHolds("), fn.index("SeekMeetHoldsUnit("))
        # The released source's own wait for a tank is the same answer.
        self.assertIn("SeekParkDpsHeld(", fn)
        # ...and the release row reads the same verdict.
        why = body(ACTIONS, "SeekHoldWhy SeekHoldWhyOfUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekParkHolds(", why)
        self.assertIn("SeekParkDpsHeld(", why)

    def test_the_receiver_may_hit_the_parked_son(self):
        # Round 7: the park's own strike is exempt from the seek hold beside the puller's latch, the receiver's in the
        # live release and the failed park's receiver or breaker; every other bot's damage on it stays held
        # (TestParkSourceHitAllowed).
        fn = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(Action* action)")
        self.assertIn("const bool pullHit = IsSeekSource(state, struck) &&", fn)
        self.assertIn("const bool parkHit = IsSeekSource(state, struck) &&", fn)
        self.assertLess(fn.index("const bool pullHit ="), fn.index("const bool parkHit ="))
        self.assertIn("SeekParkSourceHitAllowed(state.parkPhase, state.parkControlled,", fn)
        self.assertIn("state.parkReceiver == bot->GetObjectGuid().GetRawValue(),", fn)
        self.assertIn("state.parkBreaker == bot->GetObjectGuid().GetRawValue());", fn)
        self.assertIn("if (!pullHit && !parkHit)", fn)
        self.assertIn("inline bool SeekParkSourceHitAllowed(SeekParkPhase phase, bool controlled, bool isReceiver, "
                      "bool isBreaker)", POLICY)

    def test_the_dot_and_area_vetoes_read_that_hold(self):
        # Fact 2 of the probe: nobody damages a parked source, the controlling mage included. Both vetoes go through
        # SeekHoldsUnit, which answers true for the parked source.
        dot = body(ACTIONS, "bool SeekDotOnSource(const SeekFight& state, Action* action)")
        self.assertIn("SeekHoldsUnit(state, target)", dot)
        self.assertIn("SeekDotVeto(", dot)
        area = body(ACTIONS, "Unit* SeekHeldSourceInArea(const SeekFight& state, Unit* around)")
        self.assertIn("SeekHoldsUnit(state, son)", area)

    def test_the_fight_read_steps_the_park_before_the_burn(self):
        fn = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertLess(fn.index("UpdateSeekPark("), fn.index("SeekLateEngagedBurnWithin("))
        self.assertIn("SeekParkLateLeadMs(", fn)
        # The park record is keyed by the fight's engage clock as well as the cycle (no re-pull carry-over).
        self.assertIn("out.engageMs = engageMs;", fn)

    def test_the_park_is_the_seek_pick_inside_the_circle(self):
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("Unit* const picked = out.source;", fn)
        self.assertIn("LiveBossAnchorRow(", fn)
        self.assertIn("InsideAnchor(", fn)
        self.assertIn("in.pickedSon = pickedGuid;", fn)
        self.assertIn("in.fightStartMs = out.engageMs;", fn)
        self.assertIn("in.controllerInReach = controllerInReach;", fn)
        self.assertIn("in.sonOnTank =", fn)
        self.assertNotIn("assert(", fn)

    def test_the_record_owns_its_son(self):
        # R2: a live record is read by its own guid, never by the reader's own pick, and only its own facts step it.
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("const bool live = recorded.phase != SeekParkPhase::None", fn)
        self.assertIn("live ? recorded.parkBot : 0", fn)
        self.assertIn("live ? recorded.receiver : 0", fn)
        self.assertIn("out.parkPreWalk = SeekParkPreWalk(", fn)
        self.assertLess(fn.index("GetCreature(ObjectGuid(recorded.son))"), fn.index("SeekParkCommit("))
        self.assertNotIn("state.son != in.son", POLICY)

    def test_the_on_tank_read_ignores_a_stale_victim_under_the_control(self):
        # F1/R1 (round 5): the machine's one on-tank source needs the tank's own current target, not just the victim
        # the control froze. Both a bot's AI target and a player's selection are read.
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekParkSonOnTank(victimIsTank, tankEngaged, in.controlled)", fn)
        self.assertIn('GetValue<Unit*>("current target")->Get() == liveSon', fn)
        self.assertIn("GetSelectionGuid() == liveSon->GetObjectGuid()", fn)
        self.assertLess(fn.index("tankEngaged ="), fn.index("SeekParkCommit("))
        self.assertIn("inline bool SeekParkSonOnTank(bool victimIsTank, bool tankEngaged, bool controlled)", POLICY)
        self.assertIn("return victimIsTank && tankEngaged && !controlled;", POLICY)

    def test_the_hold_reads_the_park_on_tank_verdict(self):
        # The damage hold and the release row use the park's own read, not a fresh victim scan.
        fn = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekParkDpsHeld(state.parkPhase, state.parkSonOnTank,", fn)
        why = body(ACTIONS, "SeekHoldWhy SeekHoldWhyOfUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekParkDpsHeld(state.parkPhase, state.parkSonOnTank,", why)
        self.assertNotIn("SeekSonVictimIsTank(", ACTIONS)

    def test_the_tank_pick_reads_the_park_read_out(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("state.parkSonOnTank", fn)
        self.assertIn("state.parkReceiver", fn)
        self.assertNotIn("SeekSonVictimIsTank(", fn)
        self.assertNotIn("SeekParkReceiver(", fn)
        # The order the read runs in: the stand-down, then the receiver's walk, then the release's take, then today's
        # body (R5, R7, R9).
        self.assertLess(fn.index("SeekParkTankStandsDown("), fn.index("state.parkPreWalk"))
        self.assertLess(fn.index("state.parkPreWalk"), fn.index("SeekParkFailedTakeDue("))
        self.assertLess(fn.index("SeekParkFailedTakeDue("), fn.index("SeekParkReleaseTakeDue("))
        self.assertLess(fn.index("SeekParkReleaseTakeDue("), fn.rindex("FindBossSeekTankUnparked("))

    def test_the_stand_down_stops_the_tank_attack(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        stand = fn[fn.index("if (task.standDown)"):]
        stand = stand[:stand.index("if (task.parkApproach)")]
        self.assertIn("AttackStop()", stand)
        self.assertIn('SET_AI_VALUE(Unit*, "current target", nullptr);', stand)
        self.assertIn("park_hold", stand)
        approach = fn[fn.index("if (task.parkApproach)"):]
        approach = approach[:approach.index("if (task.walkout)")]
        self.assertIn("SeekParkStrikeInReach(", approach)
        self.assertIn("walkToParkSon(task.source, walk)", approach)
        # The walk is clamped to the anchor and the same-walk check keeps it from re-sending every tick.
        self.assertIn("ClampMoveToBossAnchor(ai, bot, x, y, z)", fn)
        self.assertLess(fn.index("SeekParkStrikeInReach("), fn.index("BossAnchorRefusesChase("))
        self.assertIn("park_release", fn)

    def test_the_park_cast_row(self):
        fn = body(ACTIONS, "bool BossTacticsParkAction::Execute(Event& event)")
        self.assertIn("LogSeekParkCast(", fn)
        self.assertIn('"refused"', fn)
        self.assertIn('"walk"', fn)
        self.assertIn('"clamp"', fn)
        log = body(ACTIONS, "void LogSeekParkCast(PlayerbotAI* ai, Player* bot, Unit* son, const char* step,")
        self.assertIn("seek park cast: by=%s son=%u step=%s yd=%.0f los=%d check=%u", log)

    def test_the_release_needs_a_tank_victim(self):
        # Decision 2 with R1's one read: the release fails after its window unless the source is on a tank with no
        # breakable control; a tank inside the window latches success instead (TestParkReleaseIgnoresStaleTankVictim,
        # TestParkReleaseLatches, TestParkReleaseWindow).
        self.assertIn("nowMs - state.releaseMs >= SeekParkReleaseWindowMs(in.receiverOk, in.controlled)", POLICY)
        self.assertIn("inline std::uint32_t SeekParkReleaseWindowMs(bool receiverOk, bool controlled)", POLICY)
        self.assertIn("return receiverOk && controlled ? kSeekParkReleaseMaxMs : kSeekParkReleaseHoldMs;", POLICY)
        check = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("state.parkSonOnTank", check)

    def test_the_release_target_exemption_is_narrow(self):
        # Decision 3: the receiver's released source is no invalid target while the release is live (open or latched);
        # nothing else. The receiver is the record's, so no second scan here.
        fn = body(ACTIONS, "bool ai::BossSeekParkReleaseTargetAllowed(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("!SeekParkReleaseLive(state.parkPhase)", fn)
        self.assertIn("state.parkReceiver == bot->GetObjectGuid().GetRawValue()", fn)
        self.assertNotIn("SeekParkReceiver(", fn)
        self.assertIn("SeekParkReleaseTargetAllowed(", fn)
        fn = body(POSSIBLE_TARGETS, "bool PossibleAttackTargetsValue::IsPossibleTarget(")
        self.assertIn("BossSeekParkReleaseTargetAllowed(", fn)
        self.assertIn("HasBreakableCC(", fn)

    def test_the_body_skips_the_parked_source_for_the_next_task(self):
        # Decision 5: the wrapper passes the parked guid the body must skip, so a second Son on a healer still gets a
        # tank instead of an empty task.
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkExcludesSource(", fn)
        self.assertIn("FindBossSeekTankUnparked(ai, bot, excludeParkSon ? state.parkSon : 0)", fn)
        fn = body(ACTIONS, "SeekTankTask FindBossSeekTankUnparked(PlayerbotAI* ai, Player* bot, std::uint64_t")
        self.assertIn("excludedSon", fn)
        self.assertIn("if (excluded(state.healerSon))", fn)
        self.assertIn("if (excluded(candidate))", fn)
        self.assertIn("if (!excluded(reliefSon))", fn)
        self.assertIn("if (!excluded(nearestSon))", fn)

    def test_a_failed_controlled_park_keeps_its_take(self):
        # Fix 2: a park that failed while the Son is still sheeped still breaks it: the release's receiver keeps its
        # take, and with none the body's ordinary takes apply (a failed park vetoes nothing).
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkFailedTakeDue(state.parkPhase, state.parkControlled, isReceiver)", fn)
        self.assertIn("inline bool SeekParkFailedTakeDue(SeekParkPhase phase, bool controlled, bool isReceiver)",
                      POLICY)
        self.assertIn("return phase == SeekParkPhase::Failed && controlled && isReceiver;", POLICY)
        read = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("readPhase != SeekParkPhase::Failed || controlled", read)
        self.assertIn("out.parkControlled = in.controlled;", read)
        target = body(ACTIONS, "bool ai::BossSeekParkReleaseTargetAllowed(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("SeekParkFailedTakeDue(state.parkPhase, state.parkControlled, true)", target)
        self.assertIn("state.parkControlled);", target)

    def test_a_failed_park_with_no_receiver_latches_its_breaker(self):
        # A failed park whose Son is still controlled and whose receiver pick came up empty hands the break to the
        # first ordinary off-tank that takes it: that taker is allowed as the sheeped Son's target through the walk
        # and the attack, kept while it qualifies, and dropped on the control's loss, death, charm or the boss's pick.
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkBreakerDue(state.parkPhase, state.parkControlled, state.parkReceiver != 0,", fn)
        self.assertIn("NoteBossSeekParkBreaker(bot, state.parkSon)", fn)
        self.assertLess(fn.index("FindBossSeekTankUnparked(ai, bot, excludeParkSon ? state.parkSon : 0)"),
                        fn.index("SeekParkBreakerDue("))
        latch = body(ACTIONS, "void NoteBossSeekParkBreaker(Player* bot, std::uint64_t sonGuid)")
        self.assertIn("it->second.state.breaker != 0", latch)
        self.assertIn("it->second.state.phase != SeekParkPhase::Failed", latch)
        read = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekParkBreakerKeeps(readPhase, controlled, sServerFacade.IsAlive(member),", read)
        self.assertIn("SeekParkCommit(record, readRevision,", read)
        self.assertIn("++it->second.revision;", latch)
        self.assertIn("if (state.breaker == readBreaker)", POLICY)
        self.assertIn("out.parkBreaker = step.breaker;", read)
        target = body(ACTIONS, "bool ai::BossSeekParkReleaseTargetAllowed(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("state.parkBreaker == bot->GetObjectGuid().GetRawValue()", target)
        self.assertIn("inline bool SeekParkBreakerDue(SeekParkPhase phase, bool controlled, bool hasReceiver, "
                      "bool takesSource)", POLICY)
        self.assertIn("inline bool SeekParkBreakerKeeps(SeekParkPhase phase, bool controlled, bool alive, "
                      "bool charmed, bool bossVictim)", POLICY)
        self.assertIn("return phase == SeekParkPhase::Failed && controlled && (isReceiver || isBreaker);", POLICY)

    def test_the_receiver_window_needs_a_controlled_son(self):
        # Fix 3: the receiver-only exclusion and the 8 s window are for a controlled Son; awake, today's takes apply
        # at once and the window is today's 4 s.
        self.assertIn("!SeekParkReleaseLive(state.parkPhase) &&", ACTIONS)
        self.assertIn("phase == SeekParkPhase::Failed && controlled && (isReceiver || isBreaker)", POLICY)

    def test_the_receiver_pre_walk_yields_to_a_take(self):
        # Fix 4: the body read with the parked Son excluded outranks the approach walk when it owes this bot a take.
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        pre = fn[fn.index("state.parkPreWalk"):]
        pre = pre[:pre.index("SeekParkFailedTakeDue(")]
        self.assertIn("FindBossSeekTankUnparked(ai, bot, state.parkSon)", pre)
        self.assertIn("other.valid && other.take", pre)

    def test_the_park_walk_rows_have_their_own_throttle(self):
        # Fix 5: the stand-down and approach rows must not spend the park_release take's `seek take` window.
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = body(ACTIONS, "void LogSeekParkWalk(PlayerbotAI* ai, Player* bot, Unit* son, const char* reason, "
                             "const char* src,")
        self.assertIn("const char* walk)", walk)
        self.assertIn("g_seekParkWalkLog[", walk)
        self.assertNotIn("g_seekPullLog[", walk)
        self.assertIn("seek take: %s -> %u victim=%s reason=%s meet=%d src=%s reach_yd=%.1f beyond_yd=%.1f walk=%s",
                      walk)
        # Round 7: the approach row carries the walk's result, so a stalled receiver's own walk is readable.
        self.assertIn('LogSeekParkWalk(ai, bot, task.source, "stand_down", "park_hold", nullptr)', fn)
        approach = fn[fn.index("if (task.parkApproach)"):]
        approach = approach[:approach.index("if (task.walkout)")]
        self.assertLess(approach.index("walkToParkSon(task.source, walk)"),
                        approach.index('"approach", "park_approach", walk'))
        self.assertIn("LogSeekParkWalk(ai, bot, task.source, \"approach\", \"park_release\", walk)", fn)
        # The wait is clamped to the row's lead, so a lead under the wait still releases.
        self.assertIn("releaseLeadMs < kSeekParkReceiverWaitMs ? releaseLeadMs : kSeekParkReceiverWaitMs", POLICY)

    def test_a_break_names_the_breaker(self):
        # Round 7, data only: a Held -> Apply break writes one `seek park break:` row with the Son's victim, the units
        # attacking it and its periodic-damage auras.
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("step.phase == SeekParkPhase::Apply && beforePhase == SeekParkPhase::Held", fn)
        self.assertIn("LogSeekParkBreak(ai, bot, liveSon);", fn)
        log = body(ACTIONS, "void LogSeekParkBreak(PlayerbotAI* ai, Player* bot, Unit* son)")
        self.assertIn("seek park break: son=%u by=%s victim=%s attackers=%s auras=%s", log)
        self.assertIn("GetAttackers()", log)
        self.assertIn("IsPeriodicDamageAura(", log)
        self.assertIn("GetCasterGuid()", log)

    def test_the_latched_release_freezes_its_receiver(self):
        # Round 7: after the latch the receiver is read from the record, never re-picked, and the record keeps its
        # guid; the take itself is Release-only (TestParkReleasedHolderKeepsTheSon). Round 8: the freeze lives in the
        # merge, so a read that lands after the latch cannot write it either (TestParkStaleReadKeepsTheLatch).
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("if (readPhase == SeekParkPhase::Released && live)", fn)
        self.assertIn("map->GetPlayer(ObjectGuid(recorded.receiver))", fn)
        self.assertIn("if (state.phase != SeekParkPhase::Released)", POLICY)
        pick = fn[fn.index("Player* receiver = nullptr;"):]
        pick = pick[:pick.index("// The failed park's breaker")]
        self.assertLess(pick.index("readPhase == SeekParkPhase::Released"), pick.index("PickSeekParkReceiverOf("))

    def test_the_write_merges_against_the_locked_record(self):
        # Round 8: the receiver and breaker are computed outside the lock, so the record write goes through the pure
        # merge (SeekParkMergeRead / SeekParkPhaseRank, TestParkStaleReadKeepsTheLatch): a stale read never erases the
        # latched release's receiver or another read's breaker, and a record whose phase moved past the reader's is
        # left alone. The merge runs before the step, so a reader's own Held -> Release keeps its pick.
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekParkMergeRead(record.state, readPhase, readBreaker,", POLICY)
        self.assertLess(POLICY.index("SeekParkMergeRead(record.state, readPhase, readBreaker,"),
                        POLICY.index("SeekParkStep(record.state, rules, in, nowMs)"))
        self.assertNotIn("record.state.receiver =", fn)
        self.assertNotIn("record.state.breaker =", fn)

    def test_a_stale_tick_drops_its_update(self):
        # Round 9: the step's facts are read before the lock, so the record carries a revision every committed write
        # bumps. A reader captures it with its read and hands it to the commit; a record another tick has committed
        # since drops the whole update, so a stale Held read cannot resurrect a control another tick saw break
        # (TestParkStaleTickDropsTheUpdate, TestParkSingleReaderProgresses).
        fn = body(ACTIONS, "void UpdateSeekPark(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("readRevision = it->second.revision;", fn)
        self.assertIn("SeekParkCommit(record, readRevision,", fn)
        self.assertLess(fn.index("record.touchedMs = nowMs;"), fn.index("SeekParkCommit("))
        self.assertNotIn("SeekParkStep(record.state", fn)
        self.assertIn("if (record.revision != readRevision)", POLICY)
        self.assertIn("++record.revision;", POLICY)
        # Every other writer of the record bumps it too.
        cast = body(ACTIONS, "void ai::NoteBossSeekParkCast(Player* bot, std::uint32_t nowMs)")
        self.assertIn("++it->second.revision;", cast)
        key_off = fn[:fn.index("if (!rules.on)") + fn[fn.index("if (!rules.on)"):].index("return;")]
        self.assertIn("++it->second.revision;", key_off)

    def test_the_trigger_only_asks(self):
        fn = body(TRIGGERS, "bool BossTacticsParkTrigger::IsActive()")
        self.assertIn("FindBossSeekPark(", fn)
        self.assertNotIn("lastCastMs", fn)
        self.assertNotIn("SeekParkStep(", fn)

    def test_the_action_alone_notes_the_cast(self):
        fn = body(ACTIONS, "bool BossTacticsParkAction::Execute(Event& event)")
        self.assertIn("NoteBossSeekParkCast(", fn)

    def test_the_parked_source_starts_no_walk_out(self):
        fn = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("SeekParkPhaseOf(", fn)

    def test_the_park_bot_is_freed_of_its_slot_while_it_applies_or_holds(self):
        fn = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkExemptsBot(", fn)
        self.assertIn("(phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held)", POLICY)

    def test_the_legacy_spread_stands_down_while_the_park_holds(self):
        # Fact 5 of the probe: a step-away while the park bot walks to its source or channels the control would cut
        # the cast. The slot read's own Apply exemption is the other half (the test above).
        fn = body(ACTIONS, "bool ai::BossSeekParkHoldsBot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("dungeonRunSeekPark", fn)
        self.assertIn("SeekParkHolds(", fn)
        self.assertIn("state.parkBot == bot->GetObjectGuid().GetRawValue()", fn)
        trigger = body(DUNGEON_TRIGGERS, "bool RaidSpreadNeededTrigger::IsActive()")
        self.assertIn("BossSeekParkHoldsBot(", trigger)
        self.assertLess(trigger.index("BossSeekParkHoldsBot("), trigger.index("ShouldRaidSurvivalSpread("))
        self.assertLess(trigger.index("BossSeekParkHoldsBot("), trigger.index("FindBossSpreadSlot("))

    def test_the_release_take_walks_then_strikes(self):
        # R9: out of strike reach the receiver walks to the source; in reach the take attacks and taunts as today.
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkReleaseTakeDue(state.parkPhase, onTank, isReceiver)", fn)
        self.assertIn("task.takeSrc = \"park_release\";", fn)
        self.assertIn("receiverDrag", fn)

    def test_the_release_never_hands_the_son_to_another_tank(self):
        # Round 3 (walk 73) with R11: a tank that has the Son keeps it; in the open release only its own take touches
        # the Son, whoever holds it, and every other tank's park path skips it
        # (TestParkReleasedHolderKeepsTheSon, TestParkOnlyTheReceiverInRelease).
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("const bool sonHolder = onTank && son && son->GetVictim() == bot;", fn)
        self.assertIn("SeekParkReleaseTank(SeekParkReleaseLive(state.parkPhase), onTank, sonHolder, isReceiver)", fn)
        self.assertIn("SeekParkExcludesSource(state.parkPhase, onTank, releaseTank, state.parkControlled)", fn)
        self.assertIn("SeekParkVetoesTankTask(state.parkPhase, task.takeSrc, onTank, state.parkControlled)", fn)
        self.assertIn("SeekParkReleaseTakeDue(SeekParkPhase phase, bool sonOnTank, bool isReceiver)", POLICY)
        self.assertIn("return phase == SeekParkPhase::Release && !sonOnTank && isReceiver;", POLICY)
        self.assertIn("if (phase == SeekParkPhase::Release)", POLICY)
        self.assertIn("return controlled ? !isReleaseTank : (sonOnTank && !isReleaseTank);", POLICY)
        self.assertIn("return controlled ? !ownTake : (sonOnTank && !ownTake);", POLICY)

    def test_the_park_holds_only_its_own_reader(self):
        # R4: a reader whose pick is another Son keeps seeking; the park's "nobody gathers" and gather-at-spot are
        # its own pick's (or a reader with no pick).
        fn = body(ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)")
        self.assertIn("const bool parkIsMine = !state.source ||", fn)
        self.assertIn("if (SeekParkHolds(state.parkPhase) && parkIsMine)", fn)
        self.assertIn("parkIsMine && SeekParkGatherAtSpot(", fn)

    def test_the_gather_waits_at_the_spot_before_the_released_source_arrives(self):
        fn = body(ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkHolds(", fn)
        self.assertIn("SeekParkGatherAtSpot(", fn)

    def test_the_policy_header_names_the_kind_number(self):
        self.assertIn("enum class SeekParkPhase", POLICY)
        self.assertIn("kSeekParkReleaseHoldMs", POLICY)

    def test_the_release_latches_its_success(self):
        # Round 2: the first tick a tank has the source inside the 4 s window moves the park to its terminal released
        # state; a later non-tank tick cannot fail it (TestParkReleaseLatches).
        self.assertIn("case SeekParkPhase::Released:", POLICY)
        self.assertIn("state.phase = SeekParkPhase::Released;", POLICY)
        self.assertIn('return "released";', POLICY)
        self.assertIn("SeekParkReleaseLive(phase)", POLICY)
        # The tank pick reads the receiver in the latched release too, so the drag to the spot is still the release's.
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkReleaseLive(state.parkPhase)", fn)

    def test_the_combat_strategy_queues_the_action_once(self):
        combat = STRATEGY[:STRATEGY.index("void BossTacticsStrategy::InitNonCombatTriggers")]
        reaction = STRATEGY[STRATEGY.index("void BossTacticsStrategy::InitReactionTriggers"):]
        nodes = re.findall(r'new TriggerNode\(\s*"boss tactics park"', STRATEGY)
        self.assertEqual(len(nodes), 1, "the park node is queued more than once")
        self.assertIn('"boss tactics park"', combat)
        self.assertNotIn('"boss tactics park"', reaction)

    def test_both_contexts_register_the_name(self):
        self.assertIn('creators["boss tactics park"]', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics park"]', ACTION_CONTEXT)

    def test_the_take_source_word_names_the_release(self):
        self.assertIn("park_release", TAKE_SRC)


if __name__ == "__main__":
    unittest.main()
