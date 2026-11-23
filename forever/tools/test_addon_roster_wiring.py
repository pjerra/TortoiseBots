#!/usr/bin/env python3
"""Wiring pins for the addon roster verbs (TBF #130 piece f-roster).

The core cannot run here, so these pin where the verbs stand, what the reply
carries and which helper each row is read through; reach into the pure shapes
through forever/tools/test_addon_roster.cpp. Every assertion fails on the
pre-f-roster source (no TbAddonRoster files, no registration line).
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
TOOLS = ROOT / "forever" / "tools"
PURE = HOST / "TbAddonRosterPure.h"
IMPL = HOST / "TbAddonRoster.cpp"
HDR = HOST / "TbAddonRoster.h"
UNIT = TOOLS / "test_addon_roster.cpp"


def read(path):
    return Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


class Wiring(unittest.TestCase):
    def setUp(self):
        self.pure = read(PURE)
        self.cpp = read(IMPL)
        self.hdr = read(HDR)
        self.scripts = read(SRC / "TbScripts.cpp")
        self.unit = read(UNIT)
        self.player_cmd = read(SRC / "host" / "player" / "TbPlayerCmd.cpp")

    def test_verbs_registered_and_advertised(self):
        body = function_body(self.cpp, r"void RegisterAddonVerbsRoster\(\)\s*\{")
        for verb in ("roster", "list", "alts"):
            self.assertIn(f'addon::RegisterVerb("{verb}"', body, f"verb {verb} not registered")
        # Registration advertises the bare verb itself (TbAddonChannel.h), so
        # no explicit Advertise call is needed for the TBM:VERBS tokens.
        self.assertNotIn("Advertise(", body)
        self.assertIn("void RegisterAddonVerbsRoster();", self.hdr)
        # The one shared-file line, right after the channel's own.
        self.assertIn("TortoiseBots::addon::AddScripts();", self.scripts)
        self.assertIn("TortoiseBots::roster::RegisterAddonVerbsRoster();", self.scripts)
        self.assertLess(self.scripts.index("TortoiseBots::addon::AddScripts();"),
                        self.scripts.index("TortoiseBots::roster::RegisterAddonVerbsRoster();"))

    def test_trailer_order_and_sources(self):
        roster = function_body(self.cpp, r"void HandleRoster\(Player\* requester, std::string_view /\*args\*/\)\s*\{")
        for line in ("RosterBegin(rows.size())", '"TBM:ROSTER_END"', "AltBotsLine(away)",
                     "addoninfo::AppendCcAssign(requester)", 'ClaimedBegin(0)', '"TBM:CLAIMED_END"',
                     "BotStateBegin(states.size())", '"TBM:BOTSTATE_END"'):
            self.assertIn(line, roster, f"roster reply misses {line}")
        # The CC stream rides at TB's spot (Info.cpp:501-505) but f-info owns
        # the lines; the roster reply carries no CC wire of its own.
        self.assertNotIn("TBM:CC_ASSIGN", strip_comments(self.cpp))
        self.assertIn('#include "host/TbAddonInfo.h"', self.cpp)
        trailer = function_body(self.cpp, r"void SendTrailer\(Player\* requester\)\s*\{")
        order = ["VersionLine(", "ProtoLine()", "LatestLine()", "VerbsBegin(", "VerbsChunks(", "kVerbsEnd",
                 "CapsLine(", "TransportLine("]
        for token in order:
            self.assertIn(token, trailer, f"trailer misses {token}")
        positions = [trailer.index(token) for token in order]
        self.assertEqual(positions, sorted(positions), "trailer order is not VERSION PROTO LATEST VERBS_BEGIN CHUNKS END CAPS TRANSPORT")
        # VERBS is the channel's own sorted-unique list, counted like ROSTER
        # (one 255-byte line cannot hold it); CAPS earns tb-gm for a
        # .tb-permitted requester; TRANSPORT is TB's channel verdict.
        self.assertIn("VerbsBegin(verbs.size())", trailer)
        self.assertIn("VerbsChunks(verbs)", trailer)
        self.assertIn("CapsLine(CapsFor(addon::AdvertisedCaps(), HasTbPermission(requester)))", trailer)
        self.assertIn("TransportLine(TransportFor(requester))", trailer)
        gm = function_body(self.cpp, r"bool HasTbPermission\(Player\* player\)\s*\{")
        self.assertIn("HasPermission(rbac::RBAC_PERM_COMMAND_GM)", gm)

    def test_rows_come_from_existing_helpers(self):
        roster = function_body(self.cpp, r"void HandleRoster\(Player\* requester, std::string_view /\*args\*/\)\s*\{")
        self.assertIn("VisibleBots(requester, caller, scope)", roster)
        visible = function_body(self.cpp, r"std::vector<Player\*> VisibleBots\(Player\* requester, player::Caller const& caller, OwnerScope const& scope\)\s*\{")
        self.assertIn("BotMgr::Instance().BotGuids()", visible)
        self.assertIn("IsInWorld()", visible)
        self.assertIn("RosterVisible(caller, FactsOf(requester, bot, scope))", visible)
        facts = function_body(self.cpp, r"player::BotFacts FactsOf\(Player\* requester, Player\* bot, OwnerScope const& scope\)\s*\{")
        for probe in ("BotMgr::Instance().IsBot(guid)",
                      "sCharacterCache->GetCharacterAccountIdByGuid(guid)",
                      "BotMgr::Instance().MasterOf(guid)",
                      "cmd::IsCommandedBy(requester, bot)",
                      "group->GetLeaderGUID()",
                      "run::BotInRunningRun(bot)"):
            self.assertIn(probe, facts, f"facts miss {probe}")
        self.assertIn("IsFreeBot(", roster)     # away set is the free bots
        self.assertIn("h3::LastOrder(", roster)  # move= word from the last order

    def test_gm_sees_what_a_player_sees(self):
        # No `all` widening over the addon: the pure rule carries no GM branch
        visible = function_body(self.pure, r"inline bool RosterVisible\(player::Caller const& caller, player::BotFacts const& bot\)\s*\{")
        self.assertNotIn("caller.gm", visible)
        self.assertNotIn("inRun", visible)
        self.assertNotIn('"all"', strip_comments(self.cpp))

    def test_chat_entry_untouched(self):
        # The piece answers over the addon channel only; `.bot who` keeps its
        # chat grammar and owns the only `who all` widening.
        self.assertNotIn("TBM:", self.player_cmd)
        self.assertIn('"all"', self.player_cmd)

    def test_pure_shapes_are_core_free(self):
        for line in self.pure.splitlines():
            if line.strip().startswith("#include"):
                self.assertTrue(line.strip().startswith('#include "host/') or line.strip().startswith("#include <"),
                                f"TbAddonRosterPure.h includes a core header: {line.strip()}")
        for name in ("RosterLine(", "AltBotsLine(", "AltSumLineText(", "AltSumEnd(",
                     "ClaimedLine(", "BotStateText(", "ProtoLine()", "LatestLine(",
                     "VerbsBegin(", "VerbsChunks(", "kVerbsEnd", "CapsLine(", "CapsFor(", "TransportVerdict(",
                     "RosterVisible(", "BotMoveWord(", "RosterStateOf("):
            self.assertIn(name, self.pure)
            self.assertIn(name.rstrip("("), self.unit, f"the unit test does not cover {name}")

    def test_required_cases_are_named(self):
        for case in (
            "roster_row_field_order", "field_escaping", "altsum_stream",
            "claimed_botstate_shapes", "transport_verdict", "verbs_caps_join", "verbs_stream",
            "bot_move_word", "roster_visible_matches_who",
        ):
            self.assertIn(f"case: {case}", self.unit, f"missing case {case}")

    def test_pure_test_builds_and_passes(self):
        build = tempfile.mkdtemp(prefix="roster-wiring-")
        binary = os.path.join(build, "test_addon_roster")
        subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra",
                        "-I", str(SRC), str(UNIT), "-o", binary],
                       check=True, cwd=str(ROOT))
        run = subprocess.run([binary], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("OK addon roster pure:", run.stdout)


if __name__ == "__main__":
    unittest.main()
