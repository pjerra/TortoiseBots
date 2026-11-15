"""Wiring pins for the seek's heal wait (AiPlayerbot.ReactionHealSeekWaitMs).

`boss tactics seek` walks a bot into the row's cloud or to the gather, and that walk (MoveTo)
breaks whatever cast is under way. The rule lives in ai/playerbot/ReactionCastGuardPolicy.h
(SeekHealWaitDue, SeekTravelMs; behaviour in tools/test_seek_heal_wait_policy.cpp). These pins
only prove the live code feeds it and that nothing else changed:

 - BossTacticsSeekAction::Execute builds the read after its anchor clamp and its same-spline
   early return, and before the walk, stands the walk down on a due wait (return false, no
   walk started), and logs one row per wait through the seek row's own writer;
 - the wait refuses itself while an emergency move or escape is due (EmergencyMoveDue): the
   reaction engine runs one picked action a tick and picks the seek ahead of every emergency row
   at or below its ACTION_EMERGENCY + 5, so a hold in front of one would starve it. The predicate
   asks the avoid-aoe condition (AoeHazardEscapeDue) and every trigger in
   kSeekHealWaitEmergencyMoves (ReactionCastGuardPolicy.h), and each listed trigger/action pair
   still exists as an ACTION_EMERGENCY row in the strategy sources. The reverse is pinned too:
   every emergency row a reaction engine can carry (movement or not) is either listed or named
   with why it is not a healer's escape, and every name the predicate asks resolves through
   GetTrigger;
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
EMERGENCY = block(MOVEMENT_CPP, "bool ai::EmergencyMoveDue(")
REACTION_ROWS = (ROOT / "ai/playerbot/ReactionRowsPolicy.h").read_text()
STRATEGY_SOURCES = "".join(
    p.read_text() for p in sorted((ROOT / "ai/playerbot/strategy").rglob("*.cpp")))

# Every emergency movement row names its trigger and action in this shape; Viscidus puts both on one line.
SOURCE_ROW = re.compile(
    r'"([^"]+)"\s*,\s*\n?\s*NextAction::array\(0, new NextAction\("([^"]+)",\s*'
    r'(ACTION_EMERGENCY(?:\s*\+\s*\d+)?|[\d.]+f)')
REACTION_ROW = re.compile(
    r'\{\s*"([^"]+)",\s*"([^"]+)",\s*kReactionRaidEmergency\s*\+\s*\d+\s*\}')

ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()

# Every action a row can name; a row whose action has no creator could never run.
ACTION_NAMES = set(re.findall(r'creators\["([^"]+)"\]', ACTION_CONTEXT))
# GetTrigger (TriggerContext.h) resolves a trigger by name; every listed name must be there.
TRIGGER_NAMES = set(re.findall(r'creators\["([^"]+)"\]', TRIGGER_CONTEXT))


def is_emergency(relevance):
    return relevance == "ACTION_EMERGENCY" or relevance.startswith("ACTION_EMERGENCY +") or \
        float(relevance.rstrip("f")) >= 90.0


def function_body(text, start):
    """`text` from `start` (just past an opening brace) to its matching close, comments/strings skipped."""
    depth, i, state = 1, start, None
    while i < len(text):
        ch = text[i]
        if state == "line":
            if ch == "\n":
                state = None
        elif state == "block":
            if text.startswith("*/", i):
                state, i = None, i + 1
        elif state in ("string", "char"):
            if ch == "\\":
                i += 1
            elif (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = None
        elif text.startswith("//", i):
            state, i = "line", i + 1
        elif text.startswith("/*", i):
            state, i = "block", i + 1
        elif ch == '"':
            state = "string"
        elif ch == "'":
            state = "char"
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
        i += 1
    raise ValueError("unbalanced braces in a trigger-init body")


def reaction_trigger_rows():
    """Every emergency row a reaction engine can carry, by (trigger, action).

    Scans each InitReactionTriggers body and follows its own calls (InitCombatTriggers,
    another strategy's InitReactionTriggers), so a row the reaction copy registers that way is
    found too. The `react` strategy's rows are data (ReactionRowsPolicy.h) and are added here.
    """
    init = re.compile(r"void\s+(\w+)::(Init\w+Triggers)\s*\([^)]*\)\s*\{")
    bodies = {}
    for source in sorted((ROOT / "ai/playerbot/strategy").rglob("*.cpp")):
        text = source.read_text()
        for match in init.finditer(text):
            bodies[(match.group(1), match.group(2))] = function_body(text, match.end())
    calls = (
        re.compile(r"(\w+)::(Init\w+Triggers)\s*\(\s*triggers\s*\)"),
        re.compile(r"(\w+Strategy)\(ai\)\.(Init\w+Triggers)\s*\(\s*triggers\s*\)"),
        re.compile(r"(?<![:.\w])(Init\w+Triggers)\s*\(\s*triggers\s*\)"),
    )
    seen, rows = set(), {}
    for owner, method in bodies:
        if method != "InitReactionTriggers":
            continue
        stack = [(owner, method)]
        while stack:
            key = stack.pop()
            if key in seen or key not in bodies:
                continue
            seen.add(key)
            body = bodies[key]
            for trigger, action, relevance in SOURCE_ROW.findall(body):
                if is_emergency(relevance):
                    rows[(trigger, action)] = relevance
            stack += [match for match in calls[0].findall(body)]
            stack += [match for match in calls[1].findall(body)]
            stack += [(key[0], match) for match in calls[2].findall(body)]
    for trigger, action in REACTION_ROW.findall(REACTION_ROWS):
        rows[(trigger, action)] = "kReactionRaidEmergency"
    return rows


# Reaction-carried emergency rows the seek's heal wait must NOT stand down for, with why. A row here is still a real
# emergency row; it just is not a healer's escape (a hold, a potion, a tank action, the seek itself, spacing).
NOT_A_HEALER_ESCAPE = {
    # The avoid-aoe condition itself: the wait already asks it through AoeHazardEscapeDue, whose trigger also
    # registers the creature-aura keep-outs, so it is deliberately not in the list.
    ("aoe threat nearby", "avoid aoe"): "asked through AoeHazardEscapeDue",
    # The seek's own row: the wait is the seek standing down, so it cannot yield to itself.
    ("boss tactics seek", "boss tactics seek"): "the seek's own row",
    # Ranged spacing, not an escape: the spread action yields to a healer's cast itself.
    ("raid spread needed", "raid spread"): "spacing, not an escape",
    # Tank actions: a healer never carries one.
    ("boss tactics tank swap", "boss tactics tank swap"): "tank swap",
    ("fankriss swap needed", "fankriss swap"): "fankriss tank row",
    ("fankriss spawn pickup needed", "fankriss spawn pickup"): "fankriss tank row",
    # A hand of protection on the immune boss's victim: no movement, and a healer can cast it.
    ("immune boss victim protect", "bop immune boss victim"): "a buff, not an escape",
    # The melee soaker's hold, gated on the `patchwerk melee safety` strategy a healer does not carry.
    ("patchwerk melee unsafe", "patchwerk melee retreat"): "patchwerk melee soaker row",
    # Reaction-only holds and casts that move nobody.
    ("boss tactics freeze", "boss tactics freeze"): "a hold",
    ("heal target full health", "interrupt current spell"): "an interrupt",
    ("medium mana", "dark rune"): "a potion",
    ("medium mana", "mana potion"): "a potion",
    ("high mana", "mana potion"): "a potion",
}


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

    def test_a_due_emergency_move_outranks_the_wait(self):
        # The seek is picked ahead of every emergency row at or below its ACTION_EMERGENCY + 5, so it asks the
        # shared predicate while it builds the pass, before the rule can grant the wait.
        ASK = "in.hazardDue = ai::SeekHealWaitDue(in) && ai::EmergencyMoveDue(ai);"
        self.assertIn(ASK, WAIT)
        ask = WAIT.index(ASK)
        # All inputs first (the rule reads inWait/msSinceWaitEnd), then the emergency question, then the grant.
        self.assertGreater(ask, WAIT.index("in.msSinceWaitEnd = waitEndedMsAgo;"))
        self.assertLess(ask, WAIT.index("if (ai::SeekHealWaitDue(in))"))
        self.assertLess(ask, WAIT.index("return false;"))
        # The rule refuses the wait outright for it (the policy test proves every listed kind cuts the wait).
        self.assertIn("if (in.hazardDue) return false;", POLICY)

    def test_the_emergency_move_predicate_checks_every_listed_trigger(self):
        # One predicate, declared beside the hazard condition and asking the same sensors plus each trigger.
        self.assertIn("bool EmergencyMoveDue(PlayerbotAI* ai);", MOVEMENT_H)
        self.assertEqual(MOVEMENT_CPP.count("bool ai::EmergencyMoveDue("), 1)
        self.assertIn("if (AoeHazardEscapeDue(ai, bot))", EMERGENCY)
        self.assertIn("for (ai::SeekHealWaitEmergencyMove const& move : ai::kSeekHealWaitEmergencyMoves)",
                      EMERGENCY)
        self.assertIn("context->GetTrigger(move.trigger)", EMERGENCY)
        self.assertIn("trigger->IsActive()", EMERGENCY)
        # `aoe threat nearby` is asked through the shared condition, not the trigger: the trigger also registers
        # the creature-aura keep-outs.
        self.assertNotIn('"aoe threat nearby"', EMERGENCY)

        # Every listed trigger/action pair is still an emergency row in a strategy source (or in the reaction
        # rows' policy list), at ACTION_EMERGENCY or above.
        source_rows = {}
        for trigger, action, relevance in SOURCE_ROW.findall(STRATEGY_SOURCES):
            if relevance == "ACTION_EMERGENCY" or relevance.startswith("ACTION_EMERGENCY +") or \
                    float(relevance.rstrip("f")) >= 90.0:
                source_rows[(trigger, action)] = relevance
        for trigger, action in REACTION_ROW.findall(REACTION_ROWS):
            source_rows[(trigger, action)] = "kReactionRaidEmergency"
        listed = re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', POLICY)
        self.assertCountEqual(listed, [
            ("has area debuff", "flee from area"),
            ("immune boss victim", "immune boss victim escape"),
            ("environmental hazard nearby", "move away from hazard"),
            ("raid bomb debuff", "raid bomb runout"),
            # The same bomb trigger on Vael drives the carrier's own flee, so the list carries both rows.
            ("raid bomb debuff", "vael adrenaline flee"),
            ("four horsemen mark", "four horsemen rotate"),
            ("dragon breath risk", "dragon flank"),
            ("boss tactics flee", "boss tactics flee"),
            ("boss tactics avoid cast", "boss tactics avoid cast"),
            ("razorgore cone", "escape razorgore cone"),
            ("razorgore ranged", "back off razorgore"),
            ("golemagg splash", "back off golemagg"),
            ("magmadar lava bomb", "move away from hazard"),
            ("geddon inferno", "move away from geddon"),
            ("onyxia deep breath", "onyxia breath safe zone"),
            ("grobbulus injection ranged", "grobbulus go behind"),
            ("solnius emerald rot", "raid bomb runout"),
            ("viscidus melee danger", "viscidus keep distance"),
            ("viscidus toxin nearby", "viscidus toxin escape"),
        ])
        for trigger, action in listed:
            self.assertIn((trigger, action), source_rows,
                          f"no ACTION_EMERGENCY row for {trigger!r} -> {action!r}")

    def test_a_wait_that_ran_out_is_closed_before_the_rule_reads_it(self):
        # The wait's end is recorded at its own untilMs before in.msSinceWaitEnd and the rule: a heal already under
        # way on the first pass past the slack cannot start a fresh wait inside the rewait spacing.
        settle = "ai::SettleSeekHealWait(nowMs, healWaitUntilMs, healWaitActive, healWaitEndMs, healWaitEnded);"
        self.assertIn(settle, WAIT)
        self.assertIn("inline void SettleSeekHealWait(", POLICY)
        self.assertIn("endMs = untilMs;", POLICY)
        self.assertIn("nowMs <= untilMs + kSeekHealWaitEndSlackMs", POLICY)
        close = WAIT.index(settle)
        self.assertLess(close, WAIT.index("const bool inWait ="))
        self.assertLess(close, WAIT.index("in.msSinceWaitEnd = waitEndedMsAgo;"))
        self.assertLess(close, WAIT.index("SeekHealWaitDue(in)"))
        # Only a wait's first pass sets its end; a heal read inside the slack leaves untilMs alone.
        self.assertEqual(WAIT.count("healWaitUntilMs = nowMs + rem;"), 1)
        # A wait the rule cuts short (an emergency came due) still ends at the pass's clock.
        self.assertIn("healWaitEndMs = nowMs;", WAIT)

    def test_every_emergency_reaction_row_is_listed_or_excluded(self):
        # The reverse of the list check: a future escape row cannot be missed silently. Every emergency row a reaction
        # engine can carry is either asked by EmergencyMoveDue or named here with why it is not a healer's escape.
        rows = reaction_trigger_rows()
        self.assertTrue(rows, "no reaction rows found: the InitReactionTriggers scan broke")
        # Anchors for the scan's own reach: avoid aoe's InitCombatTriggers copy, the BWL reaction copy and boss
        # tactics' own reaction rows.
        for anchor in (("has area debuff", "flee from area"),
                       ("raid bomb debuff", "vael adrenaline flee"),
                       ("boss tactics seek", "boss tactics seek")):
            self.assertIn(anchor, rows, f"the reaction scan no longer reaches {anchor!r}")
        for trigger, action in rows:
            self.assertIn(action, ACTION_NAMES, f"no action creator for {action!r}")
        listed = set(re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', POLICY))
        uncovered = sorted(pair for pair in rows
                           if pair not in listed and pair not in NOT_A_HEALER_ESCAPE)
        self.assertEqual(uncovered, [],
                         "emergency reaction rows with no wait decision: " + repr(uncovered))
        stale = sorted(pair for pair in NOT_A_HEALER_ESCAPE if pair not in rows)
        self.assertEqual(stale, [], f"excluded rows that are no longer reaction rows: {stale}")
        unreachable = sorted(pair for pair in listed if pair not in rows)
        self.assertEqual(unreachable, [], f"listed rows no reaction engine carries: {unreachable}")
        # Every name the predicate asks resolves through GetTrigger (TriggerContext.h); GetTrigger returns NULL for
        # an unregistered name, and EmergencyMoveDue then silently asks nothing.
        for trigger in {pair[0] for pair in listed} | {pair[0] for pair in NOT_A_HEALER_ESCAPE}:
            self.assertIn(trigger, TRIGGER_NAMES, f"no GetTrigger registration for {trigger!r}")

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
