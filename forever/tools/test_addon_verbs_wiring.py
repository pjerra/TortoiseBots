#!/usr/bin/env python3
"""Wiring pins for the addon-channel `.bot` passthrough (TBF #130 piece f-verbs).

Every `.bot` word the addon sends that the player layer answers runs through
player::Dispatch with the reply sink set; the captured lines become one Ack/Err
plus the lines verbatim. The pins below fail on the pre-f-verbs tree (no sink,
no TbAddonVerbs files, no TbScripts call).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
PLAYER = HOST / "player"

REGISTERED = ["add", "autogear", "hire", "logout", "maintenance", "remove", "stats", "summon", "who"]
NOT_REGISTERED = ["roster", "list", "alts", "action", "invite", "uninvite", "role", "strategy",
                  "formation", "downtime", "command", "cc", "run", "features", "config",
                  "help", "follow", "stay", "attack", "flee"]


def read(path):
    return Path(path).read_text()


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


class VerbsWiring(unittest.TestCase):
    def setUp(self):
        self.cmd_cpp = read(PLAYER / "TbPlayerCmd.cpp")
        self.cmd_h = read(PLAYER / "TbPlayerCmd.h")
        self.gear_cpp = read(HOST / "TbAutogear.cpp")
        self.pure = read(HOST / "TbAddonVerbsPure.h")
        self.verbs_h = read(HOST / "TbAddonVerbs.h")
        self.verbs_cpp = read(HOST / "TbAddonVerbs.cpp")
        self.scripts = read(SRC / "TbScripts.cpp")

    def test_sink_declared_in_player_header(self):
        self.assertIn("void SetReplySink(std::vector<std::string>* sink);", self.cmd_h)
        self.assertIn("bool CaptureReply(std::string const& line);", self.cmd_h)
        self.assertIn("#include <vector>", self.cmd_h)

    def test_say_honors_the_sink(self):
        body = function_body(self.cmd_cpp, r"void Say\(Player\* player, std::string const& line\)\s*\{")
        self.assertIn("s_replySink", body)
        self.assertIn("s_replySink->push_back(line);", body)
        # Captured lines never reach chat while the sink is set.
        self.assertLess(body.index("push_back"), body.index("SendSysMessage"))
        # The definitions live at player scope (external linkage for the verbs file), not in the anonymous block.
        m = re.search(r"bool CaptureReply\(std::string const& line\)\s*\{", self.cmd_cpp)
        assert m, "CaptureReply definition not found"
        anon_start = self.cmd_cpp.index("bool s_commands = true;")
        self.assertLess(m.start(), anon_start)

    def test_autogear_say_honors_the_sink(self):
        body = function_body(self.gear_cpp, r"void Say\(Player\* player, std::string const& text\)\s*\{")
        self.assertIn("player::CaptureReply(text)", body)
        self.assertLess(body.index("CaptureReply"), body.index("SendSysMessage"))

    def test_off_build_still_links(self):
        tail = self.cmd_cpp.split("#else", 1)[1]
        self.assertIn("void SetReplySink(std::vector<std::string>*) { }", tail)
        self.assertIn("bool CaptureReply(std::string const&) { return false; }", tail)

    def test_pure_shape_present(self):
        for symbol in ["IsPassthroughVerb", "PassthroughVerbs", "CanonicalVerb",
                       "ParseReplyLine", "Summarize", "IsWhoRow"]:
            self.assertIn(symbol, self.pure)
        tokens = re.findall(r'"([a-z]+)"', self.pure.split("PassthroughVerbs")[1].split("}")[0])
        self.assertEqual(tokens, REGISTERED)   # sorted, unique, exactly the eight

    def test_verbs_registered_and_routed(self):
        self.assertIn("void RegisterAddonVerbsPassthrough();", self.verbs_h)
        for verb in REGISTERED:
            self.assertIn(f'addon::RegisterVerb("{verb}"', self.verbs_cpp)
        for verb in NOT_REGISTERED:
            self.assertNotIn(f'RegisterVerb("{verb}"', self.verbs_cpp)
        self.assertIn("player::SetReplySink(&lines);", self.verbs_cpp)
        self.assertIn("player::SetReplySink(nullptr);", self.verbs_cpp)
        self.assertIn("player::Dispatch(requester, rest)", self.verbs_cpp)

    def test_registered_verbs_ride_dispatch(self):
        body = function_body(self.cmd_cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
        if "HandleSummon" in self.cmd_cpp:
            for verb in REGISTERED:   # player-commands R3 merged: every verb routes
                self.assertIn(f'"{verb}"', body)
        else:                         # pre-R3: summon/stats ride the pure map until #203 merges
            for verb in ["add", "logout", "remove", "who", "maintenance", "autogear"]:
                self.assertIn(f'"{verb}"', body)
            for verb in ["summon", "stats"]:
                self.assertTrue("IsPassthroughVerb" in self.pure and verb in self.pure)

    def test_scripts_calls_registration_after_channel(self):
        self.assertIn('#include "host/TbAddonVerbs.h"', self.scripts)
        self.assertIn("TortoiseBots::addonverbs::RegisterAddonVerbsPassthrough();", self.scripts)
        self.assertLess(self.scripts.index("TortoiseBots::addon::AddScripts();"),
                        self.scripts.index("TortoiseBots::addonverbs::RegisterAddonVerbsPassthrough();"))


if __name__ == "__main__":
    unittest.main()
