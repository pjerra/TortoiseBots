"""Wiring pins for S2: a pushback the core reports (SMSG_SPELL_DELAYED) lengthens the engine's wait, and an async cast failure re-arms the engine.

Run:
  python3 -m unittest forever/tools/test_sd_wiring.py
  python3 forever/tools/test_sd_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SESSION_H = (ROOT / "forever/src/TbBotSession.h").read_text()
SESSION_CPP = (ROOT / "forever/src/TbBotSession.cpp").read_text()
BRAIN_H = (ROOT / "forever/src/host/TbEngineBrain.h").read_text()
BRAIN_CPP = (ROOT / "forever/src/host/TbEngineBrain.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
DOCS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()


def capture_delay(session_cpp):
    start = session_cpp.index("void BotSession::CaptureSpellDelay(")
    return session_cpp[start:session_cpp.index("void BotSession::PushCastResult(", start)]


def deliver_cast_result(brain_cpp):
    start = brain_cpp.index("void DeliverCastResult(")
    return brain_cpp[start:brain_cpp.index("// World thread, before UpdateAI", start)]


def check_wiring(session_h, session_cpp, brain_h, brain_cpp, scripts, state, docs):
    if not re.search(r"SpellDelayed,\s*//", session_h):
        raise AssertionError("TbBotSession.h CastResult::Kind must name SpellDelayed")
    if "uint32 delayMs = 0;" not in session_h:
        raise AssertionError("TbBotSession.h CastResult must carry delayMs")
    if "uint64_t delayed = 0;" not in session_h:
        raise AssertionError("TbBotSession.h CastResultCounts must count delayed")
    if "std::string InjectSpellDelay(uint32 delayMs);" not in session_h:
        raise AssertionError("TbBotSession.h must declare InjectSpellDelay")

    send = session_cpp[session_cpp.index("void BotSession::SendPacket("):session_cpp.index("void BotSession::CaptureCastResult(")]
    if "SMSG_SPELL_DELAYED" not in send:
        raise AssertionError("TbBotSession.cpp SendPacket must capture SMSG_SPELL_DELAYED")
    if "CaptureSpellDelay(" not in send:
        raise AssertionError("TbBotSession.cpp SendPacket must route SMSG_SPELL_DELAYED to CaptureSpellDelay")
    body = capture_delay(session_cpp)
    if "in >> result.caster >> delayMs;" not in body:
        raise AssertionError("CaptureSpellDelay must read the caster then the delay, as SpellDelayed::Write writes them")
    # A broadcast of somebody else's pushback (the packet goes to the whole set) is not this bot's cast.
    if not re.search(r"if \(result\.caster != _characterGuid\)\s*\n\s*return;", body):
        raise AssertionError("CaptureSpellDelay must drop a pushback that names another caster")
    if "CastResult::Kind::SpellDelayed" not in body or "result.delayMs = delayMs;" not in body:
        raise AssertionError("CaptureSpellDelay must queue a SpellDelayed result with the delay")
    inject = session_cpp[session_cpp.index("std::string BotSession::InjectSpellDelay("):]
    if "WorldPackets::Spells::SpellDelayed" not in inject:
        raise AssertionError("InjectSpellDelay must build the core's typed SpellDelayed packet")
    if "SendDirectMessage" not in inject:
        raise AssertionError("InjectSpellDelay must send through Player::SendDirectMessage, as the core does")
    if re.search(r"if \(result\.kind == CastResult::Kind::SpellDelayed\)\s*\n\s*\+\+_castResultCounts\.delayed;", session_cpp) is None:
        raise AssertionError("PushCastResult must count the accepted pushbacks (delayed)")

    if "std::string InjectSpellDelay(Player* bot, uint32 delayMs);" not in brain_h:
        raise AssertionError("TbEngineBrain.h must declare InjectSpellDelay")
    if not re.search(r"std::string InjectSpellDelay\(Player\*, uint32\)", brain_cpp):
        raise AssertionError("TbEngineBrain.cpp must keep the OFF-build InjectSpellDelay stub")
    delivered = deliver_cast_result(brain_cpp)
    if "CastResult::Kind::SpellDelayed" not in delivered:
        raise AssertionError("DeliverCastResult must handle SpellDelayed")
    if "PushbackExtraMs(" not in delivered:
        raise AssertionError("DeliverCastResult must pass the delay through sd::PushbackExtraMs")
    if "SPELL_DELAYED" not in delivered:
        raise AssertionError('DeliverCastResult must write the action-log line SPELL_DELAYED')
    if "NoteSpellDelayed(" not in delivered:
        raise AssertionError("DeliverCastResult must lengthen the engine's wait (PlayerbotAI::NoteSpellDelayed)")
    # A pushback returns before the failure paths: without the `return;` every pushback would also run the failure code and write a bogus CAST_FAILED_ASYNC.
    if not re.search(r"NoteSpellDelayed\(extra\);\s*\n\s*return;", delivered):
        raise AssertionError("the SpellDelayed branch of DeliverCastResult must return right after NoteSpellDelayed(extra)")
    # D4: the asynchronous cast failure that ends the bar also re-plans the engine once, on a millisecond clock when the legacy second clock does not act.
    failed = delivered[delivered.index('"CAST_FAILED_ASYNC"'):]
    if "RearmAfterCastFailure(brain, result.spellId)" not in failed:
        raise AssertionError("the async CastFailed path must re-arm the engine (RearmAfterCastFailure)")
    rearm = brain_cpp[brain_cpp.index("void RearmAfterCastFailure("):brain_cpp.index("// One cast result, handed to the engine")]
    if "SpellInterrupted(spellId)" not in rearm:
        raise AssertionError("RearmAfterCastFailure must still call the legacy SpellInterrupted first")
    if "sd::FailureWaitMs(" not in rearm or "NoteCastFailureWait(" not in rearm:
        raise AssertionError("RearmAfterCastFailure must cut the wait with sd::FailureWaitMs (millisecond clock) through NoteCastFailureWait")
    if "NoteCastStart(brain);" not in brain_cpp[brain_cpp.index("brain->ai->UpdateAI(diff);"):][:80]:
        raise AssertionError("Tick must stamp the cast start (NoteCastStart) right after UpdateAI")

    if not re.search(r'\{\s*"pushback"\s*,\s*HandlePushback\b', scripts):
        raise AssertionError('TbScripts.cpp missing the "pushback" row')
    if "TortoiseBots::engine::InjectSpellDelay(" not in scripts:
        raise AssertionError(".tb pushback must send through engine::InjectSpellDelay")

    if 'rec.N("delayed", counts.delayed);' not in state:
        raise AssertionError('.tb state must print the delayed counter')
    if ".tb pushback " not in docs:
        raise AssertionError("TB-COMMANDS.md missing .tb pushback")


class SpellDelayWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP, SCRIPTS, STATE, DOCS)

    def test_mutations_fail(self):
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP.replace("SMSG_SPELL_DELAYED", "SMSG_SPELL_GO", 1), BRAIN_H, BRAIN_CPP, SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP.replace("result.caster != _characterGuid", "false", 2), BRAIN_H, BRAIN_CPP, SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP.replace("PushbackExtraMs(", "int64(", 1), SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP.replace("    RearmAfterCastFailure(brain, result.spellId);\n}", "}"), SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP.replace("NoteSpellDelayed(extra);\n        return;", "NoteSpellDelayed(extra);", 1), SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP.replace("sd::FailureWaitMs(", "int32(", 1), SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP.replace("        NoteCastStart(brain);\n", "", 1), SCRIPTS, STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP, SCRIPTS.replace('"pushback"', '"nope"'), STATE, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SESSION_H, SESSION_CPP, BRAIN_H, BRAIN_CPP, SCRIPTS, STATE.replace('rec.N("delayed", ', 'rec.N("nope", '), DOCS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        # One mutation the pins must catch: the caster test of the pushback reader.
        check_wiring(SESSION_H, SESSION_CPP.replace("result.caster != _characterGuid", "false", 2), BRAIN_H, BRAIN_CPP, SCRIPTS, STATE, DOCS)
        sys.exit(0)
    unittest.main()
