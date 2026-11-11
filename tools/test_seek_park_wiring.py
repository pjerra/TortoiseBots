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
        self.assertLess(read.index("if (!rules.on)"), read.index("SeekParkStep("))
        # The pure rules keep today's lead with the key off, whatever the phase says (TestParkNeedsRoom,
        # TestParkHeldUntilReleaseLead in tools/test_seek_park_policy.cpp).
        self.assertIn("if (!rulesOn)", POLICY)

    def test_the_tank_pick_reads_the_key_before_todays_body(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertLess(fn.index("dungeonRunSeekPark"), fn.index("FindBossSeekTankUnparked("))
        self.assertIn("SeekParkVetoesTankTask(", fn)

    def test_todays_tank_body_does_not_read_the_key(self):
        fn = body(ACTIONS, "SeekTankTask FindBossSeekTankUnparked(PlayerbotAI* ai, Player* bot)")
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

    def test_the_park_bot_is_freed_of_its_slot_while_it_applies(self):
        fn = body(ACTIONS, "BossSpreadSlotTask ReadBossSpreadSlot(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkExemptsBot(", fn)

    def test_the_gather_waits_at_the_spot_before_the_released_source_arrives(self):
        fn = body(ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)")
        self.assertIn("SeekParkHolds(", fn)
        self.assertIn("SeekParkGatherAtSpot(", fn)

    def test_the_policy_header_names_the_kind_number(self):
        self.assertIn("enum class SeekParkPhase", POLICY)
        self.assertIn("kSeekParkReleaseHoldMs", POLICY)

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
