#!/usr/bin/env python3
"""Wiring pins for the `lootlog` addon verb (TBF #134 piece i-loot).

Ports TortoiseBots/commands/Inventory.cpp HandleLootLog: the feed is the
bot_events.csv rows the slice actions already write, the reply is TB-exact
TBM:LOOT_BEGIN/LOOT/LOOT_END with the loot-log cap. Poll only, no live push.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
TOOLS = ROOT / "forever" / "tools"
PURE = HOST / "TbAddonLootLogPure.h"
IMPL = HOST / "TbAddonLootLog.cpp"
HDR = HOST / "TbAddonLootLog.h"
UNIT = TOOLS / "test_addon_lootlog.cpp"


def read(path):
    return Path(path).read_text()


class Wiring(unittest.TestCase):
    def setUp(self):
        self.pure = read(PURE)
        self.cpp = read(IMPL)
        self.hdr = read(HDR)
        self.scripts = read(SRC / "TbScripts.cpp")
        self.unit = read(UNIT)

    def test_pure_builds_wire_lines(self):
        for token in (
            "ParseLootLogArgs",
            "FormatLootLine",
            "FormatLootBegin",
            "FormatLootEnd",
            "RowFromHistory",
            "LootHistoryPolicy.h",
            "TBM:LOOT_BEGIN|",
            "TBM:LOOT_END|",
            "TBM:LOOT|",
        ):
            self.assertIn(token, self.pure, f"TbAddonLootLogPure.h missing {token}")

    def test_header_registers_both_surfaces(self):
        for token in ("RegisterAddonVerbsLootLog", "HandleLootLogChat"):
            self.assertIn(token, self.hdr, f"TbAddonLootLog.h missing {token}")

    def test_host_ports_tb_reader(self):
        for token in (
            'addon::RegisterVerb("lootlog"',
            'addon::AdvertiseCap("loot-log")',
            "LogFileRotation::Pieces",
            "kLootHistoryMaxRows",
            "kLootHistoryWindowSeconds",
            "ParseLootHistoryRow",
            "ParseLootTimestamp",
            "addon::Err",
            '"no-bot"',
            '"invalid"',
            "No live group bots found.",
        ):
            self.assertIn(token, self.cpp, f"TbAddonLootLog.cpp missing {token}")

    def test_no_live_push(self):
        self.assertNotIn("SendAddonMessage", self.cpp)
        self.assertNotIn("NotifyOwnerOfLoot", self.cpp)

    def test_scripts_wires_verb_and_gm_command(self):
        for token in (
            '#include "host/TbAddonLootLog.h"',
            "TortoiseBots::lootlog::RegisterAddonVerbsLootLog()",
            "TortoiseBots::lootlog::HandleLootLogChat",
        ):
            self.assertIn(token, self.scripts, f"TbScripts.cpp missing {token}")

    def test_scope_matches_tb(self):
        self.assertIn("GetMemberSlots", self.cpp)
        self.assertIn("BotGuids", self.cpp)
        self.assertIn("Tier::Tactical", self.cpp)

    def test_pure_test_builds_and_passes(self):
        build = tempfile.mkdtemp(prefix="lootlog-wiring-")
        binary = os.path.join(build, "test_addon_lootlog")
        subprocess.run(
            ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra",
             "-I", str(ROOT), "-I", str(ROOT / "ai"), str(UNIT), "-o", binary],
            check=True, cwd=str(ROOT),
        )
        run = subprocess.run([binary], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("test_addon_lootlog:", run.stdout)
        self.assertIn("checks passed", run.stdout)

    def test_required_cases_are_named(self):
        for case in (
            "StoreLootAction row",
            "LootMoney row",
            "LootRoll row",
            "250-byte fit",
            "Page framing",
            "bot_events.csv line parses",
        ):
            self.assertIn(case, self.unit, f"missing case {case}")


if __name__ == "__main__":
    unittest.main()
