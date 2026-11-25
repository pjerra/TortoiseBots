"""Wiring pins for the engine switch on a bot with no master.

A bot with no master has nothing to follow, so PlayerbotAI can only run it as a free bot (`engine::Create` refuses a
masterless brain without the free flag, and the engine's default non-combat strategies read `IsFreeBot`). The pins keep
the two ways into that state together and in this order:

  * `.tb login <character>` with the engine on (EngineByDefault, an owned `.bot`) is a free bot from the start;
  * `.tb engine <character> on` on a masterless bot marks it free, so the brain is really built on the next tick
    instead of the command answering `engine on` while `.tb state` keeps saying `engine=no`.

Run: python3 -m unittest forever/tools/test_engine_mode_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MGR_CPP = (ROOT / "forever/src/TbBotMgr.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
ENGINE = (ROOT / "forever/src/host/TbEngineBrain.cpp").read_text()
CONF = (ROOT / "forever/conf/tortoisebots.conf.dist").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


class MasterlessEngine(unittest.TestCase):
    def test_a_masterless_engine_login_is_a_free_bot(self):
        login = func(MGR_CPP, "std::string BotMgr::Login(")
        engine_on = login.index("bot.useEngine = _engineByDefault || free || owned || engine;")
        free = login.index("bot.free = true;", engine_on)
        self.assertLess(engine_on, free)
        self.assertIn("if (bot.useEngine && bot.master.IsEmpty())", login[engine_on:free])
        # Before the entry is put in the table, so the first tick already sees the free kit.
        self.assertLess(free, login.index("_bots.emplace(characterGuid"))

    def test_engine_on_switches_a_masterless_bot_to_free(self):
        body = func(MGR_CPP, "std::string BotMgr::SetEngine(")
        self.assertLess(body.index("if (!engine::Built())"), body.index("if (bot.master.IsEmpty() && !bot.free)"))
        free_block = body[body.index("if (bot.master.IsEmpty() && !bot.free)"):body.index("bot.useEngine = true;")]
        self.assertIn("me->GetGroup()", free_block)
        self.assertIn("a free bot plays alone", free_block)
        self.assertLess(free_block.index("me->GetGroup()"), free_block.index("bot.free = true;"))
        # The party refusal must answer before the flags change: the command either works or changes nothing.
        self.assertLess(body.index("bot.free = true;"), body.index("bot.useEngine = true;"))

    def test_the_brain_of_a_masterless_bot_is_the_free_one(self):
        # The host path that builds it: FreeThink, with no master and the free flag.
        free_think = func(MGR_CPP, "void BotMgr::FreeThink(")
        self.assertIn("engine::Create(me, nullptr, error, true)", free_think)
        # ... and the guard this rests on: a masterless brain without the free flag is refused, not built inert.
        create = func(ENGINE, "Brain* Create(Player* bot, Player* master, std::string& error, bool free)")
        self.assertIn("if (!master && !free)", create)
        self.assertIn('error = "the bot has no master";', create)

    def test_the_login_reply_says_when_the_bot_is_free(self):
        handler = func(SCRIPTS, "static bool HandleLogin(")
        self.assertIn("ObjectGuid const guid = GuidByName(name);", handler)
        self.assertIn("BotMgr::Instance().Login(guid, master, free)", handler)
        self.assertIn("BotMgr::Instance().IsFreeBot(guid)", handler)
        self.assertLess(handler.index("IsFreeBot(guid)"), handler.index("is logging in"))

    def test_the_config_says_what_a_masterless_login_does(self):
        self.assertIn("A bot that logs in without a master has nothing to follow, so the engine runs it as a free bot", CONF)


if __name__ == "__main__":
    unittest.main()
