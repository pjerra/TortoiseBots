"""Wiring pins for the seek's heal wait (AiPlayerbot.ReactionHealSeekWaitMs).

`boss tactics seek` walks a bot into the row's cloud or to the gather, and that walk (MoveTo)
breaks whatever cast is under way. The rule lives in ai/playerbot/ReactionCastGuardPolicy.h
(SeekHealWaitDue, SeekTravelMs; behaviour in tools/test_seek_heal_wait_policy.cpp). These pins
only prove the live code feeds it and that nothing else changed:

 - BossTacticsSeekAction::Execute builds the read after its anchor clamp and its same-spline
   early return, and before the walk, stands the walk down on a due wait (return false, no
   walk started), and logs one row per wait through the seek row's own writer;
 - the wait refuses itself while the avoid-aoe trigger's condition holds (AoeHazardEscapeDue):
   `avoid aoe` shares the seek's ACTION_EMERGENCY + 5, so a hold in front of it would starve
   the escape. One condition, used by the trigger that queues the escape and by the seek;
 - the cast read is the shared PlayerbotAI::ReadGenericCast the reaction guard's break uses,
   so the two can never disagree about what a heal under way is;
 - the key is read with default 0, clamped to 0-3000, beside the cast guard's percent in the
   reloadable loader, and the shipped template sets 0;
 - the wait never stops a walk or a cast itself (it only declines the walk), and no boss or
   spell id is named in the new code.

Run: python3 -m unittest tools/test_seek_heal_wait_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SEEK_CPP = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
SEEK_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
PLAYERBOT_AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
POLICY = (ROOT / "ai/playerbot/ReactionCastGuardPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_seek_heal_wait_policy.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
MOVEMENT_CPP = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
MOVEMENT_H = (ROOT / "ai/playerbot/strategy/actions/MovementActions.h").read_text()
TRIGGERS_CPP = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()

WAIT_HEAD = "// The heal wait (AiPlayerbot.ReactionHealSeekWaitMs, ReactionCastGuardPolicy.h)"
WALK = "MoveTo(bot->GetMapId(), x, y, z, false, false, false, true"


def block(text, start, end="\n}\n"):
    """The text from `start` to the next `end` (a column-0 close by default)."""
    i = text.index(start)
    return text[i:text.index(end, i)]


EXEC = block(SEEK_CPP, "bool BossTacticsSeekAction::Execute(Event& event)")
WAIT = EXEC[EXEC.index(WAIT_HEAD):EXEC.index(WALK)]
READ = block(PLAYERBOT_AI, "bool PlayerbotAI::ReadGenericCast")
REACTION = block(PLAYERBOT_AI, "bool PlayerbotAI::UpdateAIReaction")
LOG = block(SEEK_CPP, "void LogSeekHealWait(")
HAZARD = block(MOVEMENT_CPP, "bool ai::AoeHazardEscapeDue(")
TRIGGER = block(TRIGGERS_CPP, "bool AoeThreatNearbyTrigger::IsActive(")


class SeekHealWaitWiring(unittest.TestCase):
    def test_the_wait_stands_between_the_clamp_and_the_walk(self):
        clamp = EXEC.index("if (!ClampMoveToBossAnchor(ai, bot, x, y, z))")
        # The same-spline return: a walk already running is not relaunched for a wait.
        spline = EXEC.index("if (hasLast && !bot->IsStopped() && !bot->movespline->Finalized()")
        rule = EXEC.index("ai::SeekHealWaitDue(in)")
        walk = EXEC.index(WALK)
        self.assertLess(clamp, spline)
        self.assertLess(spline, rule)
        self.assertLess(rule, walk)
        # The at-spot pass above never reaches the wait: the gather's own stop is untouched.
        self.assertLess(EXEC.index("if (task.atSpot)"), rule)

    def test_a_due_wait_declines_the_walk(self):
        self.assertIn("if (ai::SeekHealWaitDue(in))", WAIT)
        due = WAIT[WAIT.index("if (ai::SeekHealWaitDue(in))"):]
        # No walk started for the next pass to relaunch, and the action yields its tick.
        self.assertIn("hasLast = false;", due)
        self.assertIn("return false;", due)
        # The first pass of a wait sets its end from the cast's own remainder; the later ones just stand.
        self.assertIn("if (!inWait)", due)
        self.assertIn("healWaitUntilMs = nowMs + rem;", due)
        self.assertIn("healWaitActive = true;", due)
        # A wait that ran out ends here, so the rewait spacing counts from it.
        self.assertIn("healWaitActive = false;", WAIT)
        self.assertIn("healWaitEnded = true;", WAIT)
        self.assertIn("healWaitEndMs = nowMs;", WAIT)

    def test_a_due_hazard_escape_outranks_the_wait(self):
        # `avoid aoe` queues at the seek's own ACTION_EMERGENCY + 5, so the seek asks its condition while it builds
        # the pass, before the rule can decline the walk.
        ASK = "in.hazardDue = sPlayerbotAIConfig.reactionHealSeekWaitMs > 0 && AoeHazardEscapeDue(ai, bot);"
        self.assertIn(ASK, WAIT)
        ask = WAIT.index(ASK)
        self.assertLess(ask, WAIT.index("ai::SeekHealWaitDue(in)"))
        self.assertLess(ask, WAIT.index("return false;"))
        # The rule refuses the wait outright for it (the policy test proves the wait is cut).
        self.assertIn("if (in.hazardDue) return false;", POLICY)

    def test_one_hazard_condition_for_the_trigger_and_the_seek(self):
        # Declared once, beside the action it belongs to, and used by the trigger that queues the escape and by the
        # seek's wait: the two cannot disagree about whether an escape is due.
        self.assertIn("bool AoeHazardEscapeDue(PlayerbotAI* ai, Player* bot);", MOVEMENT_H)
        self.assertEqual(MOVEMENT_CPP.count("bool ai::AoeHazardEscapeDue("), 1)
        for sensor in ("CollectDamagingDynObjHazards(ai, bot, clouds, true)", "InsideAoeHazard(cloud,",
                       'AI_VALUE2(bool, "has area debuff", "self target")',
                       'AI_VALUE(std::list<ObjectGuid>, "nearest damaging traps")',
                       'AI_VALUE(std::list<ObjectGuid>, "possible triggers")'):
            self.assertIn(sensor, HAZARD)
        # The trigger keeps its creature-aura keep-out registration and returns the shared rule, with no second copy
        # of the sensors.
        self.assertIn('SET_AI_VALUE(Hazard, "add hazard", std::move(hazard));', TRIGGER)
        self.assertIn("return AoeHazardEscapeDue(ai, bot);", TRIGGER)
        self.assertNotIn("InsideAoeHazard(", TRIGGER)
        self.assertNotIn("CollectDamagingDynObjHazards", TRIGGER)

    def test_the_reads_the_rule_takes(self):
        # The key, the role, the siphon's clock and the walk's own length from the bot's run speed.
        self.assertIn("in.waitMaxMs = sPlayerbotAIConfig.reactionHealSeekWaitMs;", WAIT)
        self.assertIn("in.botIsHealer = ai->IsHeal(bot);", WAIT)
        self.assertIn("in.dueInMs = task.dueInMs;", WAIT)
        self.assertIn("in.travelMs = ai::SeekTravelMs(task.dist, bot->GetSpeed(MOVE_RUN));", WAIT)
        # A cloud's own time left in the cloud phase, and no cloud rule at the gather.
        self.assertIn("in.cloudLeftMs = task.phase == SeekTask::Phase::Cloud && task.cloud", WAIT)
        self.assertIn("? static_cast<std::int32_t>(task.cloud->GetDuration()) : -1;", WAIT)
        # The wait's own state: its end, and when the last one ended.
        self.assertIn("healWaitActive && nowMs <= healWaitUntilMs + ai::kSeekHealWaitEndSlackMs", WAIT)
        self.assertIn("waitEndedMsAgo = nowMs - healWaitEndMs;", WAIT)
        self.assertIn("in.inWait = inWait;", WAIT)
        self.assertIn("in.msSinceWaitEnd = waitEndedMsAgo;", WAIT)
        # The seek code's own clock.
        self.assertIn("const std::uint32_t nowMs = WorldTimer::getMSTime();", WAIT)

    def test_the_wait_state_is_per_bot_action(self):
        members = block(SEEK_H, "class BossTacticsSeekAction")
        self.assertIn("std::uint32_t healWaitUntilMs = 0;", members)
        self.assertIn("bool healWaitActive = false;", members)
        self.assertIn("std::uint32_t healWaitEndMs = 0;", members)
        self.assertIn("bool healWaitEnded = false;", members)

    def test_the_read_is_the_shared_one(self):
        # Declared once, on the AI, and used by the guard's break and by the seek.
        self.assertIn("bool ReadGenericCast(Player* caster, ai::ReactionCastRead& cast);", PLAYERBOT_AI_H)
        self.assertIn("ReadGenericCast(bot, cast);", REACTION)
        self.assertIn("const bool castRead = ai->ReadGenericCast(bot, cast);", WAIT)
        self.assertIn("in.cast = castRead ? &cast : nullptr;", WAIT)
        # The read is the generic slot's state, countdown, channel duration and heal effects.
        self.assertIn("GetCurrentSpell(CURRENT_GENERIC_SPELL)", READ)
        self.assertIn("cast.state = spell->getState();", READ)
        self.assertIn("cast.remainingMs = spell->GetCastedTime();", READ)
        self.assertIn("cast.totalMs = cast.state == SPELL_STATE_CASTING && spell->m_spellInfo", READ)
        self.assertIn("? spell->m_spellInfo->CalculateDuration(caster) : spell->GetCastTime();", READ)
        self.assertIn("cast.isHeal = ReactionCastIsHeal(spell->m_spellInfo);", READ)
        # One heal test, and the guard's break no longer carries its own copy of the read.
        self.assertEqual(PLAYERBOT_AI.count("static bool ReactionCastIsHeal("), 1)
        self.assertEqual(SEEK_CPP.count("ReactionCastIsHeal"), 0)
        self.assertNotIn("spell->getState()", REACTION)

    def test_one_row_per_wait(self):
        self.assertIn("LogSeekHealWait(ai, rem, task.dueInMs, in.travelMs);", WAIT)
        # A first pass only: the wait's own passes write nothing more.
        self.assertEqual(WAIT.count("LogSeekHealWait("), 1)
        self.assertIn("event=heal_wait,rem_ms=%u,due_in=%u,travel_ms=%u", LOG)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsSeek", info1, "");', LOG)
        self.assertIn('if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))', LOG)
        # The row's due_in is seconds, as the seek row's own is: the walk reads both alike.
        self.assertIn("static_cast<unsigned>(dueInMs / 1000)", LOG)

    def test_the_wait_stops_nothing_itself(self):
        self.assertNotIn("CastStop", WAIT)
        self.assertNotIn("InterruptSpell", WAIT)
        self.assertNotIn("StopMoving", WAIT)
        self.assertNotIn("MoveTo", WAIT)

    def test_no_boss_or_spell_ids_in_the_new_code(self):
        for text in (WAIT, LOG):
            self.assertIsNone(re.search(r"\b\d{5}\b", text), "a boss or spell id in the wait code")

    def test_policy_is_core_free_and_carries_the_rule(self):
        self.assertNotIn("#include <game", POLICY)
        self.assertIn("inline std::uint32_t SeekTravelMs(float distYd, float speedYdS)", POLICY)
        self.assertIn("inline bool SeekHealWaitDue(SeekHealWaitIn const& in)", POLICY)
        self.assertIn("constexpr std::uint32_t kSeekHealWaitMarginMs = 3000;", POLICY)
        self.assertIn("constexpr std::uint32_t kSeekHealRewaitMs = 5000;", POLICY)
        # Each refusal of the rule, in order.
        self.assertIn("bool hazardDue = false;", POLICY)
        self.assertIn("if (in.hazardDue) return false;", POLICY)
        self.assertIn("if (in.waitMaxMs <= 0) return false;", POLICY)
        self.assertIn("if (!in.botIsHealer) return false;", POLICY)
        self.assertIn("if (!in.cast || !in.cast->isHeal) return false;", POLICY)
        self.assertIn("if (ReactionCastPct(*in.cast) < 0) return false;", POLICY)
        self.assertIn("if (rem > static_cast<std::uint32_t>(in.waitMaxMs)) return false;", POLICY)
        self.assertIn("static_cast<std::uint64_t>(rem) + in.travelMs + kSeekHealWaitMarginMs", POLICY)
        self.assertIn("if (in.dueInMs == 0 || static_cast<std::uint64_t>(in.dueInMs) < need) return false;", POLICY)
        self.assertIn("if (in.cloudLeftMs >= 0 && static_cast<std::uint64_t>(in.cloudLeftMs) < need) return false;",
                      POLICY)
        self.assertIn("if (!in.inWait && in.msSinceWaitEnd < kSeekHealRewaitMs) return false;", POLICY)
        # The policy test names the case the old code fails and pins the constants.
        self.assertIn("CHECK(SeekHealWaitDue(in));", POLICY_TEST)
        self.assertIn("CHECK(ai::kSeekHealWaitMarginMs == 3000);", POLICY_TEST)
        self.assertIn("CHECK(!SeekHealWaitDue(in));", POLICY_TEST)

    def test_config_key(self):
        # Default 0: with the key off the walk breaks the cast as before.
        self.assertIn("int reactionHealSeekWaitMs = 0;", CONFIG_H)
        self.assertIn(
            'reactionHealSeekWaitMs =\n'
            '        std::min(3000, std::max(0, config.GetIntDefault("AiPlayerbot.ReactionHealSeekWaitMs", 0)));',
            CONFIG_CPP)
        # Read beside the cast guard's percent, in LoadDungeonRunKeys, which `.reload config` re-runs.
        loader = CONFIG_CPP[CONFIG_CPP.index("void PlayerbotAIConfig::LoadDungeonRunKeys()"):]
        loader = loader[:loader.index("\n}\n")]
        self.assertIn('config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)', loader)
        self.assertIn('config.GetIntDefault("AiPlayerbot.ReactionHealSeekWaitMs", 0)', loader)
        self.assertLess(loader.index("ReactionHealCastGuardPct"), loader.index("ReactionHealSeekWaitMs"))
        # The shipped template ships it off.
        self.assertRegex(TEMPLATE, re.compile(r"^AiPlayerbot\.ReactionHealSeekWaitMs = 0$", re.MULTILINE))
        self.assertIn("# 0 - off: the walk breaks the cast as before (Default)", TEMPLATE)


if __name__ == "__main__":
    unittest.main()
