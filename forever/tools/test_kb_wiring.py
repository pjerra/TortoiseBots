#!/usr/bin/env python3
"""Wiring pins for S3 (throws, falls and forced speed).

The decision is pure (forever/src/host/TbThrownPure.h, tested by test_thrown.cpp); the host watcher watches every
bot's EFFECT_MOTION spline, the engine's NoteThrown keeps IsJumping up, and the landing goes through the core's
own fall code. Run:
  python3 -m unittest forever/tools/test_kb_wiring.py
  python3 forever/tools/test_kb_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbThrownPure.h").read_text()
THROWN = (ROOT / "forever/src/host/TbThrown.cpp").read_text()
THROWN_H = (ROOT / "forever/src/host/TbThrown.h").read_text()
KB_COMPAT = (ROOT / "forever/src/compat/TbCompat_KB.h").read_text()
UNPORTED_COMPAT = (ROOT / "forever/src/compat/TbCompat_Unported.h").read_text()
BRAIN = (ROOT / "forever/src/host/TbEngineBrain.cpp").read_text()
BRAIN_H = (ROOT / "forever/src/host/TbEngineBrain.h").read_text()
RUN_TASKS = (ROOT / "forever/src/host/run/TbRunTasks.cpp").read_text()
MGR = (ROOT / "forever/src/TbBotMgr.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
PARTY_STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
FEATURES = (ROOT / "ai/playerbot/UnportedFeatures.h").read_text()
UNPORTED = (ROOT / "forever/docs/UNPORTED.md").read_text()
COMMANDS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()
LIST = (ROOT / "forever/SLICE-KB.txt").read_text()
PURE_TEST = (ROOT / "forever/tools/test_thrown.cpp").read_text()

WATCHER_CALL = "kb::Update(me, bot.brain.get());"
MASTER_AWAY_GATE = "if (bot.masterAway && !UpdateMasterAway(bot, diff))"
ENGINE_DEADLINE = "if (kb::RaisesDeadline(pai->GetJumpTime(), jumpUntilMs))"


def check_pure(pure):
    for needle in ("enum class Kind", "Start", "Flying", "Land", "struct State", "jumpUntilMs",
                   "Step Observe(State& state, Sample const& s, InWater&& inWater)",
                   "struct Sample", "bool chargesFall", "constexpr float kLandingToleranceYd = 5.0f;"):
        if needle not in pure:
            raise AssertionError("TbThrownPure.h misses " + needle)
    if "return currentMs == 0 || int32_t(wantedMs - currentMs) > 0;" not in pure:
        raise AssertionError("the jump deadline ignores the unset (0) case or the uint32 wrap")
    if "if (RaisesDeadline(state.jumpUntilMs, until))" not in pure:
        raise AssertionError("a refresh can lower the jump deadline")
    if "state.jumpUntilMs = until;" not in pure:
        raise AssertionError("a start does not set the jump deadline")
    if "bool const flyingNow = s.effectMotion && !s.splineDone;" not in pure:
        raise AssertionError("the flight test is not generator and spline")
    if "state.mapId = s.mapId;" not in pure:
        raise AssertionError("the flight's map is not remembered for the landing check")
    if "bool const whereTheFlightWas = s.mapId == state.mapId && s.instanceId == state.instanceId &&" not in pure:
        raise AssertionError("a landing on another map/instance is charged")
    if "dx * dx + dy * dy <= kLandingToleranceYd * kLandingToleranceYd" not in pure:
        raise AssertionError("a landing far from the flight's spot is charged")
    if "step.chargesFall = whereTheFlightWas && !inWater();" not in pure:
        raise AssertionError("a landing in water is charged")
    if "if (state.flying && !state.landed)" not in pure:
        raise AssertionError("a landing needs a start and is reported once")


def check_watcher(thrown, compat):
    if "GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE" not in thrown:
        raise AssertionError("TbThrown.cpp does not watch EFFECT_MOTION")
    if "bot->movespline->Finalized()" not in thrown or "bot->movespline->timeRemaining()" not in thrown:
        raise AssertionError("TbThrown.cpp does not read the spline")
    if "Observe(record.flight, sample," not in thrown:
        raise AssertionError("TbThrown.cpp does not use the pure decision")
    if "[bot] { return InWaterAt(bot); }" not in thrown:
        raise AssertionError("the landing does not ask the core's liquid read")
    if "GetLiquidStatus" not in thrown or "LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER" not in thrown:
        raise AssertionError("InWaterAt is not the core's liquid read")
    if "engine::NoteThrown(brain, step.jumpUntilMs, true)" not in thrown:
        raise AssertionError("a start is not handed to the engine")
    if "record.falldmg = step.chargesFall ? tbcompat::FallLandCompat(bot, step.apexZ) : 0;" not in thrown:
        raise AssertionError("a landing is charged without the flight's own-start rule")
    if "record.falldmg" not in thrown or "++record.thrown" not in thrown:
        raise AssertionError("the state counters are not kept")
    # The core reads its own position and last-fall z; both must be put where its handler leaves them, in this order.
    for needle in ("bot->m_movementInfo.pos.Relocate(", "bot->SetFallInformation(0, apexZ);", "bot->HandleFall();",
                   "bot->SetFallInformation(0, bot->GetPositionZ());"):
        if needle not in compat:
            raise AssertionError("TbCompat_KB.h misses " + needle)
    if compat.index("Relocate(") > compat.index("HandleFall()"):
        raise AssertionError("m_movementInfo.pos is relocated after the fall is calculated")
    if compat.index("SetFallInformation(0, apexZ)") > compat.index("HandleFall()"):
        raise AssertionError("the apex is set after the fall is calculated")


def check_engine(brain, ai_h, brain_h):
    stub = brain[brain.index("#else   // not built with the slice"):]
    if "void NoteThrown(Brain*, uint32, bool) {}" not in stub:
        raise AssertionError("the OFF build has no NoteThrown stub")
    if "bool InFlight(Brain*) { return false; }" not in stub:
        raise AssertionError("the OFF build has no InFlight stub")
    if "bool InFlight(Brain* brain);" not in brain_h:
        raise AssertionError("TbEngineBrain.h does not declare InFlight")
    body = brain[brain.index("void NoteThrown(Brain* brain"):]
    body = body[:body.index("\n}\n")]
    if "pai->InterruptSpell(false, \"knockback\")" not in body or "pai->ResetJumpDestination()" not in body:
        raise AssertionError("a start does not stop the cast and the old jump target")
    if ENGINE_DEADLINE not in body:
        raise AssertionError("the jump deadline ignores the unset (0) case or the uint32 wrap")
    if "int32(jumpUntilMs - pai->GetJumpTime())" in body:
        raise AssertionError("the jump deadline is compared the wrap-unsafe way")
    if "pai->SetJumpTime(jumpUntilMs);" not in body:
        raise AssertionError("the jump deadline is not set")
    if "bool InFlight(Brain* brain)\n{\n    return Usable(brain) && brain->ai->IsJumping();\n}" not in brain:
        raise AssertionError("InFlight is not the engine's jump time")
    if "uint32 GetJumpTime() const { return jumpTime; }" not in ai_h:
        raise AssertionError("PlayerbotAI.h has no jump-time getter")
    if "void NoteThrown(Brain* brain, uint32 jumpUntilMs, bool start);" not in brain_h:
        raise AssertionError("TbEngineBrain.h does not declare NoteThrown")
    # A rebind must not cut a flight.
    bind = brain[brain.index("void BindMaster(Brain* brain"):]
    bind = bind[:bind.index("\n}\n")]
    if "if (!brain->ai->IsJumping())" not in bind or "brain->ai->StopMoving();" not in bind:
        raise AssertionError("BindMaster stops a bot in flight")


def check_wiring(mgr, scripts, party_state, unported_compat, run_tasks):
    if WATCHER_CALL not in mgr:
        raise AssertionError("BotMgr::Update does not run the watcher for every bot")
    watcher_at = mgr.find(WATCHER_CALL)
    away_at = mgr.find(MASTER_AWAY_GATE)
    if watcher_at < 0 or away_at < 0 or watcher_at > away_at:
        raise AssertionError("the watcher runs only after the master-away gate")
    # Every host stop holds for a flight: Unit::StopMoving and MotionMaster::Clear cut the spline.
    lines = mgr.splitlines()
    guarded = 0
    for index, line in enumerate(lines):
        if "StopMoving();" not in line:
            continue
        window = "\n".join(lines[max(0, index - 8):index + 1])
        if "engine::InFlight(bot.brain.get())" not in window:
            raise AssertionError("BotMgr stops a bot in flight: " + line.strip())
        guarded += 1
    if guarded != 4:
        raise AssertionError("BotMgr's stop sites changed: %d found" % guarded)
    if "if (moving && !ai->IsJumping())" not in run_tasks:
        raise AssertionError("HoldForAct stops a bot in flight")
    if "kb::Forget(leaving);" not in mgr:
        raise AssertionError("BotMgr::OnPlayerLogout does not drop the state")
    if '{ "knock",  TortoiseBots::kb::HandleKnock' not in scripts:
        raise AssertionError(".tb knock is not registered")
    if '{ "speed",  TortoiseBots::kb::HandleSpeed' not in scripts:
        raise AssertionError(".tb speed is not registered")
    if "kb::AppendThrownState(rec, bot);" not in party_state:
        raise AssertionError(".tb state does not print the thrown keys")
    if "inline void SetFallInformationCompat(Player* player, float z) { player->SetFallInformation(0, z); }" not in unported_compat:
        raise AssertionError("SetFallInformationCompat is still a no-op on ForeverCore")


def check_rows(features, unported, commands, listing):
    # The legacy 1.12 speed and knock-back cases stay in PlayerbotAIPackets.cpp for the Turtle build, but ForeverCore
    # acks its own speed changes and flies a bot server-side, so neither is an unported feature any more.
    for gone in ("X(IncomingSpeedChange,", "X(IncomingKnockBack,"):
        if gone in features:
            raise AssertionError("the legacy packet row is back in UnportedFeatures.h: " + gone)
    for row in ("| incoming: forced speed change |", "| incoming: knock back |"):
        if row in unported:
            raise AssertionError("UNPORTED.md still has the row " + row)
    if ".tb knock <bot> <speedXY> <speedZ>" not in commands or ".tb speed <bot> run|walk|swim <rate>" not in commands:
        raise AssertionError("TB-COMMANDS does not document the two commands")
    for key in ("thrown=", "falldmg=", "runrate="):
        if key not in commands:
            raise AssertionError("TB-COMMANDS does not document " + key)
    w1 = (ROOT / "forever/SLICE-W1.txt").read_text()
    if not listing.startswith(w1.splitlines()[0] + "\n") and not listing.startswith("#"):
        raise AssertionError("SLICE-KB.txt is not built on SLICE-W1.txt")
    if "forever/src/host/TbThrownPure.h" not in listing or "forever/src/compat/TbCompat_KB.h" not in listing:
        raise AssertionError("SLICE-KB.txt misses the round's files")


def mutation_proofs():
    """Each (name, callable) must raise AssertionError on the mutated text: the pin is what keeps the fix in."""
    moved = MGR.replace("        " + WATCHER_CALL + "\n", "", 1)
    moved = moved.replace(
        "        if (!me || !me->IsInWorld() || me->IsBeingTeleported())\n            continue;\n",
        "        if (!me || !me->IsInWorld() || me->IsBeingTeleported())\n            continue;\n        " + WATCHER_CALL + "\n", 1)
    return [
        ("engine deadline compare", lambda: check_engine(
            BRAIN.replace(ENGINE_DEADLINE, "if (int32(jumpUntilMs - pai->GetJumpTime()) > 0)"), AI_H, BRAIN_H)),
        ("pure deadline zero clause", lambda: check_pure(PURE.replace("currentMs == 0 || ", ""))),
        ("pure water rule", lambda: check_pure(PURE.replace("whereTheFlightWas && !inWater()", "whereTheFlightWas && true"))),
        ("pure landing distance", lambda: check_pure(PURE.replace(
            "dx * dx + dy * dy <= kLandingToleranceYd * kLandingToleranceYd", "true"))),
        ("watcher charge rule", lambda: check_watcher(
            THROWN.replace("step.chargesFall ? tbcompat::FallLandCompat(bot, step.apexZ) : 0",
                           "tbcompat::FallLandCompat(bot, step.apexZ)"), KB_COMPAT)),
        ("watch before master-away", lambda: check_wiring(moved, SCRIPTS, PARTY_STATE, UNPORTED_COMPAT, RUN_TASKS)),
        ("host stop in flight", lambda: check_wiring(
            MGR.replace("engine::InFlight(bot.brain.get())", "true"), SCRIPTS, PARTY_STATE, UNPORTED_COMPAT, RUN_TASKS)),
        ("act hold in flight", lambda: check_wiring(
            MGR, SCRIPTS, PARTY_STATE, UNPORTED_COMPAT, RUN_TASKS.replace("if (moving && !ai->IsJumping())", "if (moving)"))),
    ]


class KbWiring(unittest.TestCase):
    def test_pure(self):
        check_pure(PURE)
        self.assertIn("knockback_off_ledge_lands_once_with_apex", PURE_TEST)
        self.assertIn("TestRefreshNeverLowers", PURE_TEST)
        self.assertIn("TestDeadlineAtZeroAndAcrossWrap", PURE_TEST)
        self.assertIn("TestTeleportMidFlightIsNotLanded", PURE_TEST)
        self.assertIn("TestWaterLandingIsNotCharged", PURE_TEST)

    def test_watcher(self):
        check_watcher(THROWN, KB_COMPAT)

    def test_engine(self):
        check_engine(BRAIN, AI_H, BRAIN_H)

    def test_wiring(self):
        check_wiring(MGR, SCRIPTS, PARTY_STATE, UNPORTED_COMPAT, RUN_TASKS)

    def test_rows(self):
        check_rows(FEATURES, UNPORTED, COMMANDS, LIST)

    def test_header_names(self):
        for decl in ("void Update(Player* bot, engine::Brain* brain);", "void Forget(ObjectGuid guid);",
                     "void AppendThrownState(h3::Rec& rec, Player* bot);"):
            self.assertIn(decl, THROWN_H)

    def test_mutation_fails(self):
        for name, mutate in mutation_proofs():
            with self.assertRaises(AssertionError, msg=name):
                mutate()


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        missed = []
        for name, mutate in mutation_proofs():
            try:
                mutate()
            except AssertionError:
                continue
            missed.append(name)
        if missed:
            print("missed mutations (a pin is gone): " + ", ".join(missed))
            sys.exit(0)
        print("all %d mutations caught" % len(mutation_proofs()))
        sys.exit(1)
    unittest.main()
