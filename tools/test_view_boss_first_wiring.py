"""Wiring pin for the boss pass in the commander view snapshot: a boss any bot
fights keeps one of the 24 mob rows, however many adds took rows before it.

The order is glue code over game units, so it is pinned here: the boss pass runs
before the exact-victim pass in commands/Commander.cpp's CollectViewSnapshot, a
boss-only AddViewHostile call rejects a non-boss before it takes a row, and the
rank letter has one source (ViewRankOf) for both the view snapshot and the
Dungeons tab.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BossRowWiring(unittest.TestCase):
    def test_boss_pass_runs_before_the_victim_pass(self):
        fn = body(CMD, "void CollectViewSnapshot(")
        after_clear = fn[fn.index("taken.Clear();"):]
        first = after_clear.index("AddViewHostile(")
        boss_call = after_clear[first:after_clear.index("\n", first)]
        self.assertTrue(boss_call.endswith(", false, true);"), boss_call)
        victim = after_clear.index("AddViewHostile(snapshot, taken, bot, victim->GetObjectGuid())")
        self.assertLess(first, victim)

    def test_boss_only_is_checked_before_a_row_is_taken(self):
        fn = body(CMD, "void AddViewHostile(")
        self.assertIn("bossOnly && ViewRankOf(unit) != 'b'", fn)
        self.assertLess(fn.index("bossOnly && ViewRankOf(unit) != 'b'"), fn.index("taken.Add("))

    def test_rank_has_one_source(self):
        self.assertEqual(CMD.count("CommanderTelemetry::RankKind("), 1)


if __name__ == "__main__":
    unittest.main()
