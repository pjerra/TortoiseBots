"""TARGET_FLAG_LOCKED must be the core's lock-opening flag (TARGET_FLAG_UNK1, 0x4000), not the donor's 0x100."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class TargetFlagLockedTest(unittest.TestCase):
    def test_shim_maps_locked_to_the_core_lock_flag(self):
        shim = (ROOT / "ai/cmangos-compat-shim.h").read_text()
        match = re.search(r"#define\s+TARGET_FLAG_LOCKED\s+(\S+)", shim)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "TARGET_FLAG_UNK1")

    def test_use_item_action_asserts_the_value(self):
        text = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.cpp").read_text()
        self.assertIn("static_assert(TARGET_FLAG_LOCKED == 0x4000", text)


if __name__ == "__main__":
    unittest.main()
