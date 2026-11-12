"""The regroup's release gate AiPlayerbot.DungeonRunRegroupWaitsForLast (DungeonRunPolicy.h RegroupReleaseWaitsForLast).

The pure rule is tested in tools/test_dungeon_run_policy.cpp; this pins the key's wiring and the two call sites:
the config key (default off), the glue feeding Facts::regroupWaitsForLast, and both places where the regroup sends
`revive party` (the main revive and the lead bot in another part of the instance) asking the rule before the order.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RegroupWaitsForLastWiringTest(unittest.TestCase):
    def test_key_defaults_off_everywhere(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn(
            'dungeonRunRegroupWaitsForLast = config.GetBoolDefault("AiPlayerbot.DungeonRunRegroupWaitsForLast", false);',
            keys)
        self.assertIn("bool dungeonRunRegroupWaitsForLast;", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRegroupWaitsForLast = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunRegroupWaitsForLast`", SETTINGS)
        self.assertIn("[`AiPlayerbot.DungeonRunRegroupWaitsForLast`](#aiplayerbotdungeonrunregroupwaitsforlast) | `0`",
            SETTINGS)

    def test_the_glue_feeds_the_facts(self):
        self.assertIn("bool regroupWaitsForLast = false;", POLICY)
        self.assertIn("f.regroupWaitsForLast = sPlayerbotAIConfig.dungeonRunRegroupWaitsForLast;", COMMANDER)

    def test_the_main_revive_asks_the_rule_before_it_releases(self):
        code = strip_comments(POLICY)
        revive = body(code, "if (f.dead > 0 || !f.leadAlive)",
            "if (!PartyGathered(f) && !f.anyInCombat && !r.gatherWaived)")
        gate = revive.find("if (RegroupReleaseWaitsForLast(r, f))")
        self.assertGreaterEqual(gate, 0)
        self.assertLess(gate, revive.index("r.reviveSent = true;"))
        self.assertIn("the last one still fights the boss (no release yet)", code)
        # The wait keeps the dead's own 240 s ceiling: past it the run pauses instead of waiting forever.
        self.assertIn("the last one still fighting the boss (cmd revive <bot>, then .bot run resume)", revive)

    def test_the_other_part_revive_asks_the_rule_too(self):
        code = strip_comments(POLICY)
        other = body(code, "r.note = \"regroup: the lead bot revived in another part of the instance: waiting for \"",
            'order = "revive party";')
        gate = other.find("if (RegroupReleaseWaitsForLast(r, f))")
        self.assertGreaterEqual(gate, 0)
        self.assertLess(gate, other.index("r.reviveSent = true;"))

    def test_the_rule_reads_the_boss_and_the_members_place(self):
        rule = body(strip_comments(POLICY), "inline bool RegroupReleaseWaitsForLast(", "\n}\n")
        self.assertIn("if (!f.regroupWaitsForLast || !r.plan)", rule)
        self.assertIn("bool living = f.alive > 0;", rule)
        self.assertIn("if (EngagedPlanBoss(r, f))", rule)
        self.assertIn("for (std::size_t i = r.step; i < r.plan->steps.size(); ++i)", rule)
        self.assertIn("s.kind == PlanStepKind::Boss && s.radius > 0.0f", rule)
        self.assertIn("ai::PlanDistance(m.x, m.y, m.z, s.x, s.y, s.z) <= s.radius", rule)
        # The place covers the fight-read flap only: no member left standing in a boss's room holds the dead for ever.
        self.assertIn("if (r.regroupMs > kRegroupReleasePlaceMs)", rule)


if __name__ == "__main__":
    unittest.main()
