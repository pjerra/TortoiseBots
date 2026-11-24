"""Wiring pins for the pool gear top-up on ForeverCore (TBF #209 / pool bot gear).

The legacy action (PoolGearTopUpAction.cpp) is in the slice build list, the slice registry has its key, and every
core-specific question goes through the both-core helper header: the pool identity, the once-per-level marker and the
gear pass. The Forever side must answer with the host's pool flag, the W6b gear index and the TB caps (only empty or
plainly outdated grey/white slots, never above the bot's level band, never above AiPlayerbot.RandomGearMaxLevel).

Run:
  python3 -m unittest forever/tools/test_pool_gear_topup_wiring.py
  python3 forever/tools/test_pool_gear_topup_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ACTION = (ROOT / "ai/playerbot/strategy/actions/PoolGearTopUpAction.cpp").read_text()
POLICY = (ROOT / "runtime/PoolGearTopUpPolicy.h").read_text()
TURTLE = (ROOT / "ai/cmangos-compat-poolgear.h").read_text()
FOREVER = (ROOT / "forever/src/compat/TbCompat_PoolGear.h").read_text()
SELECTOR = (ROOT / "ai/playerbot/TbCompatPoolGear.h").read_text()
HOST = (ROOT / "forever/src/host/TbHostPoolGear.cpp").read_text()
GEAR_GEN = (ROOT / "forever/src/host/gear/TbGearGen.cpp").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()
SLICE_CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
SLICE = (ROOT / "forever/SLICE-W4.txt").read_text()
CONF = (ROOT / "forever/conf/aiplayerbot.conf.dist").read_text()
KEYS_DOC = (ROOT / "docs/server/config-keys.md").read_text()


def check_caps(gear_src):
    start = gear_src.index("GearSetOutcome TopUpGearForTarget(")
    end = gear_src.index("\n}\n", start)
    fn = gear_src[start:end]
    for needle in (
        "TortoiseBots::GearQualityFloor(level, false)",   # TB quality floor of the incremental seed band
        "TortoiseBots::GearItemLevelCap(level,",          # owner's pool cap: level + above-level allowance...
        "sPlayerbotAIConfig.randomGearMaxItemLevelAboveLevel",
        "sPlayerbotAIConfig.randomGearMaxLevel",          # ...never above AiPlayerbot.RandomGearMaxLevel
        "c.quality < minQuality || c.itemLevel > maxItemLevel",
        "ai::FilterCandidatesForSlot(",
        "ai::TargetMaxQualityForLevel(level)",
        "target->CanEquipNewItem(",
        "target->EquipNewItem(",
        "target->DestroyItem(",
    ):
        if needle not in fn:
            raise AssertionError("TopUpGearForTarget missing: %s" % needle)
    if "StoreNewItem(" in fn:
        raise AssertionError("TopUpGearForTarget must not stash the replaced grey/white (TB destroys it)")


def check_host_mask(host_src):
    for needle in (
        "TortoiseBots::PoolGearTopUpSlot(",
        "EQUIPMENT_SLOT_BODY",
        "EQUIPMENT_SLOT_TABARD",
        "TortoiseBots::gear::TopUpGearForTarget(bot, level, mask)",
        "BotMgr::Instance().IsPoolBot(",
        "kPoolGearTopUpMinLevel",
    ):
        if needle not in host_src:
            raise AssertionError("host/TbHostPoolGear.cpp missing: %s" % needle)


class PoolGearTopUpWiring(unittest.TestCase):
    def test_action_is_in_the_slice_and_registered(self):
        listed = [l.strip() for l in SLICE.splitlines() if l.strip() and not l.startswith("#")]
        self.assertIn("ai/playerbot/strategy/actions/PoolGearTopUpAction.cpp", listed)
        self.assertIn("pool gear top up", GEN)
        self.assertIn('creators["pool gear top up"]', SLICE_CTX)
        self.assertIn('#include "playerbot/strategy/actions/PoolGearTopUpAction.h"', SLICE_CTX)

    def test_action_asks_the_compat_layer_not_the_facade(self):
        self.assertIn('#include "playerbot/TbCompatPoolGear.h"', ACTION)
        for helper in (
            "PoolGearTopUpIsPoolBotCompat(bot)",
            "PoolGearTopUpIsHiredCompat(bot)",
            "PoolGearTopUpIsClaimedCompat(bot)",
            "PoolGearTopUpLastLevelCompat(bot)",
            "PoolGearTopUpMarkLevelCompat(bot, level)",
            "PoolGearTopUpRunCompat(bot, level)",
        ):
            self.assertIn(helper, ACTION)
        self.assertNotIn("sRandomBotFacade", ACTION)
        self.assertNotIn("PlayerbotFactory", ACTION)
        # Mark first: one attempt per level even when no slot found a candidate.
        self.assertLess(ACTION.index("PoolGearTopUpMarkLevelCompat"), ACTION.index("PoolGearTopUpRunCompat"))
        self.assertIn("TortoiseBots::PoolGearTopUpEligible", ACTION)
        self.assertIn("TortoiseBots::PoolGearTopUpDue", ACTION)

    def test_selector_and_both_cores(self):
        self.assertIn('#if __has_include("TbCompat_PoolGear.h")', SELECTOR)
        self.assertIn('#include "cmangos-compat-poolgear.h"', SELECTOR)
        # Turtle keeps the action's original calls.
        self.assertIn("sRandomBotFacade.IsRandomBot(bot)", TURTLE)
        self.assertIn("factory.TopUpPoolGear()", TURTLE)
        self.assertIn("kPoolGearTopUpLevelValue", TURTLE)
        self.assertIn("HireLifecycle::Instance().IsHired", TURTLE)
        self.assertIn("ClaimLifecycle::Instance().IsClaimed", TURTLE)
        # Forever: the host's pool flag, never the legacy facade's random-bot answer.
        self.assertIn("tbs_poolgear::IsPoolBot(bot)", FOREVER)
        self.assertIn("tbs_poolgear::TopUp(bot, level)", FOREVER)
        self.assertIn("kPoolGearTopUpLevelValue", POLICY)

    def test_forever_top_up_keeps_tb_slots_and_caps(self):
        self.assertIn("BotMgr::Instance().IsPoolBot(", HOST)
        check_host_mask(HOST)
        check_caps(GEAR_GEN)
        # Green or better is never a candidate for replacement: the mask comes from the pure policy.
        self.assertIn("return itemLevel + kPoolGearOutdatedItemLevelGap <= botLevel;", POLICY)

    def test_caps_mutation_fails(self):
        mutated = GEAR_GEN.replace("c.quality < minQuality || c.itemLevel > maxItemLevel", "false")
        with self.assertRaises(AssertionError):
            check_caps(mutated)
        mutated_host = HOST.replace("TortoiseBots::PoolGearTopUpSlot(", "PoolGearTopUpSlotRemoved(")
        with self.assertRaises(AssertionError):
            check_host_mask(mutated_host)

    def test_knob_is_shipped_and_documented_as_wired(self):
        self.assertIn("AiPlayerbot.PoolGearTopUpOnLevel = 0", CONF)
        self.assertIn("| `AiPlayerbot.PoolGearTopUpOnLevel` | `0` | bool | yes | `ai/playerbot/strategy/actions/PoolGearTopUpAction.cpp` |", KEYS_DOC)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_caps(GEAR_GEN.replace("c.quality < minQuality || c.itemLevel > maxItemLevel", "false"))
        sys.exit(0)
    unittest.main()
