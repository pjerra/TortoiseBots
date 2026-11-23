"""Wiring pins for the opt-in player-led run (issue #139): by reading the sources, the glue that no standalone test can run is where it has to be.

  1. a bot-led run is untouched: `StartRun` does not name the player-led state, `SyncBinding` and the real-player pause run only when the run is not player-led;
  2. a player-led tick expects the player as group leader, drops a bot that left or was rebound from the roster, ends when the tank is rebound or the key is off,
     picks the step before the facts and runs the player-led policy's Tick (never the bot-led one);
  3. the executor orders only the accepted roster of a player-led run, lines the followers up with the player (not the tank) and tells the tank to follow, not stay;
  4. the end of a player-led run sends `follow`, the start word is `lead=player`, the three keys are read by TbRunPlayerLed.cpp.

Run: python3 -m unittest forever/tools/test_run_playerled_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUN = ROOT / "forever/src/host/run"
RUN_CPP = (RUN / "TbRun.cpp").read_text()
ORDERS = (RUN / "TbRunOrders.cpp").read_text()
CMDS = (RUN / "TbRunCommands.cpp").read_text()
GLUE = (RUN / "TbRunPlayerLed.cpp").read_text()


def func(text, signature):
    """The body of the function that starts with `signature` (to the closing brace at column 0)."""
    begin = text.index(signature)
    end = text.index("\n}\n", begin)
    return text[begin:end]


class BotLedUntouched(unittest.TestCase):
    def test_start_run_does_not_know_the_mode(self):
        body = func(RUN_CPP, "StartOutcome StartRun(Player* leader")
        self.assertNotIn("PlayerLed", body)
        self.assertNotRegex(body, r"rec->commander|rec->pl\b")

    def test_binding_and_real_player_pause_are_for_bot_led_runs_only(self):
        tick = func(RUN_CPP, "void TickRun(RunRec& rec, uint32 nowMs)")
        self.assertRegex(tick, r"if \(group && !playerLed\)[^\n]*\n\s+SyncBinding\(rec, group, lead\);")
        self.assertRegex(tick, r"bool realPlayer = false;\n\s+if \(group && !playerLed\)")
        self.assertIn("if (playerLed)\n        NotePlayerOrders(rec);\n    else if (OutsideOrder(rec, who, verb))", tick)

    def test_default_start_word_changes_nothing(self):
        self.assertIn("leadWord == playerled::LeadWord::Player ? StartPlayerLedRun(", CMDS)
        self.assertIn(": StartRun(leader, plan, nowMs, fromStep)", CMDS)


class PlayerLedTick(unittest.TestCase):
    def setUp(self):
        self.tick = func(RUN_CPP, "void TickRun(RunRec& rec, uint32 nowMs)")

    def test_the_player_leads_the_group(self):
        self.assertIn("group->GetLeaderGUID() != (playerLed ? rec.commander : rec.leader)", self.tick)

    def test_roster_and_tank_are_watched(self):
        self.assertIn("SyncPlayerLedRoster(rec, group, nowMs);", self.tick)
        self.assertIn("h3::CommanderGuid(rec.leader) != rec.commander", self.tick)
        roster = func(RUN_CPP, "void SyncPlayerLedRoster(")
        self.assertIn("h3::CancelRunOrders(guid);", roster)
        self.assertIn("h3::EndRunPull(bot);", roster)
        self.assertLess(roster.index("h3::CancelRunOrders(guid);"), roster.index("rec.bound.erase(guid);"))
        self.assertIn("group->IsMember(entry.first)", roster)
        self.assertIn("h3::CommanderGuid(entry.first) != rec.commander", roster)

    def test_key_switched_off_ends_the_run(self):
        self.assertIn("if (playerLed && !PlayerLedKeyOn())", self.tick)

    def test_pick_before_the_facts_and_player_led_tick(self):
        self.assertLess(self.tick.index("anchor = PlayerLedPick(rec, lead);"), self.tick.index("ReadFacts(rec, nowMs, facts)"))
        self.assertIn("playerLed ? PlayerLed::Tick(run, rec.pl, facts, anchor, dt) : DungeonRun::Tick(run, facts, dt)", self.tick)

    def test_one_live_run_per_party(self):
        body = func(RUN_CPP, "StartOutcome StartPlayerLedRun(")
        self.assertIn("IsPlayerLed(*other) && other->commander == commander->GetGUID()", body)
        self.assertIn("other->leader == bot->GetGUID() || other->bound.count(bot->GetGUID())", body)

    def test_the_tank_is_not_held_from_following(self):
        body = func(RUN_CPP, "StartOutcome StartPlayerLedRun(")
        self.assertNotIn("HoldRunLead", body)
        self.assertNotIn("BindMember", body)
        self.assertNotIn("BindBotMaster", body)


class RunPolicyForTheRosterOnly(unittest.TestCase):
    def test_a_late_joiner_gets_no_run_policy(self):
        body = func(RUN_CPP, "RunRec* RunOfBot(Player* bot)")
        self.assertIn("IsPlayerLed(*rec) && bot->GetGUID() != rec->leader && !rec->bound.count(bot->GetGUID())", body)


class Executor(unittest.TestCase):
    def test_roster_only(self):
        body = func(ORDERS, "OrderAnswer Issue(RunRec& rec, std::string const& order")
        self.assertIn("party.commander = rec.commander;", body)
        self.assertIn("if (IsPlayerLed(rec))", body)
        self.assertIn("party.members.push_back(rec.leader);", body)
        self.assertIn("for (auto const& entry : rec.bound)", body)

    def test_followers_line_up_with_the_player(self):
        self.assertIn("ctx.fromRun ? (ctx.commander.IsEmpty() ? lead->GetGUID() : ctx.commander)", ORDERS)

    def test_the_tank_follows_in_a_player_led_run(self):
        self.assertIn('(ctx.fromRun && bot == party.lead && ctx.commander.IsEmpty()) ? "stay" : "follow"', ORDERS)
        revive = func(ORDERS, "OrderAnswer DoReviveParty(")
        self.assertEqual(revive.count("!ctx.commander.IsEmpty()"), 2)


class EndAndKeys(unittest.TestCase):
    def test_end_sends_follow(self):
        body = func(RUN_CPP, "uint32 StayParty(RunRec const& rec)")
        self.assertIn('playerLed ? "follow chat shortcut" : "stay chat shortcut"', body)

    def test_the_word(self):
        pure = (RUN / "TbRunPlayerLedPure.h").read_text()
        self.assertIn('"lead=player"', pure)

    def test_keys_are_read_by_the_glue(self):
        for member in ("dungeonRunPlayerLed;", "dungeonRunPlayerLedPullYd;", "dungeonRunPlayerLedAwayMinutes;"):
            self.assertIn("sPlayerbotAIConfig." + member, GLUE)


if __name__ == "__main__":
    unittest.main()
