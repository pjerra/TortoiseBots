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
        # The cast's target kind, movement stop and action duration are the pure policy the C++ test pins.
        for sym in ("DecideItemCastTargetKind", "ShouldStopMovingForItemCast", "ItemCastDurationMs"):
            self.assertIn(sym, i2)
        # The family-wide refusal gate is gone: every family is decided per item in CastItemSpellCompat.
        self.assertNotIn("ItemUseUnportedCompat", i2)
        self.assertNotIn("ItemUseUnportedCompat", read("ai/cmangos-compat-s2i.h"))
        self.assertNotIn("ItemUseUnportedCompat", read("ai/playerbot/strategy/actions/UseItemAction.cpp"))

    def test_potion_family_casts_through_core(self):
        # Family 1: a potion/healthstone/mana consumable from the bags is an owned ON_USE effect; the helper casts it and
        # the PvP "do not spend it" branch starts the item's own cooldowns through SpellHistory.
        i2 = read("forever/src/compat/TbCompat_I2.h")
        self.assertIn("if (!itemUsed)", i2)
        self.assertIn("bot->CastItemUseSpell(itemUsed, spellData.SpellId, targets", i2)
        unported = read("forever/src/compat/TbCompat_Unported.h")
        cooldown = unported.split("AddSpellAndCategoryCooldownsCompat")[1].split("}")[0]
        self.assertIn("StartCooldown", cooldown)
        self.assertNotIn("Unported", cooldown)
        features = read("ai/playerbot/UnportedFeatures.h")
        self.assertNotIn("ItemUsePacket", features)
        self.assertIn("Player::CastItemUseSpell", features)
        self.assertNotIn("item use packet", read("forever/docs/UNPORTED.md"))

    def test_bandage_family_casts_through_core(self):
        # Family 2: "use bandage" is a targeted item use; the target (self target) and the item id reach UseItemInternal, whose
        # CastItemSpellCompat sets a unit target and calls Player::CastItemUseSpell.
        action = read("ai/playerbot/strategy/actions/UseItemAction.h")
        bandage = action.split("class UseBandageAction")[1].split("// Used when this action is executed as a reaction")[0]
        self.assertIn('"use bandage"', action)
        self.assertIn('"self target"', bandage)
        self.assertIn("GetItemId() override", bandage)
        self.assertIn("SetUnitTarget(unit)", read("forever/src/compat/TbCompat_I2.h"))
        self.assertIn('"use bandage"', read("forever/src/slice/SliceAiObjectContext.cpp"))

    def test_random_potion_stub_present(self):
        stubs = read("forever/src/slice/SliceLinkStubs.cpp")
        self.assertIn("RandomItemMgr::GetRandomPotion", stubs)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
