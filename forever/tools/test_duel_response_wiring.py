#!/usr/bin/env python3
"""Wiring pins for a bot answering a duel challenge.

The bot session receives no duel packet, so the host reads the core's own Player::duel state: the accept/decline
answer is sent for every bot on every pass (not only a free bot with capital errands on), and the bot attacks its
duel opponent once the countdown has put the duel in progress. The level gate is AiPlayerbot.BotAcceptDuelMinimumLevel
from aiplayerbot.conf, the file the engine reads (not worldserver.conf through sConfigMgr).

Run: python3 -m unittest forever/tools/test_duel_response_wiring.py
"""
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


MGR = "forever/src/TbBotMgr.cpp"
ERRANDS = "forever/src/host/TbHostCapitalErrands.cpp"
ERRANDS_H = "forever/src/host/TbHostCapitalErrands.h"
BRAIN = "forever/src/host/TbEngineBrain.cpp"
POLICY = "ai/playerbot/policy/world/CapitalErrandPolicy.h"


class EveryBotAnswers(unittest.TestCase):
    def test_the_response_tick_runs_outside_the_free_bot_errand_guard(self):
        text = read(MGR)
        start = text.index("void BotMgr::Update(uint32 diff)")
        call = text.index("tbhost::errands::TickDuelResponse(me);", start)
        guard = text.index("if (bot.free && _capitalErrandsEnabled)", call)
        # The call is at the lambda's own level, not inside the free-bot guard's block.
        line = text[text.rfind("\n", 0, call) + 1:call]
        self.assertEqual(line, " " * 8, "TickDuelResponse is nested deeper than the per-bot body")
        self.assertLess(call, guard)
        body = text[guard:guard + 200]
        self.assertIn("tbhost::errands::TickCapitalErrands(me, diff);", body)

    def test_the_response_tick_is_declared(self):
        self.assertIn("void TickDuelResponse(Player* bot);", read(ERRANDS_H))
        self.assertIn("void TickDuelResponse(Player* bot)\n{", read(ERRANDS))


class AnyChallenger(unittest.TestCase):
    def test_the_answer_does_not_require_the_challenger_to_be_a_bot(self):
        text = read(ERRANDS)
        self.assertNotIn("initiatorIsBot", text)

    def test_the_master_exemption_uses_a_real_player_master(self):
        text = read(ERRANDS)
        block = text[text.index("void RespondToDuel"):text.index("void ScanForBotDuel")]
        self.assertIn("mgr.MasterOf(bot->GetGUID())", block)
        self.assertIn("!master->GetSession()->IsBot()", block)
        self.assertIn("ShouldAcceptBotDuel(", block)

    def test_the_rule_is_the_tb_health_or_master_test(self):
        text = read(POLICY)
        start = text.index("inline bool ShouldAcceptBotDuel(")
        body = text[start:text.index("\n}", start)]
        self.assertIn("challengerIsMaster || botHealthPct >= 90", body)
        self.assertNotIn("botIsFreeBot", body)


class AcceptedDuelIsFought(unittest.TestCase):
    def test_the_in_progress_duel_is_attacked_and_logged_once(self):
        text = read(ERRANDS)
        block = text[text.index("void RespondToDuel"):text.index("void ScanForBotDuel")]
        self.assertIn("DUEL_STATE_IN_PROGRESS", block)
        self.assertIn("bot->Attack(bot->duel->Opponent, true);", block)
        self.assertIn('"[W2] duel-fight bot={} opponent={}"', block)
        self.assertIn("st.duelFightLogged", block)


class LevelGateSource(unittest.TestCase):
    def test_the_key_is_read_from_the_engine_config(self):
        self.assertIn("sPlayerbotAIConfig.botAcceptDuelMinimumLevel", read(BRAIN))
        self.assertIn("uint32 DuelMinimumLevel()", read(BRAIN))

    def test_the_host_no_longer_reads_the_key_from_worldserver_conf(self):
        text = read(MGR)
        self.assertNotIn("BotAcceptDuelMinimumLevel", text)
        self.assertNotRegex(text, r"_botAcceptDuelMinLevel")


if __name__ == "__main__":
    unittest.main()
