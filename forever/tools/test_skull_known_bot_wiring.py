#!/usr/bin/env python3
"""Wiring pin for the persistent skull icon (TbRunOrders.cpp SetPersistentSkull): an offline bot member still counts as a bot.

IsBot looks only in the live bot set, so an offline bot member counted as a real player and the skull was skipped.
BotMgr records every logged-in character in _knownBots and the icon check uses IsKnownBot (live or previously a bot),
so only a never-bot character blocks the icon.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MGR_H = (ROOT / "src" / "TbBotMgr.h").read_text()
MGR_CPP = (ROOT / "src" / "TbBotMgr.cpp").read_text()
ORDERS = (ROOT / "src" / "host" / "run" / "TbRunOrders.cpp").read_text()
def fn_body(text, marker):
    start = text.index(marker)
    return text[start:text.index("\n}\n", start)]



class SkullKnownBot(unittest.TestCase):
    def test_login_records_the_character_as_a_known_bot(self):
        login = fn_body(MGR_CPP, "std::string BotMgr::Login(")
        self.assertIn("_bots.emplace(characterGuid, std::move(bot));", login)
        self.assertIn("_knownBots.insert(characterGuid);", login)

    def test_known_bot_covers_live_and_logged_out(self):
        self.assertIn("bool IsKnownBot(ObjectGuid characterGuid) const", MGR_H)
        decl = MGR_H[MGR_H.index("bool IsKnownBot(ObjectGuid characterGuid) const"):MGR_H.index(";", MGR_H.index("bool IsKnownBot(ObjectGuid characterGuid) const"))]
        self.assertIn("_bots.count(characterGuid)", decl)
        self.assertIn("_knownBots.count(characterGuid)", decl)

    def test_skull_check_uses_known_bots_not_live_bots(self):
        skull = fn_body(ORDERS, "void SetPersistentSkull(")
        self.assertIn("IsKnownBot(slot.guid)", skull)
        self.assertNotIn("IsBot(slot.guid)", skull)


if __name__ == "__main__":
    unittest.main()
