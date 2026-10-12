#!/usr/bin/env python3
"""A fresh request re-arms an action's prerequisites (Queue::Push merge).

Engine::DoNextAction runs an action's prerequisite (its walk-up: "reach
melee", "reach spell", "reach sap", ...) first, then puts the action back in
the queue marked "skip prerequisites" (PushAgain) and usually ends the tick
there, because the walk executed. On the next tick the trigger asks for the
action again and Queue::Push merges that request into the waiting basket.
When the merge kept the skip mark, every other tick the action came up
without its walk, was impossible (out of reach) and a lower action ran
instead: the in-game "reach sap" / "follow" flip-flop of a stealthed rogue
(build 9, fix 61 round 4).

This test reads the merge rule and the engine's push relevances from the
sources, replays the queue tick by tick for a bot walking up to a target
while a lower "follow" pulls it back, and checks that:
  * with the shipped merge rule every tick is the walk until the action is
    cast (no follow in between);
  * the old rule (keep the mark) reproduces the flip-flop, so the model is
    the one that failed;
  * inside one tick the mark still stops the prerequisite from being queued
    again (no loop once the walk is no longer useful).

Run: python3 -m unittest tools/test_queue_prerequisite_merge.py
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRATEGY_H = ROOT / "ai/playerbot/strategy/Strategy.h"
QUEUE_CPP = ROOT / "ai/playerbot/strategy/Queue.cpp"
ENGINE_CPP = ROOT / "ai/playerbot/strategy/Engine.cpp"
ACTION_H = ROOT / "ai/playerbot/strategy/Action.h"
FOLLOW_CPP = ROOT / "ai/playerbot/strategy/generic/FollowMasterStrategy.cpp"


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


def priorities():
    text = STRATEGY_H.read_text()
    body = text[text.index("enum ActionPriority"):]
    body = body[:body.index("}")]
    return {name: float(value) for name, value in re.findall(r"(ACTION_\w+)\s*=\s*(\d+)", body)}


def merge_clears_skip_mark():
    """True when Queue::Push lets a fresh (non-skip) push clear the mark."""
    body = function_body(QUEUE_CPP.read_text(), "void Queue::Push(")
    return bool(re.search(r"if\s*\(\s*!\s*action->isSkipPrerequisites\(\)\s*\)\s*basket->clearSkipPrerequisites\(\);",
                          body))


class Basket:
    def __init__(self, name, rel, skip):
        self.name, self.rel, self.skip = name, rel, skip


class Queue:
    """Queue::Push / Queue::Pop: merge by name, keep the higher relevance."""

    def __init__(self, fresh_push_clears_skip):
        self.baskets = []
        self.fresh_push_clears_skip = fresh_push_clears_skip

    def push(self, name, rel, skip=False):
        for basket in self.baskets:
            if basket.name == name:
                if basket.rel < rel:
                    basket.rel = rel
                if self.fresh_push_clears_skip and not skip:
                    basket.skip = False
                return
        self.baskets.append(Basket(name, rel, skip))

    def pop(self):
        if not self.baskets:
            return None
        best = max(self.baskets, key=lambda b: b.rel)
        self.baskets.remove(best)
        return best


def simulate(fresh_push_clears_skip, cast_rel, follow_rel, ticks=40, start_dist=30.0, step=3.0, reach=5.0):
    """Replay DoNextAction for a bot whose action needs a walk-up.

    World model: "walk" (the action's prerequisite) moves `step` yards closer
    and is useful while out of reach; "cast" is possible in reach; "follow"
    (the master stays behind) is always useful and moves `step` yards back.
    The trigger pushes "cast" every tick, the follow trigger pushes "follow".
    Returns (executed per tick, prerequisite pushes per tick).
    """
    queue = Queue(fresh_push_clears_skip)
    dist = start_dist
    executed = []
    prereq_pushes = []
    for _ in range(ticks):
        queue.push("cast", cast_rel)
        queue.push("follow", follow_rel)
        ran = None
        pushes = 0
        while ran is None:
            basket = queue.pop()
            if basket is None:
                break
            if basket.name == "walk" and dist <= reach:
                continue  # useless: already in reach
            if basket.name == "cast" and not basket.skip:
                # Engine: prerequisite at +0.02, the action again at +0.01, skip set.
                queue.push("walk", basket.rel + 0.02)
                queue.push("cast", basket.rel + 0.01, skip=True)
                pushes += 1
                continue
            if basket.name == "cast" and dist > reach:
                continue  # impossible: out of reach, no alternatives
            ran = basket.name
        executed.append(ran)
        prereq_pushes.append(pushes)
        if ran == "cast":
            break
        if ran == "walk":
            dist = max(dist - step, 0.0)
        elif ran == "follow":
            dist += step
    return executed, prereq_pushes


class QueuePrerequisiteMergeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        prio = priorities()
        cls.cast_rel = prio["ACTION_HIGH"] + 1
        follow = function_body(FOLLOW_CPP.read_text(), "void FollowMasterStrategy::InitNonCombatTriggers(")
        m = re.search(r'"out of free move range",\s*NextAction::array\(0, new NextAction\("follow", (ACTION_\w+)\)', follow)
        assert m, "follow node not found"
        cls.follow_rel = prio[m.group(1)]

    def test_engine_rules_the_model_assumes(self):
        engine = ENGINE_CPP.read_text()
        walk = function_body(engine, "bool Engine::DoNextAction(")
        self.assertIn("bool skipPrerequisites = basket->isSkipPrerequisites();", walk)
        self.assertIn('MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.02, false, event, "prereq")', walk)
        self.assertIn("PushAgain(actionNode, relevance + 0.01, event);", walk)
        # The prerequisite runs before the possibility check, and a tick ends
        # on the first executed action.
        self.assertLess(walk.index("if (!skipPrerequisites)"), walk.index("action->isPossible()"))
        again = function_body(engine, "void Engine::PushAgain(")
        self.assertIn('MultiplyAndPush(nextAction, relevance, true, event, "again");', again)
        triggers = function_body(engine, "void Engine::ProcessTriggers(")
        self.assertIn('MultiplyAndPush(node->getHandlers(), 0.0f, false, event, "trigger");', triggers)

    def test_merge_clears_the_mark_only_on_a_fresh_push(self):
        self.assertTrue(merge_clears_skip_mark(), "Queue::Push must clear the skip mark on a fresh push")
        body = function_body(QUEUE_CPP.read_text(), "void Queue::Push(")
        # The merge never sets the mark: a PushAgain merging into a fresh
        # basket must not make the fresh request skip its walk.
        self.assertNotRegex(body, r"skipPrerequisites\s*=\s*true")
        self.assertIn("void clearSkipPrerequisites() { skipPrerequisites = false; }", ACTION_H.read_text())

    def test_walk_every_tick_then_cast(self):
        executed, _ = simulate(merge_clears_skip_mark(), self.cast_rel, self.follow_rel)
        self.assertEqual(executed[-1], "cast", executed)
        self.assertTrue(all(step == "walk" for step in executed[:-1]), executed)

    def test_kept_mark_reproduces_the_flip_flop(self):
        executed, _ = simulate(False, self.cast_rel, self.follow_rel)
        self.assertNotIn("cast", executed)
        self.assertEqual(executed[:4], ["walk", "follow", "walk", "follow"], executed)

    def test_in_reach_casts_in_the_same_tick_without_a_loop(self):
        executed, pushes = simulate(merge_clears_skip_mark(), self.cast_rel, self.follow_rel, start_dist=4.0)
        self.assertEqual(executed, ["cast"])
        self.assertEqual(pushes, [1])

    def test_one_prerequisite_push_per_tick(self):
        # Same queue work per tick as before: one prerequisite push per tick
        # the action comes up fresh, never more.
        _, pushes = simulate(merge_clears_skip_mark(), self.cast_rel, self.follow_rel)
        self.assertTrue(all(n == 1 for n in pushes), pushes)


if __name__ == "__main__":
    unittest.main()
