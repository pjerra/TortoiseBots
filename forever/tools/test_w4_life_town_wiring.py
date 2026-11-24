#!/usr/bin/env python3
"""Wiring pins for the town verbs (TBF #147, package C).

The vendor map and useful-stock values, the bag-pressure destroy action and the 50-yard nearby service bridge were in
the tree but not on the slice list. This round compiles them against the W2 town pair. The pins:

  * the three sources stay on the slice list the Forever build reads;
  * the vendor-template helpers exist on both cores, the Forever side answers "no template list" and the Turtle side
    reads the core's template rows;
  * the shared value file keeps no Turtle-only spelling (the split template id, the template lookup, the pointer-typed
    list) and reads the shared vendor map through the compat macro with the empty answer checked;
  * the nearby service bridge keys its fail parks by the one guid key both cores agree on.

Run: python3 -m unittest forever/tools/test_w4_life_town_wiring.py
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"
W2_F = "forever/src/compat/TbCompat_W2Town.h"
W2_T = "ai/cmangos-compat-w2town.h"
VENDOR_VALUES = "ai/playerbot/strategy/values/VendorValues.cpp"
DESTROY = "ai/playerbot/strategy/actions/DestroyItemAction.cpp"
SERVICE = "ai/playerbot/strategy/actions/ServiceNearbyNpcAction.cpp"

SOURCES = (
    VENDOR_VALUES,
    DESTROY,
    SERVICE,
)

HELPERS = (
    "VendorTemplateIdCompat",
    "VendorTemplateItemsCompat",
    "VendorItemRefsCompat",
)


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


class SliceMembership(unittest.TestCase):
    def test_the_three_sources_are_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        for path in SOURCES:
            self.assertIn(path, paths)


class Pair(unittest.TestCase):
    def test_the_vendor_helpers_exist_on_both_cores(self):
        core = read(W2_F)
        turtle = read(W2_T)
        for name in HELPERS:
            self.assertIn(name, core, "ForeverCore side lacks " + name)
            self.assertIn(name, turtle, "Turtle side lacks " + name)

    def test_forever_answers_no_vendor_template_list(self):
        core = read(W2_F)
        self.assertRegex(core, r"inline uint32 VendorTemplateIdCompat\(CreatureInfo const\*\)\s*\{\s*return 0;")
        self.assertRegex(core, r"inline VendorItemData const\* VendorTemplateItemsCompat\(uint32\)\s*\{\s*return nullptr;")

    def test_turtle_reads_the_core_template_rows(self):
        turtle = read(W2_T)
        self.assertIn("cInfo->vendor_id", turtle)
        self.assertIn("GetNpcVendorTemplateItemList", turtle)


class VendorValues(unittest.TestCase):
    def test_stock_is_read_through_the_compat_helpers(self):
        values = read(VENDOR_VALUES)
        self.assertIn('#include "playerbot/TbCompatW2Town.h"', values)
        self.assertIn("VendorItemRefsCompat(", values)
        self.assertIn("VendorTemplateIdCompat(", values)
        self.assertIn("VendorTemplateItemsCompat(", values)

    def test_no_turtle_only_vendor_spelling_is_left(self):
        values = read(VENDOR_VALUES)
        for gone in ("cInfo->vendor_id", "GetNpcVendorTemplateItemList"):
            self.assertNotIn(gone, values)
        self.assertNotRegex(values, r"\bVendorItemList\b")

    def test_the_shared_vendor_map_read_is_guarded(self):
        values = read(VENDOR_VALUES)
        self.assertIn('GAI_VALUE_COMPAT(VendorMap*, "vendor map")', values)
        self.assertNotIn('GAI_VALUE(VendorMap*', values)
        self.assertRegex(values, r'GAI_VALUE_COMPAT\(VendorMap\*, "vendor map"\);\s*if \(!vendorMap\)\s*return \{\};')


class NearbyService(unittest.TestCase):
    def test_the_fail_park_key_is_the_shared_guid_key(self):
        service = read(SERVICE)
        self.assertIn("uint64_t const npcGuid = GuidKeyCompat(target);", service)
        self.assertNotIn("target.GetRawValue()", service)

    def test_the_four_verbs_dispatch_through_the_existing_actions(self):
        service = read(SERVICE)
        for action in ('"talk to quest giver"', '"accept all quests"', '"sell"', '"trainer"'):
            self.assertIn(action, service)


if __name__ == "__main__":
    unittest.main()
