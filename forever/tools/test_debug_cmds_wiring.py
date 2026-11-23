#!/usr/bin/env python3
"""Wiring pins for the diagnostic port (TBF #142): whisper debug/cdebug/log/set value, the
cast-cancel row, the action-counts dump caller, BOTPERF and the player fleet/version/features lines.

Run:
  python3 forever/tools/test_debug_cmds_wiring.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FOREVER = ROOT / "forever"
HOST = FOREVER / "src" / "host"
SLICE = FOREVER / "src" / "slice"
LEGACY = ROOT / "ai" / "playerbot"


def read(path):
    return Path(path).read_text() if Path(path).exists() else ""


def slice_files():
    return [line.strip() for line in read(FOREVER / "SLICE-W4.txt").splitlines()
            if line.strip() and not line.strip().startswith("#")]


def check_build_list():
    listed = slice_files()
    needed = [
        "ai/playerbot/CastCancelLog.cpp",
        "ai/playerbot/strategy/actions/DebugAction.cpp",
        "ai/playerbot/strategy/actions/DebugEngineCommands.cpp",
        "ai/playerbot/strategy/actions/DebugMiscCommands.cpp",
        "ai/playerbot/strategy/actions/LogLevelAction.cpp",
        "ai/playerbot/strategy/actions/SetValueAction.cpp",
        "ai/playerbot/strategy/generic/DebugStrategy.cpp",
    ]
    for rel in needed:
        if rel not in listed:
            raise AssertionError(f"SLICE-W4.txt lacks {rel}")
        if not (ROOT / rel).exists():
            raise AssertionError(f"SLICE-W4.txt names a missing file {rel}")
    # The families whose code is 1.12-only stay out and are answered by the stubs.
    for rel in ("ai/playerbot/strategy/actions/DebugSpellCommands.cpp",
                "ai/playerbot/strategy/actions/DebugTransportCommands.cpp",
                "ai/playerbot/strategy/actions/DebugTravelCommands.cpp",
                "ai/playerbot/strategy/actions/DebugPositionCommands.cpp",
                "ai/playerbot/strategy/actions/DebugItemCommands.cpp"):
        if rel in listed:
            raise AssertionError(f"SLICE-W4.txt compiles the unported debug family {rel}")


def check_cast_cancel():
    stubs = read(SLICE / "SliceLinkStubs.cpp")
    if "LogCastCancel" in stubs:
        raise AssertionError("SliceLinkStubs.cpp still stands in for the cast-cancel writer")
    writer = read(LEGACY / "CastCancelLog.cpp")
    for pin in ("raidBossDiagnostics", "hasLog(\"bot_events.csv\")", "logEvent(ai, \"CastCancel\"",
                "CastCancelPct(totalMs, cast->GetCastedTime())"):
        if pin not in writer:
            raise AssertionError(f"CastCancelLog.cpp lost `{pin}`")
    if "GetSpellEntryCompat" not in writer:
        raise AssertionError("CastCancelLog.cpp does not read the spell through the compat helper")


def check_registration():
    ctx = read(SLICE / "SliceAiObjectContext.cpp")
    for pin in (
        'creators["debug"] = [](PlayerbotAI* ai) { return new DebugAction(ai); };',
        'creators["cdebug"] = [](PlayerbotAI* ai) { return new DebugAction(ai); };',
        'creators["log"] = [](PlayerbotAI* ai) { return new LogLevelAction(ai); };',
        'creators["set value"] = [](PlayerbotAI* ai) { return new SetValueAction(ai); };',
        'creators["debug"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "debug"); };',
        'creators["set value"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "set value"); };',
        'creators["log level"] = [](PlayerbotAI* ai) { return new LogLevelValue(ai); };',
        'creators["debug"] = [](PlayerbotAI* ai) { return new DebugStrategy(ai); };',
    ):
        if pin not in ctx:
            raise AssertionError(f"the generated context lacks `{pin}`")
    generator = read(FOREVER / "tools" / "slice_gen_context.py")
    if "DEBUG_KEYS = {" not in generator:
        raise AssertionError("slice_gen_context.py has no DEBUG_KEYS list")
    if "PRESET_KEYS, DEBUG_KEYS)" not in generator:
        raise AssertionError("slice_gen_context.py KEYS does not include DEBUG_KEYS")
    for check, script in (("--check", "slice_gen_context.py"), ("--quiet", "slice_registry_check.py")):
        run = subprocess.run([sys.executable, str(FOREVER / "tools" / script), check],
                             cwd=ROOT, capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(f"{script} {check} failed:\n{run.stdout}{run.stderr}")
    registry = subprocess.run([sys.executable, str(FOREVER / "tools" / "slice_registry_check.py"), "--quiet"],
                              cwd=ROOT, capture_output=True, text=True)
    if "(a) slice keys that are not in the full build, or map elsewhere: 0" not in registry.stdout:
        raise AssertionError("slice_registry_check.py (a) is not empty")


def check_debug_stubs():
    stubs = read(SLICE / "SliceDebugStubs.cpp")
    for pin in ("DebugAction::FakeSpell", "DebugAction::addAura", "DebugAction::ParseLocation",
                "DebugAction::HandleDSpell", "DebugAction::HandleASpell", "DebugAction::HandleTransport",
                "DebugAction::HandlePOI", "DebugAction::HandlePosition", "DebugAction::HandleTrade"):
        if pin not in stubs:
            raise AssertionError(f"SliceDebugStubs.cpp lost `{pin}`")
    action = read(LEGACY / "strategy" / "actions" / "DebugAction.cpp")
    for pin in ("ai->ChangeStrategy(on ? \"+debug\" : \"-debug\", BotState::BOT_STATE_NON_COMBAT)",
                "if (text.empty() || text == \"on\" || text == \"off\")"):
        if pin not in action:
            raise AssertionError(f"DebugAction.cpp lacks the bare-debug toggle `{pin}`")


def check_action_counts():
    botmgr = read(FOREVER / "src" / "TbBotMgr.cpp")
    for pin in ("_actionCountsElapsedMs += diff;", "5u * 60u * 1000u", "engine::DumpActionCounts();"):
        if pin not in botmgr:
            raise AssertionError(f"TbBotMgr.cpp lacks the action-count dump caller `{pin}`")
    engine = read(HOST / "TbEngineBrain.cpp")
    if "ai::botdiag::DumpActionCounts();" not in engine:
        raise AssertionError("TbEngineBrain.cpp does not call the engine's DumpActionCounts")
    if "void DumpActionCounts() {}" not in engine:
        raise AssertionError("the engine-off build has no DumpActionCounts no-op")


def check_botperf():
    pure = read(HOST / "TbBotPerfPure.h")
    for pin in ("struct Window", "CoreMs", "BotPerfLine", "\"TortoiseBots: BOTPERF passUs=\"", "\" coreMs=\""):
        if pin not in pure:
            raise AssertionError(f"TbBotPerfPure.h lacks `{pin}`")
    scripts = read(FOREVER / "src" / "TbScripts.cpp")
    for pin in ("perf::BotPerfLine(", "botPerf.elapsedMs >= 30000", "GetFleetCounts()"):
        if pin not in scripts:
            raise AssertionError(f"TbScripts.cpp lacks the BOTPERF writer `{pin}`")
    if "TortoiseBots::perf::Window& BotPerf()" not in scripts:
        raise AssertionError("BOTPERF has no separate window (it must not reset `.tb perf`)")


def check_player_lines():
    player = read(HOST / "player" / "TbPlayerCmd.cpp")
    for pin in ("bool HandleVersion(Player* player)", "GitRevision::GetFullVersion()", "TBM:VERSION|forever-port",
                "TortoiseBots: version: by Sagiroth", "https://github.com/Sagiroth/TortoiseBots",
                "bool HandleFeatures(Player* player)", "addoninfo::FeaturesChatLines()",
                "\" bot(s) online, \"", "\" owned by you, \"", "\" with an engine\""):
        if pin not in player:
            raise AssertionError(f"TbPlayerCmd.cpp lacks `{pin}`")
    if player.index('verb == "version"') > player.index("if (!s_commands"):
        raise AssertionError("version/features are behind the player-commands switch (TB keeps them visible)")
    info = read(HOST / "TbAddonInfo.cpp")
    if "FeaturesChatLines()" not in info or "\"TortoiseBots: feature \"" not in info:
        raise AssertionError("TbAddonInfo.cpp does not expose the feature chat lines")


def main():
    check_build_list()
    check_cast_cancel()
    check_registration()
    check_debug_stubs()
    check_action_counts()
    check_botperf()
    check_player_lines()
    print("RESULT: PASS (test_debug_cmds_wiring)")


if __name__ == "__main__":
    main()
