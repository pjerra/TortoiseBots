#!/usr/bin/env python3
"""Wiring test for capital errands (#39) and bot guild mail (#41).

Run:
  python3 -m unittest forever/tools/test_errands_mail_wiring.py
  python3 forever/tools/test_errands_mail_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def check_wiring(bot_mgr, conf_dist, errands_cpp, guild_mail_cpp):
    if "TortoiseBots.World.CapitalErrands" not in bot_mgr or "TickCapitalErrands" not in bot_mgr:
        raise AssertionError("TbBotMgr.cpp missing CapitalErrands tick wiring")
    if "TortoiseBots.World.GuildMail" not in bot_mgr or "TickGuildMail" not in bot_mgr:
        raise AssertionError("TbBotMgr.cpp missing GuildMail tick wiring")
    if "_capitalErrandsEnabled" not in bot_mgr or "_guildMailEnabled" not in bot_mgr or "_botAcceptDuelMinLevel" not in bot_mgr:
        raise AssertionError("TbBotMgr.cpp missing cached config flags in LoadConfig")
    if "TortoiseBots.World.CapitalErrands = 0" not in conf_dist or "TortoiseBots.World.GuildMail = 0" not in conf_dist:
        raise AssertionError("tortoisebots.conf.dist missing CapitalErrands/GuildMail defaults")
    if "PickCapitalErrand" not in errands_cpp or "CanInitiateBotDuel" not in errands_cpp:
        raise AssertionError("TbHostCapitalErrands.cpp missing policy calls")
    if "GameTime::GetGameTimeMS" not in errands_cpp or "Cell::VisitWorldObjects" not in errands_cpp:
        raise AssertionError("TbHostCapitalErrands.cpp missing shared clock or localized duel scan")
    if "bot->duel != nullptr" not in errands_cpp or "tbhost::town::TeachTrainerSpell" not in errands_cpp:
        raise AssertionError("TbHostCapitalErrands.cpp missing duel cast check or direct trainer teach")
    if "PlanGuildGoldGift" not in guild_mail_cpp or "CanMailGuildMaterial" not in guild_mail_cpp:
        raise AssertionError("TbHostGuildMail.cpp missing policy calls")
    if "HasRealGuildPlayers" not in guild_mail_cpp or "IsPoolAccountName" not in guild_mail_cpp:
        raise AssertionError("TbHostGuildMail.cpp missing offline guild member classification")
    if "PrepareGuildMailPlan" not in guild_mail_cpp:
        raise AssertionError("TbHostGuildMail.cpp missing pre-walk guild mail plan check")


class TestErrandsMailWiring(unittest.TestCase):
    def test_wiring(self):
        bot_mgr = read("forever/src/TbBotMgr.cpp")
        conf_dist = read("forever/conf/tortoisebots.conf.dist")
        errands_cpp = read("forever/src/host/TbHostCapitalErrands.cpp")
        guild_mail_cpp = read("forever/src/host/TbHostGuildMail.cpp")
        if MUTATE:
            bot_mgr = bot_mgr.replace("TickCapitalErrands", "DisabledErrands")
        check_wiring(bot_mgr, conf_dist, errands_cpp, guild_mail_cpp)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
