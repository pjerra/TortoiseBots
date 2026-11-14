"""Wiring pins for the reaction cast guard (AiPlayerbot.ReactionHealCastGuardPct).

UpdateAIReaction breaks a bot's cast as soon as FindReaction PICKS a reaction whose
action says it may, before that action's Execute can find it has nothing to do. The
guard rule lives in ai/playerbot/ReactionCastGuardPolicy.h (ReactionCastBreakDue,
ReactionCastPct; behaviour in tools/test_reaction_cast_guard_policy.cpp). These pins
only prove the live code feeds it and that nothing else changed:

 - the engine reads the picked action's ReactionYieldsToHealCast, which defaults to
   false on Action and is overridden to true by exactly two actions: the boss tactics
   seek and the raid spread. Every emergency (and every other movement action) keeps
   breaking the cast;
 - the call site still breaks the cast only through ReactionCastBreakDue, with the
   picked reaction's own ShouldInterruptCast, the bot's healer role, the generic
   cast's progress (ReactionCastPct over Spell::GetCastTime/GetCastedTime) and its
   heal effects;
 - the key is read with default 0, clamped to 0-100, beside DungeonRunBossSpreadSlots
   in the loader the reload path re-runs, and the shipped template sets 0.

Run: python3 -m unittest tools/test_reaction_cast_guard_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/ReactionCastGuardPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_reaction_cast_guard_policy.cpp").read_text()
ACTION_H = (ROOT / "ai/playerbot/strategy/Action.h").read_text()
REACTION_ENGINE = (ROOT / "ai/playerbot/strategy/ReactionEngine.h").read_text()
MOVEMENT_H = (ROOT / "ai/playerbot/strategy/actions/MovementActions.h").read_text()
SEEK_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
DUNGEON_H = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()

YIELD_TRUE = "bool ReactionYieldsToHealCast() const override { return true; }"


def body(text, start, end="\n    };"):
    """The text from `start` to the class body's end."""
    i = text.index(start)
    return text[i:text.index(end, i)]


class ReactionCastGuardWiring(unittest.TestCase):
    def test_action_default_does_not_yield(self):
        # The base says no: only the actions that opt in stand down for a heal.
        self.assertIn("virtual bool ReactionYieldsToHealCast() const { return false; }", ACTION_H)
        # A movement action still breaks the cast when picked, as before.
        self.assertIn("bool ShouldReactionInterruptCast() const override { return true; }", MOVEMENT_H)
        self.assertNotIn("ReactionYieldsToHealCast", MOVEMENT_H)

    def test_exactly_seek_and_spread_yield(self):
        self.assertIn(YIELD_TRUE, body(SEEK_H, "class BossTacticsSeekAction"))
        self.assertIn(YIELD_TRUE, body(DUNGEON_H, "class RaidSpreadAction"))
        overrides = [p.read_text() for p in sorted((ROOT / "ai").rglob("*.h"))]
        self.assertEqual(sum(t.count(YIELD_TRUE) for t in overrides), 2)

    def test_engine_forwards_the_flag(self):
        self.assertIn("bool YieldsToHealCast() const { return action ? action->ReactionYieldsToHealCast() : false; }",
                      REACTION_ENGINE)

    def test_call_site_feeds_the_rule(self):
        call = PLAYERBOT_AI[PLAYERBOT_AI.index("bool PlayerbotAI::UpdateAIReaction"):]
        call = call[:call.index("\n}")]
        # One break site, and it goes through the policy with all six inputs.
        self.assertEqual(call.count("InterruptSpell()"), 1)
        self.assertIn("if (ReactionCastBreakDue(interruptCast, reaction->YieldsToHealCast(),", call)
        self.assertIn("sPlayerbotAIConfig.reactionHealCastGuardPct, IsHeal(bot), castIsHeal, castPct))", call)
        self.assertIn("const bool interruptCast = reaction->ShouldInterruptCast();", call)
        # The cast progress comes from the generic slot's own clock, and only while that cast really runs (a spell
        # left behind after its cast finished must not read as a heal under way).
        self.assertIn("Spell* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL)", call)
        self.assertIn("if (cast->getState() == SPELL_STATE_CASTING)", call)
        self.assertIn("castPct = ReactionCastPct(cast->GetCastTime(), cast->GetCastedTime());", call)
        # A heal by its effects: a direct heal or a periodic one.
        self.assertIn("castIsHeal = ReactionCastIsHeal(cast->m_spellInfo);", call)
        self.assertIn("entry->Effect[i] == SPELL_EFFECT_HEAL", PLAYERBOT_AI)
        self.assertIn("entry->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL", PLAYERBOT_AI)

    def test_policy_is_core_free_and_tested(self):
        self.assertNotIn("#include <game", POLICY)
        self.assertIn("inline bool ReactionCastBreakDue(", POLICY)
        self.assertIn("if (!shouldInterrupt) return false;", POLICY)
        self.assertIn("if (guardPct <= 0 || !yieldsToHeal || !botIsHealer || !castIsHeal || castPct < 0) return true;", POLICY)
        self.assertIn("return castPct < guardPct;", POLICY)
        # The case that fails on the old rule: guard 1, yielding, healer, heal 4 % in, no break.
        self.assertIn("CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, 4));", POLICY_TEST)

    def test_config_key(self):
        # Default 0: with the key off every reaction breaks as before.
        self.assertIn(
            'reactionHealCastGuardPct =\n'
            '        std::min(100, std::max(0, config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)));',
            CONFIG_CPP)
        self.assertIn("int reactionHealCastGuardPct = 0;", CONFIG_H)
        # Read in LoadDungeonRunKeys, which `.reload config` re-runs, beside the spread-slots key.
        loader = CONFIG_CPP[CONFIG_CPP.index("void PlayerbotAIConfig::LoadDungeonRunKeys()"):]
        loader = loader[:loader.index("\n}\n")]
        self.assertIn('dungeonRunBossSpreadSlots = config.GetBoolDefault("AiPlayerbot.DungeonRunBossSpreadSlots", false);', loader)
        self.assertIn('config.GetIntDefault("AiPlayerbot.ReactionHealCastGuardPct", 0)', loader)
        # The shipped template ships it off.
        self.assertRegex(TEMPLATE, re.compile(r"^# 0 - off: .*\n(?:#.*\n)*AiPlayerbot\.ReactionHealCastGuardPct = 0$",
                                              re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
