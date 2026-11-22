#!/usr/bin/env python3
"""Wiring pins for the `behavior` and `service` addon verbs (c-behsvc).

Run:
  python3 -m unittest forever/tools/test_addon_behsvc_wiring.py
  python3 forever/tools/test_addon_behsvc_wiring.py --mutate
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbAddonBehaviorServicePure.h").read_text()
H = (ROOT / "forever/src/host/TbAddonBehaviorService.h").read_text()
CPP = (ROOT / "forever/src/host/TbAddonBehaviorService.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
SLICE_LIST = (ROOT / "forever/SLICE-W4.txt").read_text()
SLICE_CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()


def check_scripts_wiring(scripts_src):
    if "TortoiseBots::behsvc::RegisterAddonVerbsBehaviorService();" not in scripts_src:
        raise AssertionError("TbScripts must call RegisterAddonVerbsBehaviorService")
    channel = scripts_src.index("TortoiseBots::addon::AddScripts();")
    reg = scripts_src.index("TortoiseBots::behsvc::RegisterAddonVerbsBehaviorService();")
    if not channel < reg:
        raise AssertionError("RegisterAddonVerbsBehaviorService must run after addon::AddScripts")


class AddonBehsvcWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        self.assertIn('#include "host/TbAddonBehaviorService.h"', SCRIPTS)
        check_scripts_wiring(SCRIPTS)

    def test_verbs_and_caps_registered(self):
        self.assertIn("RegisterAddonVerbsBehaviorService", H)
        self.assertIn('addon::RegisterVerb("behavior"', CPP)
        self.assertIn('addon::RegisterVerb("service"', CPP)
        self.assertIn('addon::AdvertiseCap("behavior")', CPP)
        self.assertIn('addon::AdvertiseCap("service")', CPP)

    def test_owner_auth_and_store_calls(self):
        self.assertIn("player::Tier::Owner", CPP)
        self.assertIn("player::Decide", CPP)
        self.assertIn("sPlayerbotDbStore.Save(ai)", CPP)
        self.assertIn("ai->ChangeStrategy", CPP)
        self.assertIn("ai::DecidePortalCast", CPP)
        self.assertIn("ai->CastSpell(spellId, bot)", CPP)

    def test_slice_strategies_registered(self):
        for cpp in ("NoAoeStrategy.cpp", "ConserveManaStrategy.cpp", "MimicConsumableStrategy.cpp"):
            self.assertIn(cpp, SLICE_LIST)
        for strat in ('"no aoe"', '"auto cc"', '"conserve mana"', '"mimic consumables"', '"spread"', '"no blink instance"'):
            self.assertIn(strat, SLICE_CTX)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated = SCRIPTS.replace("TortoiseBots::behsvc::RegisterAddonVerbsBehaviorService();", "")
        try:
            check_scripts_wiring(mutated)
        except AssertionError:
            print("MUTATION CAUGHT: scripts-registration")
        else:
            print("ERROR: scripts-registration mutation survived", file=sys.stderr)
            sys.exit(1)
    else:
        unittest.main(argv=[sys.argv[0]])
