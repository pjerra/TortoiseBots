"""Wiring pins for W6b (gear generation: classic item filter, host TbGearGen, .tb gear set|audit, target guard).

Run:
  python3 -m unittest forever/tools/test_w6b_wiring.py
  python3 forever/tools/test_w6b_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY = (ROOT / "ai/playerbot/policy/item/GearSetPolicy.h").read_text()
GEAR_GEN_H = (ROOT / "forever/src/host/gear/TbGearGen.h").read_text()
GEAR_GEN_CPP = (ROOT / "forever/src/host/gear/TbGearGen.cpp").read_text()
PARTY_STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
SLICE_W6B = (ROOT / "forever/SLICE-W6B.txt").read_text()


def check_band_filter(policy_src):
    fn_start = policy_src.index("inline bool IsClassicItemCandidate(")
    fn_end = policy_src.index("\n}\n", fn_start)
    fn = policy_src[fn_start:fn_end]
    if "requiredExpansion != 0" not in fn:
        raise AssertionError("IsClassicItemCandidate missing requiredExpansion != 0 filter")
    if "!IsItemInGearBand(itemId)" not in fn:
        raise AssertionError("IsClassicItemCandidate missing the classic + Forever id band filter")
    if "randomBonusListTemplateId != 0" not in fn:
        raise AssertionError("IsClassicItemCandidate missing randomBonusListTemplateId != 0 filter")
    if "IsTrapItem(itemId)" not in fn:
        raise AssertionError("IsClassicItemCandidate missing IsTrapItem(itemId) filter")


class W6bWiring(unittest.TestCase):
    def test_classic_filter_and_traps(self):
        check_band_filter(POLICY)
        self.assertIn("kClassicItemMaxId = 25000", POLICY)
        self.assertIn("kForeverItemBandMinId = 200000", POLICY)
        self.assertIn("kForeverItemBandMaxId = 292000", POLICY)
        band_start = POLICY.index("inline bool IsItemInGearBand(")
        band = POLICY[band_start:POLICY.index("\n}\n", band_start)]
        self.assertIn("itemId < kClassicItemMaxId", band)
        self.assertIn("itemId >= kForeverItemBandMinId && itemId < kForeverItemBandMaxId", band)
        for trap_id in ("16922", "17802", "18803"):
            self.assertIn(trap_id, POLICY)

    def test_expansion_mutation_fails(self):
        mutated = POLICY.replace("if (requiredExpansion != 0)", "if (false)")
        with self.assertRaises(AssertionError):
            check_band_filter(mutated)
        no_band = POLICY.replace("if (!IsItemInGearBand(itemId))", "if (false)")
        with self.assertRaises(AssertionError):
            check_band_filter(no_band)

    def test_host_gear_gen_uses_policy_and_core_equip(self):
        self.assertIn("ai::IsClassicItemCandidate", GEAR_GEN_CPP)
        self.assertIn("GetItemTemplateStore()", GEAR_GEN_CPP)
        self.assertIn("CanEquipNewItem(", GEAR_GEN_CPP)
        self.assertIn("EquipNewItem(", GEAR_GEN_CPP)
        self.assertIn("CanStoreNewItem(", GEAR_GEN_CPP)
        self.assertIn("StoreNewItem(", GEAR_GEN_CPP)
        self.assertIn("GetItemByGuid(", GEAR_GEN_CPP)
        self.assertIn("DestroyItem(", GEAR_GEN_CPP)
        self.assertNotIn("DestroyItemCount(", GEAR_GEN_CPP)
        self.assertIn("SwapItem(", GEAR_GEN_CPP)
        self.assertIn("sRandomItemMgr.CalculateStatWeight", GEAR_GEN_CPP)
        self.assertIn("TortoiseBots::gear::InitGearIndex()", SCRIPTS)

    def test_command_surface_and_target_guard(self):
        self.assertIn("ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, true)", PARTY_STATE)
        self.assertIn('verb == "set"', PARTY_STATE)
        self.assertIn('Lower(args.w[0]) == "audit"', PARTY_STATE)
        self.assertIn("SetGearForTarget", PARTY_STATE)
        self.assertIn("AuditGearIndex", PARTY_STATE)

    def test_slice_w6b_exists_and_nonempty(self):
        lines = [l.strip() for l in SLICE_W6B.splitlines() if l.strip() and not l.startswith("#")]
        self.assertGreater(len(lines), 250)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_band_filter(POLICY.replace("if (requiredExpansion != 0)", "if (false)"))
        sys.exit(0)
    unittest.main()
