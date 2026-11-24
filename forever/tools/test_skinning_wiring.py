#!/usr/bin/env python3
"""Wiring pins for corpse skinning (TBF #153, package P1).

Run:
  python3 -m unittest forever/tools/test_skinning_wiring.py
  python3 forever/tools/test_skinning_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestSkinningWiring(unittest.TestCase):
    def test_corpse_gate_is_the_core_gate(self):
        v2 = read("forever/src/compat/TbCompat_V2.h")
        if MUTATE:
            v2 = v2.replace("ai::CorpseReadyForSkinning", "false")
        self.assertIn("ai::CorpseReadyForSkinning", v2)
        self.assertIn("creature->HasUnitFlag(UNIT_FLAG_SKINNABLE)", v2)
        self.assertIn("creature->IsSkinnedBy(bot)", v2)
        self.assertIn("creature->GetLootForPlayer(bot)", v2)
        self.assertNotIn("if (creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))\n        tbcompat::Unported", v2)

    def test_creature_cast_path_is_open(self):
        i2 = read("forever/src/compat/TbCompat_I2.h")
        if MUTATE:
            i2 = i2.replace("if (object->ToCreature())\n            return false;", "")
        self.assertIn("if (object->ToCreature())\n            return false;", i2)
        self.assertIn("GAMEOBJECT_TYPE_CHEST", i2)

    def test_skin_window_is_announced(self):
        s9 = read("forever/src/compat/TbCompat_S9.h")
        if MUTATE:
            s9 = s9.replace("owner.IsCreature() && loot->loot_type == LOOT_SKINNING", "false")
        self.assertIn("owner.IsCreature() && loot->loot_type == LOOT_SKINNING", s9)
        self.assertIn("owner.IsGameObject()", s9)

    def test_stand_in_is_gone_and_the_host_answers(self):
        stubs = read("forever/src/slice/SliceLinkStubs.cpp")
        host = read("forever/src/host/TbHostGuildOrderSkin.cpp")
        if MUTATE:
            stubs = stubs.replace("// ItemUsageValue.cpp is in the slice",
                                  "bool GuildOrderSkinMobFor(Player*, uint32_t) { NotePool(); return false; }\n"
                                  "// ItemUsageValue.cpp is in the slice")
        self.assertNotIn("bool GuildOrderSkinMobFor(Player*, uint32_t)", stubs)
        self.assertIn("bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry)", host)
        self.assertIn("tbhost::guild_order_skin::SkinMobFor", host)

    def test_index_loads_with_the_order_store(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace("LoadSkinSources();", "")
        self.assertIn('#include "host/TbHostGuildOrderSkin.h"', orders)
        self.assertIn("tbhost::guild_order_skin::LoadSkinSources();", orders)

    def test_index_uses_the_shared_rules(self):
        skin = read("forever/src/host/TbHostGuildOrderSkin.cpp")
        if MUTATE:
            skin = skin.replace("CreatureRankExcludedFromSoloFarm", "NeverExcluded")
        self.assertIn("skinning_loot_template", skin)
        self.assertIn("creature_template_difficulty", skin)
        self.assertIn("CreatureRankExcludedFromSoloFarm", skin)
        self.assertIn("SkinningSkillForMobLevel", skin)
        self.assertIn("GetCreatureClassicLevel", skin)
        self.assertIn("OrderType::Farm", skin)
        self.assertIn("OrderState::Working", skin)

    def test_host_file_is_built_by_the_slice_glob(self):
        self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/src/host/TbHostGuildOrderSkin.cpp")))
        self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/src/host/TbHostGuildOrderSkin.h")))
        cmake = read("CMakeLists.txt")
        if MUTATE:
            cmake = cmake.replace("host/TbHost*.cpp", "host/NoHostFiles*.cpp")
        self.assertIn("host/TbHost*.cpp", cmake)

    def test_unported_row_drops_skinning(self):
        features = read("ai/playerbot/UnportedFeatures.h")
        if MUTATE:
            features = features.replace("loot: game objects, pickpocketing and disenchant loot", "loot: gathering and skinning")
        self.assertIn("loot: game objects, pickpocketing and disenchant loot", features)
        self.assertNotIn("skinning a corpse, pickpocketing", features)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
