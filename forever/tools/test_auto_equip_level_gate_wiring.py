"""Wiring pins for the pool-bot level check on organic gear (TBF #209 / pool bot gear).

EquipUpgradesAction is the only path that wears what a bot loots, wins, buys or is traded. On Forever the item rows
often store RequiredLevel 0 even at high item levels, so the core's "can wear it" answer is not enough; the audit
derives the missing requirement (runtime/GearEffectiveLevelPolicy.h) and applies it to pool bots only. The pool
identity must come from the both-core helper header, never from the legacy facade on Forever.

Run:
  python3 -m unittest forever/tools/test_auto_equip_level_gate_wiring.py
  python3 forever/tools/test_auto_equip_level_gate_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ACTION = (ROOT / "ai/playerbot/strategy/actions/EquipAction.cpp").read_text()
POLICY = (ROOT / "runtime/GearEffectiveLevelPolicy.h").read_text()
USAGE = (ROOT / "ai/playerbot/strategy/values/ItemUsageValue.cpp").read_text()
SLICE = (ROOT / "forever/SLICE-W4.txt").read_text()
SELECTOR = (ROOT / "ai/playerbot/TbCompatPoolGear.h").read_text()
TURTLE = (ROOT / "ai/cmangos-compat-poolgear.h").read_text()
FOREVER = (ROOT / "forever/src/compat/TbCompat_PoolGear.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
CONF = (ROOT / "forever/conf/aiplayerbot.conf.dist").read_text()
KEYS_DOC = (ROOT / "docs/server/config-keys.md").read_text()
SETTINGS_DOC = (ROOT / "docs/server/settings/world-population.md").read_text()

GATE_CONDITION = "TortoiseBots::GearEffectiveRequiredLevel(levelProto->RequiredLevel, levelProto->ItemLevel) > bot->GetLevel()"


def organic_gate(action_src):
    """The loop body of EquipUpgradesAction, from its Execute() to the slot resolution."""
    start = action_src.index("bool EquipUpgradesAction::Execute(Event& event)")
    end = action_src.index("uint8 slot = ItemUsageValue::GetPreferredEquipSlot", start)
    return action_src[start:end]


def check_gate(action_src):
    for needle in (
        '#include "playerbot/TbCompatPoolGear.h"',
        '#include "../../../../runtime/GearEffectiveLevelPolicy.h"',
    ):
        if needle not in action_src:
            raise AssertionError("EquipAction.cpp missing: %s" % needle)
    gate = organic_gate(action_src)
    for needle in (
        "TortoiseBots::PoolGearTopUpEligible(",
        "sPlayerbotAIConfig.autoEquipUpgradeRespectsRequiredLevel",
        "IsPoolBotCompat(bot)",
        "PoolGearTopUpIsHiredCompat(bot)",
        "PoolGearTopUpIsClaimedCompat(bot)",
        "ai->HasRealPlayerMaster()",
        "ItemPrototype const* levelProto = GetItemProtoCompat(item);",
        "levelProto->Class == ITEM_CLASS_WEAPON || levelProto->Class == ITEM_CLASS_ARMOR",
        GATE_CONDITION,
        "continue;",
    ):
        if needle not in gate:
            raise AssertionError("EquipUpgradesAction organic loop missing: %s" % needle)


class AutoEquipLevelGateWiring(unittest.TestCase):
    def test_gate_sits_in_the_pool_organic_path(self):
        check_gate(ACTION)

    def test_gate_is_off_for_hires_owned_and_player_orders(self):
        gate = organic_gate(ACTION)
        start = gate.index("TortoiseBots::PoolGearTopUpEligible(")
        block = gate[start:gate.index("for (auto& item : items)")]
        # The pool gear top-up's own eligibility rule: a live player master, a hire
        # and a guild claim all keep the player-chosen gear.
        for argument in (
            "IsPoolBotCompat(bot)",
            "PoolGearTopUpIsHiredCompat(bot)",
            "PoolGearTopUpIsClaimedCompat(bot)",
            "ai->HasRealPlayerMaster()",
        ):
            self.assertIn(argument, block)
        self.assertNotIn("sRandomBotFacade", block)
        explicit_equip = ACTION[:ACTION.index("bool EquipUpgradesAction::Execute")]
        self.assertNotIn("GearEffectiveRequiredLevel", explicit_equip)

    def test_pool_identity_is_the_both_core_helper(self):
        self.assertIn("IsPoolBotCompat(Player* bot)", SELECTOR)
        self.assertIn("return PoolGearTopUpIsPoolBotCompat(bot);", SELECTOR)
        # Turtle: the legacy random-bot facade. Forever: the host's scheduler flag.
        self.assertIn("sRandomBotFacade.IsRandomBot(bot)", TURTLE)
        self.assertIn("tbs_poolgear::IsPoolBot(bot)", FOREVER)

    def test_policy_is_the_shared_clamped_rule(self):
        self.assertIn("GearEffectiveRequiredLevel(std::uint32_t requiredLevel, std::uint32_t itemLevel)", POLICY)
        self.assertIn("requiredLevel > 1 || (requiredLevel == 1 && itemLevel < 15)", POLICY)
        self.assertIn("itemLevel > 5 ? itemLevel - 5 : 1", POLICY)
        self.assertIn("kGearMaxRequiredLevel", POLICY)
        self.assertIn("kGearMaxRequiredLevel = 60", POLICY)

    def test_equip_usage_carries_the_pool_level_gate(self):
        # Quest-reward choice, loot roll and vendor buy all read this usage.
        self.assertIn("ai/playerbot/strategy/values/ItemUsageValue.cpp", SLICE)
        self.assertIn('#include "playerbot/TbCompatPoolGear.h"', USAGE)
        self.assertIn('#include "runtime/GearEffectiveLevelPolicy.h"', USAGE)
        self.assertIn('#include "runtime/PoolGearTopUpPolicy.h"', USAGE)
        start = USAGE.index("ItemUsage ItemUsageValue::QueryItemUsageForEquip(")
        gate = USAGE[start:USAGE.index("uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);", start)]
        for needle in (
            "TortoiseBots::PoolGearTopUpEligible(",
            "sPlayerbotAIConfig.autoEquipUpgradeRespectsRequiredLevel",
            "IsPoolBotCompat(bot)",
            "PoolGearTopUpIsHiredCompat(bot)",
            "PoolGearTopUpIsClaimedCompat(bot)",
            "ai->HasRealPlayerMaster()",
            "GearEffectiveRequiredLevel(itemProto->RequiredLevel, itemProto->ItemLevel) > bot->GetLevel()",
        ):
            self.assertIn(needle, gate)

    def test_knob_is_read_defaulted_shipped_and_documented(self):
        self.assertIn(
            'autoEquipUpgradeRespectsRequiredLevel = config.GetBoolDefault("AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel", true);',
            CONFIG_CPP)
        self.assertIn("AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel = 1", TEMPLATE)
        self.assertIn("AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel = 1", CONF)
        self.assertIn(
            "| `AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel` | `1` | bool | yes | `ai/playerbot/strategy/actions/EquipAction.cpp`, `ai/playerbot/strategy/values/ItemUsageValue.cpp` |",
            KEYS_DOC)
        self.assertIn("### `AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel`", SETTINGS_DOC)

    def test_mutation_fails(self):
        with self.assertRaises(AssertionError):
            check_gate(ACTION.replace(GATE_CONDITION, "false"))
        with self.assertRaises(AssertionError):
            check_gate(ACTION.replace("IsPoolBotCompat(bot)", "true"))


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_gate(ACTION.replace(GATE_CONDITION, "false"))
        sys.exit(0)
    unittest.main()
