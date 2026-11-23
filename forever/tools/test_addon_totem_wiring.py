#!/usr/bin/env python3
"""Wiring pins for the `totem` addon verb (TbAddonTotem.{h,cpp} and TbAddonTotemPure.h).

Run:
  python3 -m unittest forever/tools/test_addon_totem_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbAddonTotemPure.h").read_text()
H = (ROOT / "forever/src/host/TbAddonTotem.h").read_text()
CPP = (ROOT / "forever/src/host/TbAddonTotem.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
PLAYER_CMD = (ROOT / "forever/src/host/player/TbPlayerCmd.cpp").read_text()
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()


def check_registers_totem(scripts_src):
    if "TortoiseBots::totem::RegisterAddonVerbsTotem();" not in scripts_src:
        raise AssertionError("TbScripts must call RegisterAddonVerbsTotem after addon::AddScripts")
    channel = scripts_src.index("TortoiseBots::addon::AddScripts();")
    totem = scripts_src.index("TortoiseBots::totem::RegisterAddonVerbsTotem();")
    if not channel < totem:
        raise AssertionError("RegisterAddonVerbsTotem must run after addon::AddScripts")


def check_no_pjerra_edits(player_cmd_src, orders_src):
    for token in ("HandleTotemVerb", "RegisterAddonVerbsTotem", "TbAddonTotem"):
        if token in player_cmd_src:
            raise AssertionError(f"TbPlayerCmd.cpp must not mention {token}")
        if token in orders_src:
            raise AssertionError(f"TbOrders.cpp must not mention {token}")


class TotemWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        self.assertIn('#include "host/TbAddonTotem.h"', SCRIPTS)
        check_registers_totem(SCRIPTS)

    def test_header_and_registration(self):
        self.assertIn("RegisterAddonVerbsTotem", H)
        self.assertIn('addon::RegisterVerb("totem"', CPP)
        self.assertIn('addon::AdvertiseCap("totem")', CPP)

    def test_pure_stays_core_free(self):
        for token in ("Player.h", "ObjectAccessor.h", "WorldSession.h", "Chat.h", "Group.h"):
            self.assertNotIn(f'#include "{token}"', PURE)
        self.assertIn("ParseTotemRequest", PURE)
        self.assertIn("FormatTotemLine", PURE)
    def test_answers_tb_wire(self):
        wire = PURE + CPP
        for token in ("TBM:TOTEM", "totem status", "totem mode", "totem drop", "totem set",
                      "no-shaman", "unknown-totem", "unknown-spell", "nothing", "no-bot"):
            self.assertIn(token, wire)

    def test_calls_existing_implementations(self):
        self.assertIn("manual saved string", CPP)
        self.assertIn("HasSpell", CPP)
        self.assertIn("QueueAction", CPP)
        self.assertIn("has totem", CPP)
        self.assertIn("TotemChoicesForSlot", CPP)
        self.assertIn("player::Decide", CPP)
        self.assertIn("player::Tier::Owner", CPP)
        self.assertIn("host/player/TbPlayerAuthPure.h", CPP)
        # FactsFor must keep every FactsOf field (host/player/TbPlayerCmd.cpp): a dropped field silently widens permission.
        for field in ("facts.isBot", "facts.account", "facts.master", "facts.commandedByCaller",
                      "facts.groupLeader", "facts.leaderIsBot", "facts.inRun"):
            self.assertIn(field, CPP)

    def test_no_pjerra_edits(self):
        check_no_pjerra_edits(PLAYER_CMD, ORDERS)

    def test_pure_grammar_matches_tb(self):
        tb = (ROOT / "commands/Totem.cpp").read_text()
        m = re.search(r"Usage: \.bot totem ([^\"]+)", tb)
        self.assertTrue(m, "TB Totem.cpp must keep its usage string")
        for word in ("<earth|fire|water|air>", "| set ", "| mode ", "| drop", "| status"):
            self.assertIn(word, m.group(1))
            self.assertIn(word, PURE)


if __name__ == "__main__":
    unittest.main()
