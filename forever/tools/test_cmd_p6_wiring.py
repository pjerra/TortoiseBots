#!/usr/bin/env python3
"""Wiring pin for Commander P6 (`forever/docs/handoff/B-commander-p6-interact.md`):
- `.tb cmd` is registered in `forever/src/TbScripts.cpp` -> `TortoiseBots::cmd::HandleCmd`.
- `TbCmdTasks.cpp` delegates to the shared task engine in `run::tasks` (`BeginCommander`, `SetLootMethodForParty`, `BotStatesOf`), never re-implementing walks or opcodes directly.
- `TbRun.cpp` ticks `tasks::Update(diffMs)` even when no dungeon run is active (`Table().empty()`).
- `TbRunTasks.cpp` validates gossip options through `GossipOrder::MatchOption` before `HandleGossipSelectOptionOpcode`.
- Mutation mode (`--mutate`) verifies each pin catches a broken wiring."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "forever/src/TbScripts.cpp"
CMD_TASKS = ROOT / "forever/src/host/cmd/TbCmdTasks.cpp"
RUN_TASKS = ROOT / "forever/src/host/run/TbRunTasks.cpp"
RUN_CPP = ROOT / "forever/src/host/run/TbRun.cpp"


def check(scripts: str, cmd_tasks: str, run_tasks: str, run_cpp: str) -> list[str]:
    errs: list[str] = []
    if '"cmd"' not in scripts or "TortoiseBots::cmd::HandleCmd" not in scripts:
        errs.append("TbScripts.cpp missing `.tb cmd` -> TortoiseBots::cmd::HandleCmd registration")
    for sym in ("run::tasks::BeginCommander", "run::tasks::SetLootMethodForParty", "run::tasks::BotStatesOf"):
        if sym not in cmd_tasks:
            errs.append(f"TbCmdTasks.cpp missing shared task call {sym}")
    for forbidden in ("HandleGameObjectUseOpcode", "HandleGossipHelloOpcode", "HandleGossipSelectOptionOpcode", "HandleSetLootMethodOpcode"):
        if forbidden in cmd_tasks:
            errs.append(f"TbCmdTasks.cpp calls {forbidden} directly instead of through TbRunTasks")
    if "GossipOrder::MatchOption(" not in run_tasks or "->HandleGossipSelectOptionOpcode(" not in run_tasks:
        errs.append("TbRunTasks.cpp missing GossipOrder::MatchOption guard before HandleGossipSelectOptionOpcode")
    elif run_tasks.find("GossipOrder::MatchOption(") > run_tasks.find("->HandleGossipSelectOptionOpcode("):
        errs.append("TbRunTasks.cpp calls HandleGossipSelectOptionOpcode before GossipOrder::MatchOption")
    update_pos = run_cpp.find("tasks::Update(diffMs);")
    empty_pos = run_cpp.find("if (Table().empty())")
    if update_pos == -1 or empty_pos == -1 or update_pos > empty_pos:
        errs.append("TbRun.cpp must call tasks::Update(diffMs) before `if (Table().empty())` so commander tasks tick outside a run")
    return errs


def main() -> int:
    scripts = SCRIPTS.read_text()
    cmd_tasks = CMD_TASKS.read_text()
    run_tasks = RUN_TASKS.read_text()
    run_cpp = RUN_CPP.read_text()

    errs = check(scripts, cmd_tasks, run_tasks, run_cpp)
    if errs:
        for e in errs:
            print("FAIL:", e)
        return 1

    if "--mutate" in sys.argv:
        # 1) Mutate TbCmdTasks to drop BeginCommander
        m1 = check(scripts, cmd_tasks.replace("run::tasks::BeginCommander", "DirectQueue"), run_tasks, run_cpp)
        assert m1, "mutation 1 (drop BeginCommander) should have failed"
        # 2) Mutate TbRun.cpp to move tasks::Update after Table().empty()
        m2 = check(scripts, cmd_tasks, run_tasks, run_cpp.replace("tasks::Update(diffMs);", "") + "\nif (Table().empty()) return;\ntasks::Update(diffMs);")
        assert m2, "mutation 2 (Update after empty check) should have failed"
        # 3) Mutate TbRunTasks.cpp to drop MatchOption
        m3 = check(scripts, cmd_tasks, run_tasks.replace("GossipOrder::MatchOption", "SkipMatch"), run_cpp)
        assert m3, "mutation 3 (skip MatchOption) should have failed"
        print("PASS: test_cmd_p6_wiring (all 3 mutations caught)")
        return 0

    print("PASS: test_cmd_p6_wiring")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
