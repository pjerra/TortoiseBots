#!/usr/bin/env python3
"""Wiring pins for proof-1 Part A (forever/docs/PROOF-1-NOTES.md). The narrow GM-observer exemption for `.tb run start|pause|resume|stop`:
the run commands ask CheckRunObserverAccess for the leader and every member, and no other command file may use it.

Run: python3 -m unittest forever/tools/test_run_observer_wiring.py
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FOREVER = ROOT / "forever"


def text(rel):
    return (ROOT / rel).read_text()


class RunObserverWiring(unittest.TestCase):
    def test_start_uses_the_observer_guard(self):
        cmds = text("forever/src/host/run/TbRunCommands.cpp")
        start = cmds[cmds.index('if (verb == "start")'):]
        start = start[:start.index("// ---- status all ----")]
        self.assertIn("h3::CheckRunObserverAccess(handler, leader, code, detail)", start)
        self.assertIn("h3::CheckRunObserverAccess(handler, m, code, detail)", start)

    def test_stop_uses_the_observer_guard(self):
        cmds = text("forever/src/host/run/TbRunCommands.cpp")
        stop = cmds[cmds.index('if (verb == "stop")'):]
        stop = stop[:stop.index("// ---- pause/resume ----")] if "// ---- pause/resume ----" in stop else stop
        self.assertIn("h3::CheckRunObserverAccess(handler, rec->leader, code, detail)", stop)

    def test_pause_and_resume_use_the_observer_guard(self):
        cmds = text("forever/src/host/run/TbRunCommands.cpp")
        # the guard block right before pause/resume (the last one in the handler)
        before = cmds[:cmds.index("StateOutcome const changed =")]
        block = before[before.rindex("std::string code, detail;"):]
        self.assertIn("h3::CheckRunObserverAccess(handler, rec->leader, code, detail)", block)

    def test_no_other_command_file_uses_it(self):
        for path in (FOREVER / "src/host/cmd").rglob("*"):
            if path.is_file() and path.suffix in (".cpp", ".h"):
                self.assertNotIn("CheckRunObserverAccess", path.read_text(), msg=str(path.relative_to(ROOT)))
        # the `.tb run` callers: start (leader + members), stop, pause/resume, and since #307 the read-only `decisions`
        cmds = text("forever/src/host/run/TbRunCommands.cpp")
        self.assertEqual(cmds.count("CheckRunObserverAccess"), 5)

    def test_guard_rule(self):
        pure = text("forever/src/host/TbGuardPure.h")
        self.assertIn("ObserverMayControlRun(bool optionOn, bool senderIsGm, bool chainEndsAtPlayer)", pure)
        host = text("forever/src/host/TbH3.cpp")
        self.assertIn("CheckRunObserverAccess(ChatHandler* handler, Player const* bot", host)
        self.assertIn('sConfigMgr->GetBoolDefault("TortoiseBots.Run.GmObserver", true)', host)
        self.assertIn("SEC_GAMEMASTER", host)
        off = text("forever/src/host/TbH3Off.cpp")
        self.assertIn("bool CheckRunObserverAccess(", off)
        conf = text("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.Run.GmObserver = 1", conf)


if __name__ == "__main__":
    unittest.main()
