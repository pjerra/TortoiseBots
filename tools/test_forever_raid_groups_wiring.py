#!/usr/bin/env python3
"""Raid groups on the Forever host: the pins that tie the pure rules (forever/src/host/TbRaidGroupsPure.h, tested by
tools/test_forever_raid_groups.cpp) and the raid layout (runtime/RaidLayoutPolicy.h) to their call sites.

    python3 -m unittest tools/test_forever_raid_groups_wiring.py
"""
import os
import re
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def body(src, signature):
    start = src.index(signature)
    depth, i = 0, src.index("{", start)
    while True:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
        i += 1


class RaidGroupsWiring(unittest.TestCase):
    def test_login_and_join_use_the_join_plan(self):
        mgr = read("forever/src/TbBotMgr.cpp")
        login = body(mgr, "std::string BotMgr::Login(")
        join = body(mgr, "std::string BotMgr::JoinParty(")
        self.assertIn("raidgroups::PlanJoin(_raidsEnabled", login)
        self.assertIn("raidgroups::PlanJoin(raidsEnabled", join)
        self.assertIn("Instance().RaidsEnabled()", join)
        self.assertNotIn("raids are not in this slice", mgr)

    def test_party_becomes_a_raid_after_the_invite_and_before_the_member(self):
        join = body(read("forever/src/TbBotMgr.cpp"), "std::string BotMgr::JoinParty(")
        invite = join.index("group->AddInvite(bot)")
        convert = join.index("group->ConvertToRaid()")
        add = join.index("group->AddMember(bot)")
        self.assertLess(invite, convert)
        self.assertLess(convert, add)

    def test_toggle_default_matches_the_template(self):
        mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn('GetBoolDefault("TortoiseBots.Raid.Enable", false)', mgr)
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertRegex(conf, r"(?m)^TortoiseBots\.Raid\.Enable = 0$")
        self.assertRegex(conf, r"(?m)^#        Default:     0\n#\n\nTortoiseBots\.Raid\.Enable = 0$")

    def test_layout_command_registered_with_a_stub_and_engine_only_source(self):
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn('{ "layout", TortoiseBots::h3::HandleLayout, rbac::RBAC_PERM_COMMAND_GM, Console::Yes }', scripts)
        self.assertIn('bool HandleLayout(', read("forever/src/host/TbH3Off.cpp"))
        cmake = read("CMakeLists.txt")
        self.assertRegex(cmake, r"host/\([^)]*\bTbRaidLayout\b[^)]*\)\\\\\.cpp\$")

    def test_layout_uses_the_policy_and_the_core_group_api(self):
        layout = read("forever/src/host/TbRaidLayout.cpp")
        handler = body(layout, "bool HandleLayout(")
        for needle in ("raidgroups::PickMainTank(", "RL::Plan(", "RL::PlanSubgroupMoves(", "group->ChangeMembersGroup(",
                       "group->SwapMembersGroups(", "MEMBER_FLAG_MAINTANK", "MEMBER_FLAG_ASSISTANT", "FocusHeal("):
            self.assertIn(needle, handler, needle)
        joiner = body(layout, "int PlaceJoiner(")
        self.assertIn("RL::PlanJoinerSubgroup(", joiner)
        self.assertIn("RL::PickJoinerHealTank(", joiner)

    def test_party_forming_caps_by_the_toggle_and_places_joiners(self):
        party = body(read("forever/src/host/TbPartyState.cpp"), "bool HandleParty(")
        self.assertIn("raidgroups::MaxNamedMembers(raids)", party)
        self.assertIn("PlaceJoiner(group, member)", party)
        self.assertIn('rec.N("sub", slot.group + 1)', party)

    def test_slice_registers_the_focus_heal_strategy(self):
        ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
        self.assertIn('creators["focus heal targets"] = [](PlayerbotAI* ai) { return new FocusHealTargetsStrategy(ai); };', ctx)
        self.assertIn('#include "playerbot/strategy/generic/FocusTargetStrategy.h"', ctx)


if __name__ == "__main__":
    unittest.main()
