"""Wiring pins for the generated-gear source index (forever/src/host/gear/TbGearSourceTier.cpp).

The index is built once at startup and gates every generated item:
  * reference_loot_template is walked recursively (cycle-safe, depth-capped), and only a non-reference row is an item id;
  * a vendor is a source independent of the creature's loot difficulty row, and only an ordinary item row (no currency,
    extended cost or player condition) counts as a base vendor source;
  * InitGearIndex does not build the source index a second time; TbScripts.cpp owns the one startup call.

Run:
  python3 -m unittest forever/tools/test_gear_source_index_wiring.py
  python3 forever/tools/test_gear_source_index_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "forever/src/host/gear/TbGearSourceTier.cpp").read_text()
GEAR_GEN = (ROOT / "forever/src/host/gear/TbGearGen.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()


def init_gear_index(src):
    return src[src.index("void InitGearIndex()"):src.index("uint32 GearIndexItemCount()")]


def collector(src):
    return src[src.index("void CollectLootTemplateItems("):src.index("void MarkLootTemplate(")]


def build_index(src):
    return src[src.index("void BuildIndex()"):src.index("void InitGearSourceIndex()")]


def check(source, gear_gen, scripts):
    walk = collector(source)
    if "inline constexpr uint32 kLootReferenceMaxDepth = 8;" not in source:
        raise AssertionError("loot walk missing: kLootReferenceMaxDepth = 8")
    for needle in (
        "visited.insert(referenceId).second",
        "CollectLootTemplateItems(references, references, referenceId, depth + 1, visited, out)",
        "depth > kLootReferenceMaxDepth",
    ):
        if needle not in walk:
            raise AssertionError("loot walk missing: %s" % needle)
    # A reference row records no item id of its own.
    if "out.push_back(row.item.itemid);" not in walk:
        raise AssertionError("loot walk does not record plain item rows")
    if "record(referenceRow.item.itemid)" in source:
        raise AssertionError("reference ids must not be recorded as item ids")

    build = build_index(source)
    vendor = build[build.index("if (VendorItemData const* vendor"):build.index("CreatureDifficulty const* difficulty")]
    for needle in (
        "vendorItem->Type == ITEM_VENDOR_TYPE_ITEM",
        "vendorItem->ExtendedCost == 0",
        "vendorItem->PlayerConditionId == 0",
        "NoteSource(state.sourceTier, itemId, GEAR_SOURCE_TIER_BASE)",
        "state.vendored.insert(itemId)",
    ):
        if needle not in vendor:
            raise AssertionError("vendor source missing: %s" % needle)

    if "InitGearSourceIndex()" in init_gear_index(gear_gen):
        raise AssertionError("InitGearIndex must not build the source index itself")
    startup = scripts[scripts.index("TortoiseBots::gear::InitGearIndex();"):]
    if "TortoiseBots::gear::InitGearSourceIndex();" not in startup[:200]:
        raise AssertionError("TbScripts.cpp must call InitGearSourceIndex once after InitGearIndex")


class GearSourceIndexWiring(unittest.TestCase):
    def test_reference_loot_walk_and_vendor_gates(self):
        check(SOURCE, GEAR_GEN, SCRIPTS)

    def test_mutation_fails(self):
        with self.assertRaises(AssertionError):
            check(SOURCE.replace("kLootReferenceMaxDepth = 8", "kLootReferenceMaxDepth = 1"), GEAR_GEN, SCRIPTS)
        with self.assertRaises(AssertionError):
            check(SOURCE.replace("vendorItem->ExtendedCost == 0", "true"), GEAR_GEN, SCRIPTS)
        with self.assertRaises(AssertionError):
            check(SOURCE, GEAR_GEN.replace("    idx.initialized = true;",
                                           "    InitGearSourceIndex();\n    idx.initialized = true;"), SCRIPTS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check(SOURCE.replace("vendorItem->PlayerConditionId == 0", "true"), GEAR_GEN, SCRIPTS)
        sys.exit(0)
    unittest.main()
