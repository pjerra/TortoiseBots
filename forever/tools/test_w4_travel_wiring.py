#!/usr/bin/env python3
"""Wiring pins for the travel chooser and walker (TBF #146, round W4-A3).

The travel manager and its values were compiled by W4-A2; this round compiles the actions and the strategy that
pick a destination and walk to it, registers their keys through forever/tools/slice_gen_context.py, adds `travel`
to a free bot's kit and ships the config keys the files read. The pins:

  * the five sources stay on the slice list the Forever build reads;
  * every travel key stays in the generator and the generated SliceAiObjectContext.cpp registers it with the full
    build's own creator; slice_registry_check.py (a) is empty and none of the keys is left in (b);
  * the link stand-ins the travel files need answer "nothing exists" (no order, no bank, no trainer, no pinned
    bot), and the maintenance stand-ins that MaintenanceValues.cpp supersedes are gone;
  * a free bot's kit carries `travel` (free kit policy and its test) and the eight config keys ship.

Run: python3 -m unittest forever/tools/test_w4_travel_wiring.py
"""
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402
import slice_gen_context as gen  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"
STUBS = "forever/src/slice/SliceLinkStubs.cpp"
KIT = "ai/playerbot/policy/world/FreeKitPolicy.h"
KIT_TEST = "forever/tools/test_free_kit_policy.cpp"
CONF = "forever/conf/aiplayerbot.conf.dist"
WL4_F = "forever/src/compat/TbCompat_WL4.h"
WL4_T = "ai/cmangos-compat-wl4.h"

SOURCES = (
    "ai/playerbot/strategy/generic/TravelStrategy.cpp",
    "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp",
    "ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp",
    "ai/playerbot/strategy/actions/TravelAction.cpp",
    "ai/playerbot/strategy/values/MaintenanceValues.cpp",
)

# (kind, key): every key the travel rows need, in the full build's kind.
KEYS = [
    ("strategy", "travel"),
    ("strategy", "travel once"),
    ("strategy", "explore"),
    ("action", "travel"),
    ("action", "choose travel target"),
    ("action", "choose group travel target"),
    ("action", "refresh travel target"),
    ("action", "request travel target"),
    ("action", "request named travel target"),
    ("action", "request quest travel target"),
    ("action", "reset travel target"),
    ("action", "move to travel target"),
    ("value", "travel target active"),
    ("value", "travel target ready"),
    ("value", "no active travel destinations"),
    ("value", "need travel purpose"),
    ("value", "should travel named"),
    ("value", "should leave outgrown zone"),
    ("value", "vendor trip needed"),
]

# The definitions that now come from MaintenanceValues.cpp, which is on the slice list.
SUPERSEDED = (
    "bool ShouldSellValue::GroupMemberLeavingForVendor",
    "bool ShouldSellValue::CantAffordNextSpell",
    "void ParkVendorErrand(",
)

# Each stand-in returns the real feature's "nothing exists" answer.
STAND_INS = (
    "void SetState(uint32_t, OrderState, OrderReason) { NotePool(); }",
    "void SetFarmZone(uint32_t, uint32_t, uint8_t, uint32_t, uint32_t) { NotePool(); }",
    "void SetSupply(uint32_t, SupplyStep, uint32_t, uint32_t, SupplyStep, OrderReason, uint32_t) { NotePool(); }",
    "void CommitAuctionZone(uint32_t, uint32_t) { NotePool(); }",
    "void NoteSearchFailed(uint32_t, uint32_t) { NotePool(); }",
    "void NoteSearchLanded(uint32_t) { NotePool(); }",
    "void NoteFarmZoneTargetFailed(uint32_t, uint32_t, uint32_t, float, float, float) { NotePool(); }",
    "uint32_t GuildBankTabCount(uint32_t) { NotePool(); return 0; }",
    "void NoteBankEmpty(uint32_t, uint32_t) { NotePool(); }",
    "void NoteMailEmpty(uint32_t, uint32_t) { NotePool(); }",
    "uint32_t NoteSupplySearchFailed(uint32_t, SupplyStep, uint32_t) { NotePool(); return 0; }",
    "uint32_t NoteArrivedAuctionVisit(uint32_t, bool) { NotePool(); return 0; }",
    "bool AuctionReentryAllowed(uint32_t, uint32_t) { NotePool(); return true; }",
    "uint32 OrderMailboxItems(Player*, uint32) { NotePool(); return 0; }",
    "std::vector<GuildOrderZoneSlice> GuildOrderZonesFor(int32_t) { NotePool(); return {}; }",
    "bool GuildOrderPreyHostileToBot(Player*, int32_t) { NotePool(); return false; }",
    "bool IsFishingSpotGuarded(Player*, WorldPosition const&, float) { NoteMisc(); return true; }",
    "Unit* GuildOrderAuctioneerNear(PlayerbotAI*, Player*) { NotePool(); return nullptr; }",
    "bool BuyPetitionAction::canBuyPetition(Player*) { NoteMisc(); return false; }",
    "bool IsOrderFishing(PlayerbotAI*) { NoteMisc(); return false; }",
    "bool DowntimeService::IsDowntimeActive(PlayerbotAI*) { NoteMisc(); return false; }",
    "std::string RandomBotFacade::GetData(uint32, std::string) { NotePool(); return std::string(); }",
    "bool RandomBotFacade::IsPinnedBot(uint32) { NotePool(); return false; }",
)

# The config keys the newly compiled files consume (the generator's literal fallbacks set the shipped values).
CONFIG_KEYS = (
    "AiPlayerbot.AsyncTravelPartitions",
    "AiPlayerbot.StuckTravelTeleport",
    "AiPlayerbot.TravelPickBudgetUs",
    "AiPlayerbot.VendorBatchMinCount",
    "AiPlayerbot.VendorBatchMinBagSpace",
    "AiPlayerbot.LowLevelVendorBatchMinCount",
    "AiPlayerbot.LowLevelVendorBatchMinBagSpace",
    "AiPlayerbot.GuildOrderAuctionVisits",
)


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


class SliceMembership(unittest.TestCase):
    def test_the_five_sources_are_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        for path in SOURCES:
            self.assertIn(path, paths)


class Registered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()

    def test_generator_lists_every_key(self):
        for kind, key in KEYS:
            self.assertIn(key, gen.KEYS[kind], "%s '%s' is not in slice_gen_context.py" % (kind, key))

    def test_generated_context_registers_every_key_with_the_full_builds_creator(self):
        registered = chk.slice_registry()
        for kind, key in KEYS:
            self.assertIn(key, registered[kind], "%s '%s' is not in SliceAiObjectContext.cpp" % (kind, key))
            self.assertEqual(registered[kind][key]["expr"], self.full[kind][key]["expr"], key)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_gen_context.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_registry_check_no_longer_lists_them(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_registry_check.py"), "--list"],
                              capture_output=True, text=True, timeout=300)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr)
        self.assertIn("(a) slice keys that are not in the full build, or map elsewhere: 0", proc.stdout)
        for kind, key in KEYS:
            self.assertIsNone(re.search(r"(?m)^    %s  " % re.escape(key), proc.stdout),
                              "%s '%s' is still on the missing list" % (kind, key))


class StandIns(unittest.TestCase):
    def test_the_superseded_maintenance_stand_ins_are_gone(self):
        stubs = read(STUBS)
        for gone in SUPERSEDED:
            self.assertNotIn(gone, stubs)

    def test_the_new_stand_ins_answer_nothing_exists(self):
        stubs = read(STUBS)
        for line in STAND_INS:
            self.assertIn(line, stubs)

    def test_the_source_plan_stand_in_refuses(self):
        stubs = read(STUBS)
        self.assertIn("TortoiseBots::GuildOrders::SourcePlan PlanGuildOrderSources(", stubs)
        self.assertIn("plan.verdict = TortoiseBots::GuildOrders::OrderReason::NoSource;", stubs)


class Pair(unittest.TestCase):
    def test_the_new_helpers_exist_on_both_cores(self):
        core = read(WL4_F)
        turtle = read(WL4_T)
        for name in ("GuildBankerGossipWL4", "SpellFocusIdWL4", "QuestStatusRewardedWL4",
                     "QuestStatusIncompleteWL4", "QuestStatusCompleteWL4", "QuestObjectiveSlotCountWL4",
                     "QuestReqCreatureOrGOIdWL4", "QuestReqCreatureOrGOCountWL4", "QuestObjectiveCountersWL4",
                     "TaxiNodeLookupWL4", "TaxiNodeNameWL4", "MoveSplineOriginWL4",
                     "MoveSplineTimeElapsedWL4", "LiftSetTransportWL4", "LiftTransportGuidWL4",
                     "LiftClearTransportWL4"):
            self.assertIn(name, core, "ForeverCore side lacks " + name)
            self.assertIn(name, turtle, "Turtle side lacks " + name)


class FreeKitAndConfig(unittest.TestCase):
    def test_a_free_bot_has_the_travel_strategy(self):
        self.assertIn('"grind", "travel", "food"', read(KIT))
        self.assertIn('"grind", "travel", "food"', read(KIT_TEST))
        self.assertNotIn('"travel", "tfish"', read(KIT_TEST))

    def test_the_new_config_keys_ship(self):
        conf = read(CONF)
        for key in CONFIG_KEYS:
            self.assertRegex(conf, r"(?m)^%s = " % re.escape(key))


if __name__ == "__main__":
    unittest.main()
