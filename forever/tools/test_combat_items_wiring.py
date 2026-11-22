#!/usr/bin/env python3
"""Wiring test for combat items (issue #124: potions, healthstones, bandages, trinkets, oils, stones, poisons).

Run:
  python3 -m unittest forever/tools/test_combat_items_wiring.py
  python3 forever/tools/test_combat_items_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestCombatItemsWiring(unittest.TestCase):
    def test_slice_list_includes_combat_item_sources(self):
        txt = read("forever/SLICE-W4.txt")
        for rel in (
            "ai/playerbot/strategy/generic/UsePotionsStrategy.cpp",
            "ai/playerbot/strategy/generic/ConsumableStrategy.cpp",
            "ai/playerbot/strategy/actions/UseConsumableAction.cpp",
            "ai/playerbot/strategy/actions/UseTrinketAction.cpp",
            "ai/playerbot/strategy/actions/ImbueAction.cpp",
            "ai/playerbot/strategy/actions/BuffAction.cpp",
            "ai/playerbot/strategy/values/PartyMemberToSoulstone.cpp",
        ):
            self.assertIn(rel, txt, f"missing {rel} in forever/SLICE-W4.txt")

    def test_context_registers_combat_items_keys(self):
        gen = read("forever/tools/slice_gen_context.py")
        self.assertIn("COMBAT_ITEMS_KEYS", gen)
        ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
        for key in (
            '"potions"',
            '"consumables"',
            '"healthstone"',
            '"healing potion"',
            '"mana potion"',
            '"dark rune"',
            '"whipper root tuber"',
            '"use bandage"',
            '"anti-venom"',
            '"use consumable"',
            '"use trinket"',
            '"apply stone"',
            '"apply oil"',
            '"buff"',
            '"has poison debuff"',
            '"party member to soulstone"',
        ):
            self.assertIn(key, ctx, f"missing {key} in SliceAiObjectContext.cpp")

    def test_factory_wires_potions_and_poisons(self):
        factory = read("forever/src/slice/SliceAiFactory.cpp")
        if MUTATE:
            factory = factory.replace('"potions"', '"no_potions"')
        self.assertIn('"potions"', factory)
        self.assertIn('"poisons"', factory)

    def test_compat_item_cast_seam_uses_core_cast(self):
        i2 = read("forever/src/compat/TbCompat_I2.h")
        for sym in ("CastItemSpellCompat", "CastItemOnTargetCompat", "QueueUseItemPacketCompat", "ItemProtoHasDisplayIdCompat"):
            self.assertIn(sym, i2)
        self.assertIn("CastItemUseSpell", i2)
        self.assertIn("return false;", i2.split("ItemUseUnportedCompat")[1].split("}")[0])

    def test_random_potion_stub_present(self):
        stubs = read("forever/src/slice/SliceLinkStubs.cpp")
        self.assertIn("RandomItemMgr::GetRandomPotion", stubs)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
