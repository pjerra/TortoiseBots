#!/usr/bin/env python3
"""Wiring pins for the dungeon-finder fill (ticket #160, the fill half).

The core calls cannot run without a server, so these pin the shape of the live side; the decisions themselves are tested
standalone by forever/tools/test_lfg_fill_policy.cpp. Each assertion fails on the source before the fill:
  1 `.tb lfg` is a row of the GM-gated `.tb` table and its WorldScript is registered by AddSC_tortoisebots
  2 the option-OFF build answers `engine-not-built` and registers no script
  3 the service is a `host/TbHost*.cpp` file, which CMake compiles only with TORTOISEBOTS_SLICE1
  4 the four AiPlayerbot.RandomBotLft* keys are read from the engine's config members, not from sConfigMgr
  5 the cadence is TB's: 5-60 s and at most 5 bots per pass
  6 the decisions come from host/TbLfgFillPure.h
  7 the fill never touches the core's dungeon finder (sLFGMgr, JoinLfg, UpdateProposal, UpdateRoleCheck)
  8 a bot joins only through BotMgr::GroupWith, gets the group's LFG bit and the kit role, and is let go with Ungroup + SetFree
  9 the player entry is the caller's own character only (join refused without a player sender)
 10 the header names no core or legacy type, so both builds and the OFF stub can include it
 11 the role commit is `.tb role`'s: stored role, snapshots, kit rebuild, then follow of the new master
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "forever/src"
SERVICE_PATH = SRC / "host/TbHostLfgFill.cpp"
HEADER_PATH = SRC / "host/TbHostLfgFill.h"
PURE_PATH = SRC / "host/TbLfgFillPure.h"
OFF_PATH = SRC / "host/TbH3Off.cpp"
SCRIPTS_PATH = SRC / "TbScripts.cpp"
ORDERS_PATH = SRC / "host/TbOrders.cpp"
API_PATH = SRC / "host/TbOrdersApi.h"
CMAKE_PATH = ROOT / "CMakeLists.txt"


def read(path):
    return pathlib.Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class Wiring(unittest.TestCase):
    def setUp(self):
        self.service = read(SERVICE_PATH)
        self.header = read(HEADER_PATH)
        self.pure = read(PURE_PATH)
        self.off = read(OFF_PATH)
        self.scripts = read(SCRIPTS_PATH)
        self.orders = read(ORDERS_PATH)
        self.api = read(API_PATH)
        self.cmake = read(CMAKE_PATH)

    def test_command_is_registered(self):
        self.assertIn('{ "lfg",    TortoiseBots::lfg::HandleLfg,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes }', self.scripts)
        self.assertIn("TortoiseBots::lfg::RegisterScripts();", self.scripts)
        self.assertIn('WorldScript("tortoisebots_lfg_fill")', self.service)
        self.assertIn("void RegisterScripts()", self.service)
        self.assertIn("new LfgFillWorldScript();", self.service)

    def test_off_build_answers_and_registers_nothing(self):
        self.assertIn("bool HandleLfg(ChatHandler* handler, Trinity::ChatCommands::Tail args)", self.off)
        self.assertIn('NotBuilt(handler, args, "lfg")', self.off)
        self.assertIn("void RegisterScripts() { }", self.off)

    def test_service_is_slice_only(self):
        # CMake excludes host/TbHost*.cpp from the default glob and adds them in the TORTOISEBOTS_SLICE1 branch.
        self.assertIn('EXCLUDE REGEX "/forever/src/(slice/[^/]*|host/TbHost[^/]*)\\\\.cpp$"', self.cmake)
        self.assertRegex(SERVICE_PATH.name, r"^TbHost")

    def test_config_keys_are_the_engine_members(self):
        for member in ("randomBotLftEnabled", "randomBotLftUpdateInterval", "randomBotLftMaxFillsPerInterval",
                       "randomBotLftAllowRoleBorrow"):
            self.assertIn("sPlayerbotAIConfig." + member, self.service, member)
        self.assertNotRegex(self.service, r'sConfigMgr->Get(?:Bool|Int|Float|String)Default\("AiPlayerbot\.RandomBotLft')
        # The engine's config has no default for `enabled`; the keys are read only once it loaded.
        self.assertIn("engine::ConfigReady()", self.service)
        self.assertIn("sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.randomBotLftEnabled", self.service)

    def test_cadence_is_tbs(self):
        self.assertIn("std::clamp<std::uint32_t>(sPlayerbotAIConfig.randomBotLftUpdateInterval, 5000, 60000)", self.service)
        self.assertIn("std::clamp<std::uint32_t>(sPlayerbotAIConfig.randomBotLftMaxFillsPerInterval, 0, 5)", self.service)
        self.assertIn("s.elapsedMs += diff;", self.service)

    def test_decisions_come_from_the_pure_policy(self):
        for call in ("lfgfill::MissingSlots", "lfgfill::PickCandidate", "lfgfill::ShouldRelease",
                     "lfgfill::CandidateFacts", "lfgfill::CoreRoleBits", "lfgfill::RoleOf"):
            self.assertIn(call, self.service, call)
        self.assertIn('#include "host/TbLfgFillPure.h"', self.service)

    def test_core_dungeon_finder_is_not_used(self):
        for forbidden in ("sLFGMgr", "JoinLfg(", "UpdateProposal(", "UpdateRoleCheck("):
            self.assertNotIn(forbidden, strip_comments(self.service), forbidden)

    def test_grouping_and_release_paths(self):
        self.assertIn("BotMgr::Instance().GroupWith(", self.service)
        self.assertIn("h3::SetBotRole(", self.service)
        self.assertIn("group->SetLfgRoles(botGuid, lfgfill::CoreRoleBits(need));", self.service)
        self.assertIn("group->SetLfgRoles(entry.player, lfgfill::CoreRoleBits(entry.role) | lfgfill::CoreRoleLeader);", self.service)
        self.assertIn("BotMgr::Instance().Ungroup(", self.service)
        self.assertIn("BotMgr::Instance().SetFree(", self.service)
        # The group is made through the core's own invite/accept path (BotMgr::GroupWith), never by editing a group.
        self.assertNotIn("AddMember(", strip_comments(self.service))
        self.assertNotIn("AddInvite(", strip_comments(self.service))
        self.assertNotIn("Group::Create", strip_comments(self.service))

    def test_join_needs_the_callers_character(self):
        self.assertIn("h3::SenderPlayer(handler)", self.service)
        self.assertIn('"needs-player"', self.service)
        # `.tb lfg` is a GM command; the queue entry belongs to the sender's own character, never a named real player's.
        self.assertNotIn("h3::FindOnline(", strip_comments(self.service))

    def test_header_is_core_free_for_both_builds(self):
        for forbidden in ("Group.h", "Player.h", "playerbot/", "ObjectGuid.h", "Define.h"):
            self.assertNotIn(forbidden, strip_comments(self.header), forbidden)
        self.assertIn('#include "ChatCommand.h"', self.header)

    def test_player_entry_is_the_callers_own_character(self):
        player_cmd = read(SRC / "host/player/TbPlayerCmd.cpp")
        self.assertIn('#include "host/TbHostLfgFill.h"', player_cmd)
        self.assertIn("return lfg::HandlePlayerLfg(player, AfterFirstWord(rest));", player_cmd)
        self.assertIn(".bot lfg join <dungeon id> <tank|healer|dps> | status | leave", player_cmd)
        self.assertIn("bool HandlePlayerLfg(Player* who, std::string_view tail);", self.header)
        self.assertIn("TortoiseBots::player::Say(", self.service)
        self.assertIn('TortoiseBots::player::Refuse(who, "lfg", code, detail)', self.service)
        # The player entry has no name argument: the queue entry is always the caller's own character.
        self.assertNotIn("h3::FindOnline(", strip_comments(self.service))

    def test_role_commit_is_the_tb_role_commit(self):
        self.assertIn("uint8 BotNaturalRoles(Player* bot);", self.api)
        self.assertIn("bool SetBotRole(Player* bot, uint8 role, std::string& code, std::string& detail);", self.api)
        self.assertIn("PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), role);", self.orders)
        self.assertIn("sPlayerbotDbStore.InvalidateStrategySnapshots(ai);", self.orders)
        self.assertIn("ApplyKit(ai, role, true);", self.orders)
        self.assertIn('ai->SetMovementStrategy("follow");', self.orders)
        self.assertIn("TortoiseBots::slice::hybrid::PlanTankOrder(", self.orders)


if __name__ == "__main__":
    unittest.main()
