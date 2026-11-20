"""Wiring pins for .tb gear <bot|party [leader]> upgrade.

Run:
  python3 -m unittest forever/tools/test_eq32_wiring.py
  python3 forever/tools/test_eq32_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
H3_HDR = (ROOT / "forever/src/host/TbH3.h").read_text()
H3_OFF = (ROOT / "forever/src/host/TbH3Off.cpp").read_text()
STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
W6_HDR = (ROOT / "forever/src/compat/TbCompat_W6.h").read_text()
DOCS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()


def check_wiring(scripts_src, h3_hdr_src, h3_off_src, state_src, w6_hdr_src, docs_src):
    if not re.search(r'\{\s*"gear"\s*,\s*TortoiseBots::h3::HandleGear\b', scripts_src):
        raise AssertionError('TbScripts.cpp missing "gear" row -> TortoiseBots::h3::HandleGear')
    if "bool HandleGear(" not in h3_hdr_src:
        raise AssertionError("TbH3.h missing HandleGear declaration")
    if not re.search(r'bool HandleGear\([^)]*\)\s*\{\s*return NotBuilt\(handler,\s*args,\s*"gear"\);\s*\}', h3_off_src):
        raise AssertionError('TbH3Off.cpp missing HandleGear -> NotBuilt(..., "gear")')

    if "ScopedExplicitEquipOrder" not in w6_hdr_src:
        raise AssertionError("TbCompat_W6.h missing ScopedExplicitEquipOrder RAII guard")
    if "ExplicitEquipOrderFlag()" not in w6_hdr_src:
        raise AssertionError("TbCompat_W6.h MayAutoEquipUpgradesCompat must check ExplicitEquipOrderFlag()")
    if "~ScopedExplicitEquipOrder()" not in w6_hdr_src or "ExplicitEquipOrderFlag() = prev" not in w6_hdr_src:
        raise AssertionError("ScopedExplicitEquipOrder must restore previous flag in destructor")

    gear_fn = state_src[state_src.index("bool HandleGear("):]
    if "tbs_w6::ScopedExplicitEquipOrder" not in gear_fn:
        raise AssertionError("HandleGear must scope the explicit-order allowance with ScopedExplicitEquipOrder")
    if '"equip upgrades"' not in gear_fn:
        raise AssertionError('HandleGear must invoke "equip upgrades"')
    if "EQUIPMENT_SLOT_START" not in gear_fn or "EQUIPMENT_SLOT_END" not in gear_fn:
        raise AssertionError("HandleGear must diff EQUIPMENT_SLOT_START..EQUIPMENT_SLOT_END")
    if '"in-combat"' not in gear_fn or '"no-engine"' not in gear_fn:
        raise AssertionError("HandleGear must refuse in-combat and no-engine")
    if ".tb gear " not in docs_src:
        raise AssertionError("TB-COMMANDS.md missing .tb gear documentation")


class Eq32Wiring(unittest.TestCase):
    def test_wiring_intact(self):
        check_wiring(SCRIPTS, H3_HDR, H3_OFF, STATE, W6_HDR, DOCS)

    def test_mutations_fail(self):
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS.replace('"gear"', '"nope"'), H3_HDR, H3_OFF, STATE, W6_HDR, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, H3_HDR, H3_OFF.replace('"gear"', '"nope"'), STATE, W6_HDR, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, H3_HDR, H3_OFF, STATE.replace("ScopedExplicitEquipOrder", "NoGuard"), W6_HDR, DOCS)
        with self.assertRaises(AssertionError):
            check_wiring(SCRIPTS, H3_HDR, H3_OFF, STATE, W6_HDR.replace("ExplicitEquipOrderFlag()", "false"), DOCS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_wiring(SCRIPTS, H3_HDR, H3_OFF, STATE.replace("ScopedExplicitEquipOrder", "NoGuard"), W6_HDR, DOCS)
        sys.exit(0)
    unittest.main()
