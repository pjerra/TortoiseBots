#!/usr/bin/env python3
"""Wiring pins for the TBM addon channel (forever/src/host/TbAddonChannel.{h,cpp},
TbAddonPure.h, TbScripts.cpp registration).

Run:
  python3 forever/tools/test_addon_channel_wiring.py
  python3 forever/tools/test_addon_channel_wiring.py --mutate
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE_H = (ROOT / "forever/src/host/TbAddonPure.h").read_text()
CHANNEL_H = (ROOT / "forever/src/host/TbAddonChannel.h").read_text()
CHANNEL_CPP = (ROOT / "forever/src/host/TbAddonChannel.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()


def check_api_shape(header_src):
    for token in [
        "using VerbHandler = void (*)(Player* requester, std::string_view args);",
        "void RegisterVerb(std::string_view verb, VerbHandler handler);",
        "void Advertise(std::string_view token);",
        "void AdvertiseCap(std::string_view token);",
        "std::vector<std::string> AdvertisedVerbs();",
        "std::vector<std::string> AdvertisedCaps();",
        "void Send(Player* to, std::string_view line);",
        "void Ack(Player* to, std::string_view intent, std::string_view scope, std::uint32_t count, std::string_view detail);",
        "void Err(Player* to, std::string_view intent, std::string_view code, std::string_view message);",
        "std::string Safe(std::string_view field);",
        "void AddScripts();",
    ]:
        if token not in header_src:
            raise AssertionError("TbAddonChannel.h lost API line: " + token)


def check_hooks(cpp_src):
    if cpp_src.count("OnAddonMessageSent") < 2:
        raise AssertionError("need both OnAddonMessageSent overrides (group + whisper)")
    if "receiver == player" not in cpp_src:
        raise AssertionError("whisper variant must accept only receiver == sender")
    if "WhisperAddon" not in cpp_src:
        raise AssertionError("Send must answer via Player::WhisperAddon")


def check_guards(cpp_src):
    for token in ["IsBot(player->GetGUID())", "BotMgr::Instance().Enabled()", "engine::Built()",
                  "g_advertisedCaps", "AdvertisedCaps()"]:
        if token not in cpp_src:
            raise AssertionError("channel lost guard: " + token)


class AddonChannelWiring(unittest.TestCase):
    def test_shared_api_shape(self):
        check_api_shape(CHANNEL_H)
        self.assertIn("namespace addon", CHANNEL_H)

    def test_pure_shapes(self):
        for token in ["kMaxPendingPerRequester = 128", "TBM:ACTION_ACK|", "TBM:ACTION_ERR|"]:
            self.assertIn(token, PURE_H)

    def test_hooks_and_answers(self):
        check_hooks(CHANNEL_CPP)
        self.assertIn("FindConnectedPlayer", CHANNEL_CPP)
        self.assertIn("not available on this server yet", CHANNEL_CPP)

    def test_guards(self):
        check_guards(CHANNEL_CPP)

    def test_scripts_registration(self):
        self.assertIn("TortoiseBots::addon::AddScripts();", SCRIPTS)
        self.assertIn('#include "host/TbAddonChannel.h"', SCRIPTS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        try:
            check_hooks(CHANNEL_CPP.replace("receiver == player", "receiver != player"))
        except AssertionError:
            print("MUTATION CAUGHT: self-whisper-only")
        else:
            print("ERROR: self-whisper-only mutation survived", file=sys.stderr)
            sys.exit(1)
        try:
            check_guards(CHANNEL_CPP.replace("IsBot(player->GetGUID())", "false"))
        except AssertionError:
            print("MUTATION CAUGHT: bot-sender guard")
            sys.exit(0)
        print("ERROR: bot-sender guard mutation survived", file=sys.stderr)
        sys.exit(1)
    unittest.main()
