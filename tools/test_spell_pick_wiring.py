#!/usr/bin/env python3
"""Wiring pins for the cast-spell pick (ai/playerbot/SpellPickPolicy.h).

The rule is behaviour-tested in tools/test_spell_pick_policy.cpp; there is no
seam to drive SpellIdValue without a bot and the core's spell store, so these
checks pin that the value still asks the rule instead of picking a rank itself:

- SpellIdValue::Calculate builds KnownSpellRank candidates from the rank text
  and returns PickCastSpellId(candidates) on the normal (no mana-save) path.
- No rank-less candidate can overwrite the resolved spell id any more: the old
  "highestSpellId = *itr; continue;" branch is gone.

Run: python3 tools/test_spell_pick_wiring.py
"""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALUE = ROOT / "ai/playerbot/strategy/values/SpellIdValue.cpp"
POLICY = ROOT / "ai/playerbot/SpellPickPolicy.h"


class CastSpellPickWiring(unittest.TestCase):
    def setUp(self):
        self.value = VALUE.read_text(encoding="utf-8")
        self.policy = POLICY.read_text(encoding="utf-8")

    def test_the_value_returns_the_policy_pick(self):
        self.assertIn('#include "playerbot/SpellPickPolicy.h"', self.value)
        self.assertIn("std::vector<KnownSpellRank> candidates;", self.value)
        self.assertIn("candidates.push_back({ *itr, SpellRankNumber(pSpellInfo->Rank[0]) });", self.value)
        self.assertIn("return PickCastSpellId(candidates);", self.value)

    def test_a_rankless_spell_no_longer_overwrites_the_pick(self):
        self.assertNotIn("highestSpellId = *itr;", self.value)
        self.assertNotIn("int highestRank", self.value)

    def test_the_policy_keeps_ranked_spells_ahead(self):
        self.assertIn("uint32_t PickCastSpellId(std::vector<KnownSpellRank> const& known)", self.policy)
        self.assertIn("return ranked ? ranked : unranked;", self.policy)


if __name__ == "__main__":
    unittest.main()
