"""Wiring pins for the refuse reasons (walk 2f package R).

The rules live in ai/playerbot/RefuseNotePolicy.h (pure, tested by
tools/test_refuse_note_policy.cpp). These pins prove the live code calls them:

 - the engine records which multiplier vetoed an action and notes the refusal
   before the impossible verdict, and the outcome code carries the note;
 - clearing the cast check also clears the note;
 - the icicles guard notes its safety refusal;
 - the party record reads the outcome reason while the two loot sites keep the
   cast-check reason;
 - the threat multiplier logs the veto row before the high-threat return.

Run: python3 -m unittest tools/test_refuse_note_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = (ROOT / "ai/playerbot/strategy/Engine.cpp").read_text()
ENGINE_H = (ROOT / "ai/playerbot/strategy/Engine.h").read_text()
MAGE = (ROOT / "ai/playerbot/strategy/mage/MageActions.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
THREAT = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
POLICY_TEXT = (ROOT / "ai/playerbot/RefuseNotePolicy.h").read_text()
TAIL_TEXT = (ROOT / "ai/playerbot/OutcomeReasonPolicy.h").read_text()


class RefuseNoteWiring(unittest.TestCase):
    def test_engine_records_vetoing_multiplier(self):
        self.assertIn("vetoedActions.push_back(action);", ENGINE)
        self.assertIn("vetoedBy.push_back(multiplier->getName());", ENGINE)

    def test_engine_notes_veto_before_impossible(self):
        i = ENGINE.index("LogAction(\"A:%s - IMPOSSIBLE")
        j = ENGINE.index("NoteOutcome(action, 'I');", i)
        self.assertIn("NoteRefusal(VetoNoteFor(", ENGINE[i:j])

    def test_note_outcome_carries_refuse_note(self):
        self.assertIn("NoteOutcome", ENGINE)
        self.assertIn("OutcomeCodeFor(", ENGINE)

    def test_initial_outcome_has_no_reason(self):
        self.assertIn("uint32 lastOutcomeCastFail = 0xFF;", ENGINE_H)

    def test_clear_resets_note(self):
        i = AI_H.index("ClearLastCastCheckFail()")
        self.assertIn("lastRefuseNote", AI_H[i:i + 200])

    def test_icicles_notes_attacked(self):
        i = MAGE.index("CastIciclesAction")
        self.assertIn("NoteRefusal(RefuseNote::Attacked)", MAGE[i:i + 1200])

    def test_party_record_reads_outcome_reason(self):
        self.assertIn("char const* ViewOutcomeReason(uint32 code)", COMMANDER)
        self.assertIn("ViewOutcomeReason(engine->GetLastOutcomeCastFail())", COMMANDER)
        self.assertEqual(len(re.findall(r"ViewFailReason\(castFail\)", COMMANDER)), 2)
        self.assertIn("return ai::OutcomeTailWord(code);", COMMANDER)
        self.assertIn('#include "../ai/playerbot/OutcomeReasonPolicy.h"', COMMANDER)

    def test_outcome_tail_names_no_check_and_combat(self):
        tail = (ROOT / "ai/playerbot/OutcomeReasonPolicy.h").read_text()
        self.assertIn("constexpr std::uint32_t kOutcomeNoCheck = 0xFF;", tail)
        self.assertIn('return "combat";', tail)
        words = ["combat"]
        for word in words:
            self.assertLessEqual(len(word), 10)

    def test_threat_multiplier_logs_veto(self):
        i = THREAT.index("bool ThreatVetoLogDue(")
        self.assertIn("g_threatVetoLog", THREAT[i:i + 2500])
        for site in ("reset", "white", "cast", "aoe", "high"):
            self.assertIn('"%s"' % site, THREAT)
        veto_block = THREAT[THREAT.index("if (ThreatVetoLogDue(ai, bot, target))"):]
        self.assertIn('LogThreatVeto(ai, bot, action, target, victim, "high",', veto_block)
        for anchor, name in (
            ("ThreatResetHolds(ai, bot, resetTarget))", "reset"),
            ("ai->SuccessorWhiteCap().Update(true", "white"),
            ("ThreatCapCastOvershoots(", "cast"),
            ('ActionThreatType::ACTION_THREAT_AOE)', "aoe"),
        ):
            veto = THREAT[THREAT.index(anchor):THREAT.index("return 0.0f;", THREAT.index(anchor))]
            self.assertIn("ThreatVetoLogDue(", veto)
            self.assertIn('LogThreatVeto(', veto)
            self.assertIn('"%s"' % name, veto)

    def test_reason_words_fit_feed(self):
        words = ["threat", "veto", "attacked", "not-known", "immune", "no-control", "combat", "other"]
        for word in words:
            self.assertTrue('"%s"' % word in COMMANDER or '"%s"' % word in POLICY_TEXT or
                            '"%s"' % word in TAIL_TEXT)
            self.assertLessEqual(len(word), 10)


if __name__ == "__main__":
    unittest.main()
