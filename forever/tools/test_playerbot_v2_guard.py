#!/usr/bin/env python3
"""Configure test for the BUILD_PLAYERBOT_V2 guard in the repository's CMakeLists.txt.

Two bot systems must never load together, so configuring the module with BUILD_TORTOISEBOTS=1 and the core's PlayerbotV2 module
(BUILD_PLAYERBOT_V2=1) has to stop with a FATAL_ERROR and a plain message. The test runs `cmake` on a tiny stand-in core project
(a `trinity_add_module` command and nothing else) with this repository added as the module:

  BUILD_TORTOISEBOTS=1 BUILD_PLAYERBOT_V2=1   -> cmake fails, the message names both options
  BUILD_TORTOISEBOTS=1 BUILD_PLAYERBOT_V2=0   -> the guard stays silent (the configure may stop later: no real core here)
  BUILD_TORTOISEBOTS=0 BUILD_PLAYERBOT_V2=1   -> the module returns before anything: cmake succeeds, no message

Run:  python3 forever/tools/test_playerbot_v2_guard.py        (needs cmake)
      PV2_GUARD_MUTATE=1 python3 forever/tools/test_playerbot_v2_guard.py   (guard removed: the test must FAIL)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MESSAGE = "two bot systems must never load together"


def configure(tortoisebots, playerbot_v2, cmakelists_text):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "module").mkdir()
        (tmp / "module" / "CMakeLists.txt").write_text(cmakelists_text)
        (tmp / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\nproject(standin NONE)\n"
            "function(trinity_add_module)\nendfunction()\nadd_subdirectory(module)\n")
        r = subprocess.run(
            ["cmake", "-S", str(tmp), "-B", str(tmp / "b"), f"-DBUILD_TORTOISEBOTS={tortoisebots}",
             f"-DBUILD_PLAYERBOT_V2={playerbot_v2}"], capture_output=True, text=True)
        return r.returncode, " ".join((r.stdout + r.stderr).split())  # cmake wraps long messages


def module_cmake():
    text = (ROOT / "CMakeLists.txt").read_text()
    if os.environ.get("PV2_GUARD_MUTATE"):
        text = re.sub(r"if\(BUILD_PLAYERBOT_V2\).*?\nendif\(\)\n", "", text, flags=re.S)
        assert "if(BUILD_PLAYERBOT_V2)" not in text, "mutation did not remove the guard"
    return text


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class Guard(unittest.TestCase):
    def setUp(self):
        self.text = module_cmake()

    def test_both_modules_stop_the_configure(self):
        rc, out = configure(1, 1, self.text)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("CMake Error at module/CMakeLists.txt", out)  # a message(FATAL_ERROR) of the module, not a later failure
        self.assertIn(MESSAGE, out)
        self.assertIn("BUILD_PLAYERBOT_V2", out)
        self.assertIn("BUILD_TORTOISEBOTS", out)

    def test_playerbot_v2_off_is_not_blocked(self):
        _, out = configure(1, 0, self.text)
        self.assertNotIn(MESSAGE, out)

    def test_module_off_is_not_blocked(self):
        rc, out = configure(0, 1, self.text)
        self.assertEqual(rc, 0, out)
        self.assertNotIn(MESSAGE, out)


if __name__ == "__main__":
    unittest.main()
