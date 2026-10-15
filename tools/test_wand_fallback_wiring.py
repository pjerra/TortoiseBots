"""Mage nukes no longer fall back to the wand; the raid wand row needs a raid group.

On Turtle each wand shot renews the GCD (ai/playerbot/WandPolicy.h), so a nuke
whose "shoot" alternative fired once on a GCD refusal kept the wand going
(Zul'Farrak: frost mages wanded 30-33% of the fight at 76-83% mana). The
"no mana" row still wands below 5% mana. The raid "has aggro -> shoot" row
runs in every dungeon (PlayerbotAI::IsInRaid), so it asks for a real raid
group, the same test as the wand stop's exception (RaidAggroWand).
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MAGE = (STRATEGY / "mage/MageStrategy.cpp").read_text()
ARCANE = (STRATEGY / "mage/ArcaneMageStrategy.cpp").read_text()
WARLOCK = (STRATEGY / "warlock/WarlockStrategy.cpp").read_text()
TRIGGERS = (STRATEGY / "triggers/GenericTriggers.cpp").read_text()
CONTEXT = (STRATEGY / "triggers/TriggerContext.h").read_text()
SPELL_ACTIONS = (STRATEGY / "actions/GenericSpellActions.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class WandFallbackWiringTest(unittest.TestCase):
    def test_mage_nukes_have_no_shoot_alternative(self):
        for text, node in ((MAGE, "frostbolt"), (ARCANE, "arcane missiles")):
            self.assertNotRegex(text, r'ACTION_NODE_A\(\w+,\s*"%s",\s*"shoot"\)' % node)
            fn = body(text, 'return new ActionNode("%s",' % node, ");")
            self.assertNotIn("shoot", fn)
        for path in (STRATEGY / "mage").glob("*Strategy.cpp"):
            if path.name == "GenericMageStrategy.cpp":
                continue  # mod-playerbots port, not registered by MageAiObjectContext
            self.assertNotRegex(path.read_text(), r'ACTION_NODE_A\([^)]*"shoot"\)', path.name)

    def test_no_mana_row_still_wands(self):
        self.assertRegex(MAGE, r'"no mana",\s*NextAction::array\(0, new NextAction\("shoot", ACTION_NORMAL\)')

    def test_raid_aggro_row_needs_raid_group(self):
        for text, cls in ((MAGE, "MageRaidStrategy"), (WARLOCK, "WarlockRaidStrategy")):
            fn = body(text, "void %s::InitCombatTriggers(" % cls)
            self.assertIn('"has aggro in raid group"', fn)
            self.assertNotRegex(fn, r'"has aggro",\s*NextAction::array\(0, new NextAction\("shoot"')
        self.assertIn('creators["has aggro in raid group"] = [](PlayerbotAI* ai) { return new HasAggroInRaidGroupTrigger(ai); };', CONTEXT)
        row = body(TRIGGERS, "bool HasAggroInRaidGroupTrigger::IsActive()")
        stop = body(TRIGGERS, "bool WandBlocksCastTrigger::RaidAggroWand()")
        for fn in (row, stop):
            self.assertIn("RaidAggroWandRow(group && group->isRaidGroup(),", fn)

    def test_stop_waits_for_spell_gcd_too(self):
        fn = body(SPELL_ACTIONS, "bool StopWandAction::Execute(Event& event)")
        self.assertIn("WandStopWaitMs(bot->GetAttackTimer(RANGED_ATTACK), ai->SpellGlobalCooldownRemainingMs(), sPlayerbotAIConfig.reactDelay)", fn)
        ai = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
        # Every own-cast path records the GCD right after a successful prepare.
        self.assertEqual(len(re.findall(r"NoteSpellGlobalCooldown\(pSpellInfo\);", ai)), 3)


if __name__ == "__main__":
    unittest.main()
