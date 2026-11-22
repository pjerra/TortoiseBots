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
        self.assertIn("raidgroups::PlanJoin(_raidsEnabled && allowRaid", login)
        self.assertIn("raidgroups::PlanJoin(raidsEnabled", join)
        self.assertIn("Instance().RaidsEnabled() && allowRaid", join)
        self.assertIn("h3::RaidConvertBlocked(group, blocked)", join)
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
        for needle in ("raidgroups::PickMainTank(", "RL::Plan(", "RL::PlanSubgroupMoves(raidgroups::LayoutMoves(seats))",
                       "group->HasFreeSlotSubGroup(op.sub)", "group->ChangeMembersGroup(",
                       "group->SwapMembersGroups(", "MEMBER_FLAG_MAINTANK", "MEMBER_FLAG_ASSISTANT", "FocusHeal("):
            self.assertIn(needle, handler, needle)
        joiner = body(layout, "int PlaceJoiner(")
        self.assertIn("RL::PlanJoinerSubgroup(", joiner)
        self.assertIn("raidgroups::PruneHealBook(applied, tanksNow, healersNow)", joiner)
        self.assertIn("raidgroups::PickAndRecordHealer(applied", joiner)
        self.assertIn("group->HasFreeSlotSubGroup(seat.subgroup)", joiner)
        self.assertLess(joiner.index("HasFreeSlotSubGroup(seat.subgroup)"), joiner.index("ChangeMembersGroup("))
        # Every member slot is in the moves (an offline member keeps its seat), not only the members in the world.
        self.assertIn("slots.push_back(slot.guid);", handler)
        self.assertLess(handler.index("slots.push_back(slot.guid);"), handler.index("if (!member || !member->IsInWorld())"))
        # Roles without an engine: the class and talents (PlayerbotAI::IsTank / IsHeal), not only the stored role.
        role = body(layout, "RL::Role RoleFor(Player* member)\n{")
        self.assertIn("PlayerbotAI::IsTank(member, true)", role)
        self.assertNotIn("GetAI(member)", role)

    def test_layout_record_goes_with_the_group(self):
        layout = read("forever/src/host/TbRaidLayout.cpp")
        self.assertIn("Layouts().erase(group->GetGUID())", body(layout, "void ForgetLayout("))
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn("TortoiseBots::h3::ForgetLayout(group);", body(scripts, "void OnDisband(Group* group) override"))
        off = read("forever/src/host/TbH3Off.cpp")
        self.assertIn("void ForgetLayout(Group*) {}", off)
        self.assertIn("bool RaidConvertBlocked(Group*, std::string&) { return false; }", off)

    def test_only_the_explicit_commands_may_make_a_raid(self):
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn("BotMgr::Instance().Login(GuidByName(name), master, free, false, false, true)", scripts)   # .tb login
        self.assertIn("BotMgr::Instance().GroupWith(guid, master, true)", scripts)                              # .tb group
        self.assertIn("GroupWith(member->GetGUID(), leader->GetGUID(), true)", read("forever/src/host/TbPartyState.cpp"))
        self.assertIn("BotMgr::Instance().GroupWith(bot, m.lead);", read("forever/src/host/cmd/TbCmdBinding.cpp"))  # restore: 5-man
        self.assertIn("BotMgr::Instance().Login(guid, player->GetGUID(), false, false, true);", read("forever/src/host/player/TbPlayerCmd.cpp"))
        mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn("if (JoinParty(member, leader).empty())", mgr)                                           # quest groups: 5-man
        self.assertIn("JoinParty(me, master, bot.allowRaid)", mgr)                                             # the login's own join
        self.assertIn("bot.allowRaid = allowRaid;", body(mgr, "std::string BotMgr::Login("))
        self.assertIn("JoinParty(bot, master, allowRaid)", body(mgr, "std::string BotMgr::GroupWith("))

    def test_party_forming_caps_by_the_toggle_and_places_joiners(self):
        party = body(read("forever/src/host/TbPartyState.cpp"), "bool HandleParty(")
        self.assertIn("raidgroups::MaxNamedMembers(raids)", party)
        self.assertIn("PlaceJoiner(group, member, &heals)", party)
        self.assertIn('rec.S("heals", heals)', party)
        self.assertIn('if (slot.group < MAX_RAID_SUBGROUPS)', party)
        self.assertLess(party.index("if (slot.group < MAX_RAID_SUBGROUPS)"), party.index('rec.N("sub", slot.group + 1)'))

    def test_slice_registers_the_focus_heal_strategy(self):
        ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
        self.assertIn('creators["focus heal targets"] = [](PlayerbotAI* ai) { return new FocusHealTargetsStrategy(ai); };', ctx)
        self.assertIn('#include "playerbot/strategy/generic/FocusTargetStrategy.h"', ctx)


if __name__ == "__main__":
    unittest.main()
