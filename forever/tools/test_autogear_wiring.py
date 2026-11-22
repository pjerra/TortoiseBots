#!/usr/bin/env python3
"""Wiring pins for `.bot autogear` and `.autogear` (TbAutogear.{h,cpp} and TbAutogearPure.h).

Run:
  python3 -m unittest forever/tools/test_autogear_wiring.py
  python3 forever/tools/test_autogear_wiring.py --mutate
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
AUTOGEAR_H = (ROOT / "forever/src/host/TbAutogear.h").read_text()
AUTOGEAR_CPP = (ROOT / "forever/src/host/TbAutogear.cpp").read_text()
AUTOGEAR_PURE = (ROOT / "forever/src/host/TbAutogearPure.h").read_text()
PLAYER_CMD = (ROOT / "forever/src/host/player/TbPlayerCmd.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()


def check_pure_claim(pure_src):
    fn_start = pure_src.index("inline bool ClaimsAutogearCommand(")
    fn_end = pure_src.index("\n}\n", fn_start)
    fn = pure_src[fn_start:fn_end]
    if "word.size() > kCmd.size()" not in fn:
        raise AssertionError("ClaimsAutogearCommand missing boundary check")


def check_ownership_via_owner_scope(player_cmd_src, autogear_cpp_src):
    if "HandleBotAutogear(player, caller.gm, scope," not in player_cmd_src:
        raise AssertionError("TbPlayerCmd Dispatch must pass OwnerScope scope to HandleBotAutogear")
    if "return scope.Owns(gameAccount);" not in player_cmd_src:
        raise AssertionError("TbPlayerCmd OwnsGameAccount must delegate to scope.Owns")
    if "player::OwnsGameAccount(scope, entry->AccountId)" not in autogear_cpp_src:
        raise AssertionError("TbAutogear must check ownership via OwnerScope::Owns")
    if "GetIdByGameAccount" in autogear_cpp_src:
        raise AssertionError("TbAutogear must not bypass OwnerScope::Owns")


class AutogearWiring(unittest.TestCase):
    def test_player_cmd_and_scripts_wiring(self):
        self.assertIn('verb == "autogear"', PLAYER_CMD)
        self.assertIn("autogear::HandleBotAutogear", PLAYER_CMD)
        self.assertIn("TortoiseBots::autogear::AddScripts();", SCRIPTS)
        self.assertIn("HandleBotAutogear", AUTOGEAR_H)
        self.assertIn("AddScripts", AUTOGEAR_H)

    def test_ownership_via_owner_scope_owns(self):
        check_ownership_via_owner_scope(PLAYER_CMD, AUTOGEAR_CPP)
        self.assertIn("!callerGm && !player::OwnsGameAccount(scope, entry->AccountId)", AUTOGEAR_CPP)

    def test_autogear_uses_forever_gear_gen_and_tb_policies(self):
        self.assertIn("gear::InitGearIndex()", AUTOGEAR_CPP)
        self.assertIn("gear::SlotCandidates(", AUTOGEAR_CPP)
        self.assertIn("sRandomItemMgr.CalculateStatWeight", AUTOGEAR_CPP)
        self.assertIn("sPlayerbotAIConfig.ownedBotAutogearEnabled", AUTOGEAR_CPP)
        self.assertIn("sPlayerbotAIConfig.ownedBotAutogearQualityCap", AUTOGEAR_CPP)
        self.assertIn("sPlayerbotAIConfig.ownedBotAutogearIlvlCap", AUTOGEAR_CPP)
        self.assertIn("sPlayerbotAIConfig.playerAutogearForAll", AUTOGEAR_CPP)
        self.assertIn("BotMgr::Instance().IsPoolBot", AUTOGEAR_CPP)
        self.assertIn("MailDraft(", AUTOGEAR_CPP)
        self.assertIn("PlayerAutogear: caller=", AUTOGEAR_CPP)

    def test_pure_claim_boundary(self):
        check_pure_claim(AUTOGEAR_PURE)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated_claim = AUTOGEAR_PURE.replace("if (word.size() > kCmd.size() && !std::isspace(static_cast<unsigned char>(word[kCmd.size()])))", "if (false)")
        try:
            check_pure_claim(mutated_claim)
        except AssertionError:
            print("MUTATION CAUGHT: claim-boundary")
        else:
            print("ERROR: claim-boundary mutation survived", file=sys.stderr)
            sys.exit(1)

        mutated_own = AUTOGEAR_CPP.replace("player::OwnsGameAccount(scope, entry->AccountId)", "false")
        try:
            check_ownership_via_owner_scope(PLAYER_CMD, mutated_own)
        except AssertionError:
            print("MUTATION CAUGHT: ownership-scope")
            sys.exit(0)
        print("ERROR: ownership-scope mutation survived", file=sys.stderr)
        sys.exit(1)
    unittest.main()
