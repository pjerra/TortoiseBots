#!/usr/bin/env python3
"""Wiring pin for the second joint live test (JOINT-LIVE-2) fixes in the module.

  * a bot that landed in an instance with its party keeps (and gets back) its engine although its commander stands on another map
    (EngineKeptOnOtherMap, host/TbEngineMapPure.h; forever/tools/test_engine_map.cpp proves the rule);
  * the run's binding of a member holds through a pause and a regroup: SyncBinding puts back a commander entry the engine's master logic dropped while
    the host master still is the run's lead, and puts the host master back when the engine moved it under the run's own entry: the repair writes the
    host bind AND `h3::SetCommander`, in that order, so one sync makes the next one Keep (SyncBindingAction, host/run/TbRunBindingPure.h;
    forever/tools/test_run_binding.cpp proves the rule); `.tb party` reads the party's commander from its MEMBERS (not the leader's own binding,
    which made a run's party read `commander=mixed`);
  * "its party is here" is a question about the group's MEMBERS, not the leader's identity: one other member in the world, not between maps, on the bot's
    exact map (the same Map, so the same instance) keeps the engine, so a bot that leads its own group still keeps it beside its party while a lone
    leader has none (PartyMemberOnMap, host/TbEngineMapPure.h);
  * `.tb party <leader> <member>...` refuses a member that is in another (persisted) party with an error that names it, before anything changes, and
    `.tb run start` names an offline member left by such a group.

These assertions fail on the commit before the fixes (the review round added the entry states, the SetCommander write and the identity check).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"


def text(rel):
    return (SRC / rel).read_text()


def block(source, marker, end_marker):
    start = source.index(marker)
    end = source.index(end_marker, start)
    return source[start:end]


class Joint2(unittest.TestCase):
    def test_the_engine_stays_for_an_instance_party(self):
        mgr = text("TbBotMgr.cpp")
        self.assertIn('#include "host/TbEngineMapPure.h"', mgr)
        branch = block(mgr, "bool kept = false;", "if (changed)")
        self.assertIn("kept = EngineKeptOnOtherMap(me->IsAlive(), inInstance, partyHere);", branch)
        self.assertRegex(branch, r"else if \(!kept\)\s*\{[\s\S]*?DropBrain\(bot\);")
        self.assertIn("if (!sameMap && !kept)", mgr)
        self.assertLess(mgr.index("kept = EngineKeptOnOtherMap("), mgr.index("if (!sameMap && !kept)"))

    def test_the_run_binding_is_reasserted(self):
        run = text("host/run/TbRun.cpp")
        self.assertIn('#include "host/run/TbRunBindingPure.h"', run)
        sync = block(run, "void SyncBinding(", "void BaselineOrders(")
        self.assertIn("SyncBindingAction(boundByRun,", sync)
        # The rule reads the entry as the table stores it (StoredCommander), because an entry that lapsed under a moved host master is exactly the
        # state it repairs; CommanderGuid's "the host master agrees" check would hide it.
        self.assertIn("BindingEntryOf(h3::StoredCommander(slot.guid), rec.leader)", sync)
        # The repair writes BOTH tables: the host master (BindBotMaster, the engine's pointer with it) and the commander entry h3::SetCommander,
        # which the bind-only repair never wrote, so the condition stayed true and the branch ran every tick.
        self.assertIn("RepairBinding(", sync)
        self.assertLess(sync.index("BindBotMaster(slot.guid, rec.leader)"), sync.index("h3::SetCommander(slot.guid, rec.leader)"))
        self.assertLess(sync.index("SyncBindingAction(boundByRun,"), sync.index("BindMember(rec, bot, lead);"))

    def test_a_lapsed_entry_is_told_apart(self):
        run = text("host/run/TbRun.cpp")
        entry = block(run, "BindingEntry BindingEntryOf(", "void SyncBinding(")
        self.assertIn("if (stored.IsEmpty())", entry)
        self.assertIn("return BindingEntry::Missing;", entry)
        self.assertIn("return stored == lead ? BindingEntry::Lead : BindingEntry::Other;", entry)
        h3 = text("host/TbH3.cpp")
        stored = block(h3, "ObjectGuid StoredCommander(", "std::vector<std::pair<ObjectGuid, ObjectGuid>> CommanderBindings()")
        self.assertIn("return itr == Bindings().end() ? ObjectGuid::Empty : itr->second;", stored)

    def test_the_party_is_the_members_on_the_bot_map(self):
        mgr = text("TbBotMgr.cpp")
        self.assertIn('#include "host/TbEngineMapPure.h"', mgr)
        branch = block(mgr, "bool kept = false;", "if (changed)")
        # The group LEADER's identity is no proxy for the party: a run's leader bot answers itself as its group's leader and still has its members
        # standing beside it, while a lone leader has none. The rule scans the group's members and asks the pure predicate about each.
        self.assertNotIn("GetLeaderGUID", branch)
        scan = block(branch, "bool partyHere = false;", "kept = EngineKeptOnOtherMap(")
        self.assertIn("for (Group::MemberSlot const& slot : group->GetMemberSlots())", scan)
        self.assertIn("me->GetMap() == member->GetMap()", scan)
        self.assertIn("PartyMemberOnMap(slot.guid == me->GetGUID(), member->IsInWorld(), member->IsBeingTeleported(),", scan)
        self.assertRegex(scan, r"if \(member && PartyMemberOnMap\([\s\S]*?\)\)\s*\{\s*partyHere = true;\s*break;")

    def test_the_party_commander_is_its_members(self):
        party = text("host/TbPartyState.cpp")
        self.assertRegex(party, r"if \(bound && commander && !group->IsLeader\(slot\.guid\)\)\s*\n\s*commanders\.insert\(")

    def test_a_member_in_another_party_is_named(self):
        party = text("host/TbPartyState.cpp")
        formation = block(party, "if (args.w.size() > 1)", "Reply reply(handler, args, \"party\");")
        self.assertIn('Fail(handler, args, "party", "party-refused"', formation)
        self.assertIn("is already in another party (", formation)
        self.assertIn(".tb ungroup ", formation)
        # The pre-check comes before anything is joined.
        self.assertLess(party.index('"party-refused"'), party.index("BotMgr::Instance().GroupWith("))

    def test_the_run_start_names_an_offline_member(self):
        run = text("host/run/TbRun.cpp")
        self.assertRegex(run, r'is not in the world \(an offline member left by an old group[\s\S]{0,120}slot\.name')


if __name__ == "__main__":
    unittest.main()
