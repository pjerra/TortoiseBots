"""The free-bot target rule is ForeverCore's, and the Turtle pick is unchanged.

W1's `policy/world/FreeGrindPolicy.h` (a free bot only STARTS a fight with an
ordinary hostile or neutral NPC) is a rule for the ForeverCore host bots. On
Turtle `RandomBotFacade::IsFreeBot` is also true for every pool random bot and
every free alt, and those bots may today pick an opposing-faction player or
another NPC: applying the rule there would change what the pool fights. The
both-core pair therefore carries the gate, and both new-pull paths of
`GrindTargetValue` ask it before the rule:

- `ai/cmangos-compat-w1.h` (Turtle) answers false, so the scan's original
  answer stands - the same expression the code always made;
- `forever/src/compat/TbCompat_W1.h` (ForeverCore) answers true, so the rule
  runs there;
- the gate is tested first in the possible-targets pick and in the idle
  fallback, so no fact is read for a Turtle bot.

The rule itself is `forever/tools/test_free_grind_policy.cpp`; its wiring on
the Forever side is `forever/tools/test_w1_wiring.py`.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GRIND = (ROOT / "ai/playerbot/strategy/values/GrindTargetValue.cpp").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-w1.h").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_W1.h").read_text()
GATE_T = "inline bool FreeBotStartFightRuleCompat()\n{\n    return false;\n}"
GATE_F = "inline bool FreeBotStartFightRuleCompat()\n{\n    return true;\n}"


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class FreeGrindTargetWiringTest(unittest.TestCase):
    def test_the_turtle_side_answers_the_original_way(self):
        self.assertIn(GATE_T, COMPAT_T)
        self.assertNotIn(GATE_T, COMPAT_F)
        self.assertIn(GATE_F, COMPAT_F)

    def test_both_pull_paths_test_the_gate_before_the_rule(self):
        for fn in (body(GRIND, "Unit* GrindTargetValue::FindTargetForGrinding(", "\n}\n"),
                   body(GRIND, "Unit* GrindTargetValue::FindIdleFallbackTarget(", "\n}\n")):
            self.assertIn("if (FreeBotStartFightRuleCompat() && sRandomBotFacade.IsFreeBot(bot) && "
                          "!ai->HasRealPlayerMaster()", fn)
            self.assertLess(fn.index("FreeBotStartFightRuleCompat()"), fn.index("FreeBotMayStartFight("))


if __name__ == "__main__":
    unittest.main()
