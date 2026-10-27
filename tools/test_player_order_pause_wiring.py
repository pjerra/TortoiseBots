"""A `cmd summon` and `cmd revive` from the commander keep the run going; every other player order still pauses it
(turtle-agent #270).

The policy (tools/test_dungeon_run_policy.cpp) decides; these checks pin that the decision lives in
`PlayerOrderKeepsRun` in runtime/DungeonRunPolicy.h, not in the glue: HandleCommanderOrder still hands every
non-`state` order to PauseDungeonRunForPlayer, and PauseForPlayer consults PlayerOrderKeepsRun.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PlayerOrderPauseWiringTest(unittest.TestCase):
    def test_summon_keeps_run_is_checked_inside_pause_for_player(self):
        pause = body(POLICY, "inline bool PauseForPlayer(Run& r, const std::string& order)", "\n}\n")
        self.assertLess(pause.index("if (r.state != State::Running)"),
                        pause.index("if (PlayerOrderKeepsRun(order))"))
        self.assertLess(pause.index("if (PlayerOrderKeepsRun(order))"),
                        pause.index("PausePlayer(r,"))

    def test_glue_still_hands_cmd_orders_to_the_pause(self):
        glue = body(COMMANDS, "bool HandleCommanderOrder(ChatHandler* handler, char const* args)", "\n}\n")
        self.assertIn('PauseDungeonRunForPlayer(order.commander->GetObjectGuid().GetRawValue(), "cmd " + verb);',
                      glue)


if __name__ == "__main__":
    unittest.main()
