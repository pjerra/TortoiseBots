#!/usr/bin/env python3
"""Wiring pins for the hire provisioner (forever/src/host/hire, package P2).

The pure decisions are forever/tools/test_hire_provision_pure.cpp; this file pins
the seams that make them reachable: the slice-only build, the world tick, the
`.bot hire` verb, the add-on verb/caps and the roster exclusion.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"


def read(path):
    return Path(path).read_text()


class HireProvisionWiring(unittest.TestCase):
    def setUp(self):
        self.cmake = read(ROOT / "CMakeLists.txt")
        self.scripts = read(SRC / "TbScripts.cpp")
        self.cmd = read(HOST / "player" / "TbPlayerCmd.cpp")
        self.addon_verbs = read(HOST / "TbAddonVerbs.cpp")
        self.addon_info = read(HOST / "TbAddonInfo.cpp")
        self.roster = read(HOST / "TbAddonRoster.cpp")
        self.provision = read(HOST / "hire" / "TbHireProvision.cpp")
        self.pure = read(HOST / "hire" / "TbHireProvisionPure.h")

    def test_built_only_with_the_engine(self):
        off = self.cmake.split("if(NOT TORTOISEBOTS_SLICE1)", 1)[1].split("else()", 1)[0]
        self.assertIn('REGEX "/forever/src/host/hire/[^/]*\\\\.cpp$"', off)
        self.assertNotIn("host/hire", self.pure)   # the pure header pulls no host code

    def test_fee_formula_is_the_shared_one(self):
        self.assertIn("runtime/HireCost.cpp", self.cmake)
        self.assertIn("HireCost::ForNextHire", self.pure)
        self.assertIn("HireCostAtTier", self.pure)

    def test_world_tick_runs_the_provisioner(self):
        self.assertIn('#include "host/hire/TbHire.h"', self.scripts)
        self.assertIn('#include "host/hire/TbHireProvision.h"', self.scripts)
        self.assertIn("TortoiseBots::hire::Update(diff);", self.scripts)
        # The caps need the engine config, so they are advertised after LoadEngineConfig.
        self.assertIn("TortoiseBots::hire::AdvertiseAddonCaps();", self.scripts)
        self.assertLess(self.scripts.index("LoadEngineConfig();"), self.scripts.index("TortoiseBots::hire::AdvertiseAddonCaps();"))

    def test_bot_hire_and_dismiss(self):
        self.assertIn('if (verb == "hire")', self.cmd)
        self.assertIn("hire::Hire(player, selection, nullptr)", self.cmd)
        self.assertIn("hire::ParseHireCommand(", self.cmd)
        self.assertIn("HireLifecycle::Instance().DismissNow(", self.cmd)

    def test_addon_verb_and_caps(self):
        self.assertIn('addon::RegisterVerb("hire", &HandleHire);', self.addon_verbs)
        self.assertIn('RunVerb("hire", requester, args)', self.addon_verbs)
        self.assertIn('addon::AdvertiseCap("hiring")', self.provision)
        self.assertIn('addon::AdvertiseCap("hire-anywhere")', self.provision)
        self.assertIn("for (std::string const& line : hire::FeatureLines())", self.addon_info)
        for feature in ["hiring", "hire-max", "hire-pvp", "hire-anywhere", "hire-tier"]:
            self.assertIn(f'FeatureLine("{feature}"', self.provision)

    def test_hires_are_not_roster_rows(self):
        body = self.roster.split("void HandleRoster(Player* requester", 1)[1]
        self.assertIn("HireLifecycle::Instance().IsHired(bot->GetGUID())", body)
        # BOTSTATE still covers every visible bot: the hire stays a bot target.
        self.assertIn("std::vector<Player*> const bots = VisibleBots(requester, caller, scope);", body)

    def test_contract_functions_implemented(self):
        for function in ["bool Enabled()", "bool HireAnywhere()", "uint32_t QuoteFee(",
                         "Outcome Hire(Player* hirer", "void Update(uint32_t diff)"]:
            self.assertIn(function, self.provision)
        self.assertIn('#include "host/hire/TbHireAccounts.h"', self.provision)
        self.assertIn("HireLifecycle::Instance().Claim(", self.provision)
        self.assertIn("HireLifecycle::Instance().SetGearTier(", self.provision)
        self.assertIn("HireLifecycle::Instance().MarkGrouped(", self.provision)
        self.assertIn("HighestUnlockedTier(hirer)", self.provision)
        self.assertIn("EquipTierSet(bot, pending.gearTier, pending.specIndex)", self.provision)


if __name__ == "__main__":
    unittest.main()
