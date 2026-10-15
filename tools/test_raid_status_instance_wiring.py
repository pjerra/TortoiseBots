"""`.bot action raid status` must not call an instance "outdoor" (issue #912).

With no raid combat strategy active the status used to print "outdoor" even in
Onyxia's Lair. It now says "instance map <id>, no raid strategy" inside an
instance and keeps "outdoor" only outside one.

Run: python3 -m unittest tools/test_raid_status_instance_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RaidStatusInstanceWiringTest(unittest.TestCase):
    def test_idle_text_depends_on_the_map(self):
        text = (ROOT / "commands/BotCommands.cpp").read_text()
        start = text.index('if (intent == "raid status")')
        block = text[start:start + 3000]
        self.assertIn("statusMap->IsDungeon()", block)
        self.assertIn('", no raid strategy"', block)
        self.assertIn('"outdoor"', block)
        self.assertNotIn('active.empty() ? "outdoor"', block)


if __name__ == "__main__":
    unittest.main()
