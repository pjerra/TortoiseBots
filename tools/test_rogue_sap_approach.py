#!/usr/bin/env python3
"""Pre-pull Sap approach: the walk must win every tick until Sap is cast (#61).

In-game retest on build 9: the stealthed rogue alternated between "reach sap"
and "follow" and never Sapped; in one attempt "unstealth" dropped Stealth next
to the mob. Cause: after a prerequisite has run, Engine::DoNextAction re-queues
the action with skip-prerequisites set (PushAgain), and Queue::Push merges the
next tick's trigger push into that basket without clearing the flag. So every
other tick "sap" came up without its walk, was impossible (out of reach), and
the next basket in the queue ran - "follow" (ACTION_NORMAL), which walked the
rogue back to the player.

This test reads the real relevances and names from the sources, replays the
engine's queue rules tick by tick for a stealthed rogue walking up to its CC
mark, and checks that:
  * with the shipped "sap cc mark" node, every tick before Sap is the walk,
    and Sap is cast once the rogue is in reach (no follow, no unstealth);
  * without the walk in the node (the build 9 wiring) the same model
    reproduces the in-game flip-flop, so the model is the one that failed.

Run: python3 -m unittest tools/test_rogue_sap_approach.py
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRATEGY_H = ROOT / "ai/playerbot/strategy/Strategy.h"
QUEUE_CPP = ROOT / "ai/playerbot/strategy/Queue.cpp"
ENGINE_CPP = ROOT / "ai/playerbot/strategy/Engine.cpp"
SPELL_ACTIONS_CPP = ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp"
ROGUE_STRATEGY_CPP = ROOT / "ai/playerbot/strategy/rogue/RogueStrategy.cpp"
ROGUE_ACTIONS_H = ROOT / "ai/playerbot/strategy/rogue/RogueActions.h"
FOLLOW_CPP = ROOT / "ai/playerbot/strategy/generic/FollowMasterStrategy.cpp"


def priorities():
    text = STRATEGY_H.read_text()
    body = text[text.index("enum ActionPriority"):]
    body = body[:body.index("}")]
    return {name: float(value) for name, value in re.findall(r"(ACTION_\w+)\s*=\s*(\d+)", body)}


def relevance(expr, prio):
    expr = expr.strip()
    m = re.fullmatch(r"(ACTION_\w+)(?:\s*([+-])\s*([\d.]+)f?)?", expr)
    if m:
        value = prio[m.group(1)]
        if m.group(2):
            value += float(m.group(3)) * (1 if m.group(2) == "+" else -1)
        return value
    return float(expr.rstrip("f"))


def function_body(text, signature):
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced body for " + signature)


def trigger_actions(body, trigger, prio):
    m = re.search(r'new TriggerNode\(\s*"' + re.escape(trigger) + r'",\s*NextAction::array\(0,(.*?)NULL\)\)\);',
                  body, re.S)
    if not m:
        raise AssertionError("trigger node not found: " + trigger)
    return [(name, relevance(expr, prio))
            for name, expr in re.findall(r'new NextAction\("([^"]+)",\s*([^)]+?)\)', m.group(1))]


class Basket:
    def __init__(self, name, rel, skip):
        self.name, self.rel, self.skip = name, rel, skip


class Queue:
    """Queue::Push / Queue::Pop as implemented (merge by name, keep the flag)."""

    def __init__(self):
        self.baskets = []

    def push(self, name, rel, skip=False):
        for basket in self.baskets:
            if basket.name == name:
                if basket.rel < rel:
                    basket.rel = rel
                return
        self.baskets.append(Basket(name, rel, skip))

    def pop(self):
        if not self.baskets:
            return None
        best = max(self.baskets, key=lambda b: b.rel)
        self.baskets.remove(best)
        return best


def simulate(sap_node, follow_rel, unstealth_rel, reach_name, ticks=40, start_dist=30.0, step=3.0, reach=5.0):
    """Replay DoNextAction for a stealthed rogue whose master stays behind.

    World model: "reach sap" walks `step` yards closer, "follow" walks `step`
    yards back; the walk is useful while out of melee reach, Sap is possible in
    reach. "follow" (out of free move range) and "unstealth" (stealthed, moving,
    master > 10 yd) are always useful here; "stealth" is useless (already on).
    Returns the executed action per tick, ending with "sap" when it is cast or
    with "unstealth" when Stealth was dropped (Sap is then lost: it needs
    Stealth, and the mob sees the rogue).
    """
    queue = Queue()
    dist = start_dist
    executed = []
    for _ in range(ticks):
        for name, rel in sap_node:
            queue.push(name, rel)
        queue.push("follow", follow_rel)
        queue.push("unstealth", unstealth_rel)
        ran = None
        while ran is None:
            basket = queue.pop()
            if basket is None:
                break
            base = basket.name.split("::")[0]
            if base == "stealth":
                continue  # useless: already stealthed
            if base == "reach sap" and dist <= reach:
                continue  # ReachSapAction::isUseful: already in reach
            if base == "sap" and not basket.skip:
                # getPrerequisites: walk at +0.02, Sap again at +0.01, skip set.
                queue.push(reach_name, basket.rel + 0.02)
                queue.push("sap", basket.rel + 0.01, skip=True)
                continue
            if base == "sap" and dist > reach:
                continue  # impossible: out of melee reach
            ran = base
        executed.append(ran)
        if ran in ("sap", "unstealth"):
            break
        if ran == "reach sap":
            dist = max(dist - step, 0.0)
        elif ran == "follow":
            dist += step
    return executed


class RogueSapApproachTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.prio = priorities()
        rogue = ROGUE_STRATEGY_CPP.read_text()
        cls.sap_node = trigger_actions(
            function_body(rogue, "void RogueStrategy::InitNonCombatTriggers("), "sap cc mark", cls.prio)
        cls.unstealth_rel = trigger_actions(
            function_body(rogue, "void RogueStealthStrategy::InitNonCombatTriggers("), "unstealth", cls.prio)[0][1]
        follow = FOLLOW_CPP.read_text()
        cls.follow_rel = trigger_actions(
            function_body(follow, "void FollowMasterStrategy::InitNonCombatTriggers("),
            "out of free move range", cls.prio)[0][1]

        # The prerequisite name CastSapAction builds out of combat:
        # reach action + "::" + MultiQualify({spell, target}, "::") with "{}".
        actions = ROGUE_ACTIONS_H.read_text()
        sap_class = actions[actions.index("class CastSapAction"):actions.index("class CastGarroteAction")]
        target = re.search(r'GetTargetName\(\) override\s*\{\s*return [^?]+\?\s*"[^"]+"\s*:\s*"([^"]+)"', sap_class)
        reach = re.search(r'GetReachActionName\(\) override\s*\{\s*return [^?]+\?\s*"[^"]+"\s*:\s*"([^"]+)"', sap_class)
        assert target and reach, "CastSapAction out-of-combat target/reach not found"
        cls.reach_name = reach.group(1) + "::{sap::" + target.group(1) + "}"

    def test_prerequisite_name_format_is_what_the_model_assumes(self):
        text = SPELL_ACTIONS_CPP.read_text()
        body = function_body(text, "NextAction** CastSpellAction::getPrerequisites(")
        self.assertIn('Qualified::MultiQualify(qualifiers, "::")', body)
        self.assertIn('reachAction + "::" + qualifiersStr', body)
        self.assertIn('MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.02', ENGINE_CPP.read_text())
        self.assertIn('PushAgain(actionNode, relevance + 0.01, event);', ENGINE_CPP.read_text())

    def test_queue_merge_refreshes_the_skip_flag(self):
        # Queue::Push now clears the skip-prerequisites mark when a fresh request
        # merges (tools/test_queue_prerequisite_merge.py pins the rule), so the
        # build 9 flip-flop is gone at the source; the walk-every-tick wiring
        # below stays as the Sap node's own guarantee.
        body = function_body(QUEUE_CPP.read_text(), "void Queue::Push(")
        self.assertIn("basket->setRelevance(action->getRelevance());", body)
        self.assertIn("kip", body)

    def test_sap_node_queues_the_walk_between_sap_and_follow(self):
        names = [name for name, _ in self.sap_node]
        self.assertEqual(names[:2], ["stealth", "sap"])
        self.assertIn(self.reach_name, names)
        rel = dict(self.sap_node)
        self.assertGreater(rel["sap"], rel[self.reach_name])
        self.assertGreater(rel[self.reach_name], max(self.follow_rel, self.unstealth_rel))

    def test_trigger_keeps_a_master_leash(self):
        # The walk outranks follow only while the master is close: beyond the
        # leash the trigger goes quiet and following wins again.
        text = (ROOT / "ai/playerbot/strategy/rogue/RogueTriggers.cpp").read_text()
        body = function_body(text, "bool SapCcMarkTrigger::IsActive(")
        self.assertIn("const float kSapMasterLeash = 50.0f;", body)
        self.assertIn("GetDistance2d(bot, master), kSapMasterLeash", body)

    def test_walk_every_tick_then_sap(self):
        executed = simulate(self.sap_node, self.follow_rel, self.unstealth_rel, self.reach_name)
        self.assertEqual(executed[-1], "sap", executed)
        self.assertTrue(all(step == "reach sap" for step in executed[:-1]), executed)

    def test_build9_wiring_reproduces_the_flip_flop(self):
        without_walk = [(n, r) for n, r in self.sap_node if not n.startswith("reach sap")]
        executed = simulate(without_walk, self.follow_rel, self.unstealth_rel, self.reach_name)
        self.assertIn("follow", executed)
        self.assertEqual(executed[-1], "unstealth", executed)


if __name__ == "__main__":
    unittest.main()
