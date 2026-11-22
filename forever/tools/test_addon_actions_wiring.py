#!/usr/bin/env python3
"""Wiring pins for the `action` addon verb (TbAddonActions.{h,cpp} and TbAddonActionsPure.h).

Run:
  python3 -m unittest forever/tools/test_addon_actions_wiring.py
  python3 forever/tools/test_addon_actions_wiring.py --mutate
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbAddonActionsPure.h").read_text()
H = (ROOT / "forever/src/host/TbAddonActions.h").read_text()
CPP = (ROOT / "forever/src/host/TbAddonActions.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
PLAYER_CMD = (ROOT / "forever/src/host/player/TbPlayerCmd.cpp").read_text()
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()


def check_registers_action(scripts_src):
    if "TortoiseBots::action::RegisterAddonVerbsActions();" not in scripts_src:
        raise AssertionError("TbScripts must call RegisterAddonVerbsActions after addon::AddScripts")
    channel = scripts_src.index("TortoiseBots::addon::AddScripts();")
    actions = scripts_src.index("TortoiseBots::action::RegisterAddonVerbsActions();")
    if not channel < actions:
        raise AssertionError("RegisterAddonVerbsActions must run after addon::AddScripts")


def check_no_pjerra_edits(player_cmd_src, orders_src):
    for token in ("HandleActionVerb", "RegisterAddonVerbsActions", "TbAddonActions"):
        if token in player_cmd_src:
            raise AssertionError(f"TbPlayerCmd.cpp must not mention {token}")
        if token in orders_src:
            raise AssertionError(f"TbOrders.cpp must not mention {token}")


class AddonActionsWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        self.assertIn('#include "host/TbAddonActions.h"', SCRIPTS)
        check_registers_action(SCRIPTS)

    def test_header_and_registration(self):
        self.assertIn("RegisterAddonVerbsActions", H)
        self.assertIn('addon::RegisterVerb("action"', CPP)
        self.assertIn("addon::Advertise(AdvertiseToken(intent))", CPP)
        self.assertIn("SupportedIntents()", CPP)

    def test_calls_existing_implementations(self):
        self.assertIn("h3::QueueOrder", CPP)
        self.assertIn("tactics::PickInterruptBot", CPP)
        self.assertIn("h8::ClearCcOrder", CPP)
        self.assertIn("FactsFor", CPP)
        self.assertIn("player::Decide", CPP)
        self.assertIn("player::Tier::Tactical", CPP)
        self.assertIn("host/player/TbPlayerAuthPure.h", CPP)
        # FactsFor must keep every FactsOf field (host/player/TbPlayerCmd.cpp): a dropped field silently widens permission.
        for field in ("commandedByCaller", "groupLeader", "leaderIsBot", "inRun", "IsCommandedBy",
                      "BotInRunningRun", "GetIdByGameAccount", "GetCharacterAccountIdByGuid", "MasterOf"):
            self.assertIn(field, CPP, f"facts glue lost {field}")


    def test_support_table_matches_classify(self):
        classify = PURE.split("inline ExecKind Classify")[1].split("inline std::string WhyUnsupported")[0]
        intents = re.search(r"intents = \{(.*?)\};", PURE, re.S).group(1)
        for token in re.findall(r'"([^"]+)"', intents):
            if token == "cc clear":
                continue
            self.assertIn(f'"{token}"', classify, f"supported intent {token} must classify to an executor")


    def test_no_edits_to_pjerras_files(self):
        check_no_pjerra_edits(PLAYER_CMD, ORDERS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated = SCRIPTS.replace("TortoiseBots::action::RegisterAddonVerbsActions();", "")
        try:
            check_registers_action(mutated)
        except AssertionError:
            print("MUTATION CAUGHT: scripts-registration")
        else:
            print("ERROR: scripts-registration mutation survived", file=sys.stderr)
            sys.exit(1)

        mutated_cpp = CPP.replace("h3::QueueOrder", "h3::MissingOrder")
        try:
            if "h3::QueueOrder" not in mutated_cpp:
                raise AssertionError("must queue through h3::QueueOrder")
            print("ERROR: queue-call mutation survived", file=sys.stderr)
            sys.exit(1)
        except AssertionError:
            print("MUTATION CAUGHT: queue-call")
    else:
        unittest.main(argv=[sys.argv[0]])
