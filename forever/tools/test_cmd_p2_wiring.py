#!/usr/bin/env python3
"""Wiring pins and mutations for the commander binding lifecycle and its persistence (package P2, forever/docs/CMD-P2-NOTES.md).

The core calls cannot run without a server, so these tests pin where the hooks stand and what the lifecycle must never do; every assertion fails on the
pre-P2 source (the hooks, the store, the row and the config key do not exist there). With CMD_P2_MUTATE=1 (run on the build VM: they compile
forever/tools/cmd_binding_life_check.cpp and cmd_binding_store_check.cpp) the mutations break the pure half, the row checker and the store, and each
must FAIL:
  lead-offline  a restore whose lead is not in the world
  dead-grace    a dead lead released before its grace
  away-grace    an absent commander released before its grace
  group-column  a group column or id in the persisted row (the spec's failing case: the core reuses database group ids)
  rows-skip-first  a stored-row reader that advances before its first Fetch (the core's Query already stands on row 1)
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"
CMD = HOST / "cmd"
TOOLS = ROOT / "forever" / "tools"


def text(path):
    return Path(path).read_text()


def code(source):
    """The source without // comments (the notes in them name the very calls the tests forbid)."""
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


# The five fields of a stored row, in the select and in the insert. A group id or a group counter here is the spec's failing case (the core reuses
# its database group ids, so a stale number names another party).
ROW_COLUMNS = ["commander_guid", "lead_guid", "members", "flags", "bound_at"]


def check_row_columns(source):
    select = re.search(r'kSelect = "SELECT (.+?) FROM', source)
    assert select, "the stored row's select is gone"
    columns = re.findall(r"`([a-z_]+)`", select.group(1))
    if columns != ROW_COLUMNS:
        raise AssertionError(f"the stored row selects {columns}, not the five fields {ROW_COLUMNS}")
    # The upsert's column list is the kColumns literal (one statement for a save and for a replace): both must be the same five fields.
    named = re.search(r'kColumns = "(.+?)";', source)
    assert named, "the stored row's write column list is gone"
    written = re.findall(r"`([a-z_]+)`", named.group(1))
    if written != ROW_COLUMNS:
        raise AssertionError(f"the stored row writes {written}, not the five fields {ROW_COLUMNS}")
    insert = re.search(r"INSERT INTO `tb_commander_binding` \((?:\{\}|[^)]+)\) VALUES", source)
    assert insert, "the stored row's insert is gone"
    if "({})" not in insert.group(0):
        raise AssertionError("the insert no longer writes the row through kColumns")
    for word in ("group", "dbstore", "db_store"):
        if word in " ".join(columns + written).lower():
            raise AssertionError(f"a group number is persisted ({word})")


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = text(CMD / "TbCmdBinding.cpp")
        self.hdr = text(CMD / "TbCmdBinding.h")
        self.pure = text(CMD / "TbCmdBindingLifePure.h")
        self.store = text(CMD / "TbCmdBindingStore.cpp")
        self.store_hdr = text(CMD / "TbCmdBindingStore.h")
        self.char_guard = text(ROOT / "forever" / "src" / "compat" / "TbCompat_CharStore.h")
        self.scripts = text(ROOT / "forever" / "src" / "TbScripts.cpp")
        self.dash = text(ROOT / "forever" / "src" / "dash" / "TbDashRuns.cpp")
        self.orders = text(HOST / "run" / "TbRunOrders.cpp")
        self.ownership = text(HOST / "TbHostOwnership.cpp")
        self.conf = text(ROOT / "forever" / "conf" / "tortoisebots.conf.dist")
        self.sql = text(ROOT / "forever" / "sql" / "characters" / "2026_11_20_00_tb_commander_binding.sql")

    def test_the_group_and_player_hooks_are_registered(self):
        self.assertIn("class tortoisebots_group : public GroupScript", self.scripts)
        self.assertIn("new tortoisebots_group();", self.scripts)
        group = self.scripts.split("class tortoisebots_group")[1].split("class tortoisebots_commands")[0]
        for hook in ("OnAddMember", "OnRemoveMember", "OnChangeLeader", "OnDisband"):
            self.assertIn(hook, group)
        for note in ("NoteGroupAdd", "NoteGroupRemove", "NoteGroupLeader", "NoteGroupDisband"):
            self.assertIn(note, group)
        self.assertIn("TortoiseBots::cmd::NoteLogin(player)", self.scripts)
        self.assertIn("TortoiseBots::cmd::NoteLogout(player)", self.scripts)
        self.assertIn("cmd::NoteDeath", self.scripts)
        self.assertIn("TortoiseBots::cmd::Maintain();", self.scripts)      # the two timers and a pending restore, once a second
        self.assertIn("TortoiseBots::cmd::LoadBindings();", self.scripts)  # the key, the graces and the stored rows

    def test_the_release_reasons_come_from_the_live_group_and_the_shared_rule(self):
        facts = function_body(self.cpp, r"L::Facts FactsFor\(ObjectGuid commander, Meta& m, Player\* commanderPlayer, Player\* lead\)\s*\{")
        for call in ("lead->GetGroup()", "group->GetMembersCount()", "group->isRaidGroup()", "group->IsLeader(lead->GetGUID())",
                     "group->IsMember(commander)"):
            self.assertIn(call, facts)                                     # the facts are read from the lead's live group, no stored number
        # The commander's membership is read by guid from the group's own member list: an offline or zoning member has not left the party.
        self.assertNotIn("IsMember(commanderPlayer->GetGUID())", code(facts))
        self.assertNotIn("GetDbStoreId", code(self.cpp + self.pure + self.store + self.store_hdr))
        maintain = function_body(self.cpp, r"void Maintain\(\)\s*\{")
        self.assertIn("L::Decide(RowOf(commander, m), facts)", maintain)
        self.assertIn("ReleaseCommander(commander, end)", maintain)
        # ... and it is 1 Hz, whatever rate the world tick runs at (the core calls Maintain on every tick): the throttle stands before the work.
        self.assertIn("kMaintainMs", maintain)
        self.assertLess(maintain.index("kMaintainMs", maintain.index("kMaintainMs") + 1), maintain.index("TryRestore()"))
        # A card the read made for a run's tank has no lead: the release rule and the restore never touch it (its pairs belong to the run).
        self.assertIn("!m.lead.IsEmpty()", maintain)
        # A stored row whose DELETE did not land is owed, not forgotten: the upkeep retries it even with nothing standing.
        self.assertIn("store::RetryErases()", maintain)
        self.assertLess(maintain.index("store::RetryErases()"), maintain.index("if (s.metas.empty())"))
        release = function_body(self.cpp, r"void ReleaseCommander\(ObjectGuid commander, L::End end(?:, Group\* partyGroup = nullptr, bool partyKnown = false)?\)\s*\{")
        self.assertIn("h3::SetCommander(bot, ObjectGuid::Empty)", release)  # h3's table is the owner: a release clears its pairs
        self.assertIn("bool const erased = store::Erase(commander.GetCounter())", release)  # and the stored row, read for its verdict
        self.assertIn("store::PendingErase(commander.GetCounter())", release)
        self.assertIn("the release is not complete", release)               # a durable row is never reported as a completed release
        self.assertIn("RestoreFollower(follower, m.lead)", release)         # the followers' own follow targets come back
        self.assertIn("ClearBotMaster(bot)", release)                       # a released bot keeps its former commander as neither master nor...
        self.assertIn("s.log.Retain(", release)                             # ...and the released card's order log goes with it at once
        self.assertIn("RC::DecideReleaseParty", function_body(self.cpp, r"void ApplyPartyAction\(ObjectGuid commander, Meta const& m, L::End end, Group\* group\)\s*\{"))
        # A shutdown keeps everything (that is what the next start restores); the two timers are the only delayed releases.
        logout = function_body(self.cpp, r"void NoteLogout\(Player\* player\)\s*\{")
        self.assertIn("World::IsStopped()", logout)
        self.assertIn("m.commanderGoneSinceMs", logout)
        self.assertIn("L::End::LeadLeft", logout)
        login = function_body(self.cpp, r"void NoteLogin\(Player\* player\)\s*\{")
        self.assertIn("m.commanderGoneSinceMs = 0", login)                  # back inside the grace: the binding stands
        self.assertIn("TryRestore()", login)

    def test_the_persisted_row_has_five_fields_and_no_group(self):
        check_row_columns(self.store)
        model = code(re.search(r"struct Stored\s*\{.*?\};", self.pure, re.S).group(0))
        self.assertIn("commander", model)
        self.assertIn("lead", model)
        self.assertIn("members", model)
        self.assertIn("flags", model)
        self.assertIn("boundAt", model)
        for word in ("group", "dbstore", "db_store", "getdbstoreid"):
            self.assertNotIn(word, model.lower())                           # no group member of the stored model, whatever its spelling
        self.assertIn("PRIMARY KEY (`commander_guid`)", self.sql)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tb_commander_binding`", self.sql)

    def test_persistence_is_off_by_default_and_every_read_is_guarded(self):
        self.assertIn("TortoiseBots.Commander.KeepBindings = 0", self.conf)
        self.assertIn('GetBoolDefault("TortoiseBots.Commander.KeepBindings", false)', self.store)
        # The ForeverCore schema guard: information_schema before any SELECT, and the CREATE on a synchronous connection before the check. The raw
        # characters statements live in compat/TbCompat_CharStore.h (compat/TbCompat_S5.h is the world twin), so the store itself makes none.
        guard = function_body(self.store, r"QueryResult CharacterQueryCompat\(char const\* sql, bool\* schemaMissing = nullptr\)\s*\{")
        self.assertIn("WorldQueryNamesCompat", guard)                       # the same single-table parser as the world guard
        self.assertIn("CharacterSchemaHasCompat(table, columns)", guard)   # the probe, before the select
        self.assertIn("CharacterQueryGuardedCompat(text)", guard)
        self.assertNotIn("CharacterDatabase", code(self.store))             # no raw statement in the store: all of them go through the guard
        table = function_body(self.store, r"bool CheckSchema\(State& s, bool create\)\s*\{")
        self.assertIn("CharacterExecuteGuardedCompat(kCreate)", table)     # the CREATE only, once per config
        self.assertIn("CharacterSchemaHasCompat(kTable, columns)", table)
        self.assertIn("if (create && !s.tableTried)", table)               # DDL is not retried on every write
        self.assertIn("information_schema.COLUMNS", self.char_guard)       # the probe asks before it names a table
        self.assertEqual(len(re.findall(r"CharacterDatabase\.Query\(", self.char_guard)), 2)   # probe + guarded select
        self.assertIn("CharacterDatabase.DirectExecute(sql.c_str())", self.char_guard)        # the one synchronous statement shape
        # The writes are one-way statements on a synchronous connection, never queued ones (a pool may reorder them), and every one of them is
        # preceded by the same five-column check: a cached `tableReady` is never trusted (the review's finding).
        self.assertNotIn("CharacterDatabase.Execute(", code(self.store))
        self.assertNotIn("PExecute", code(self.store))
        self.assertNotIn("PQuery", code(self.store))
        writable = function_body(self.store, r"bool Writable\(State& s\)\s*\{")
        self.assertIn("CheckSchema(s, true)", writable)
        for name in ("Save", "Erase", "Rows"):
            body = function_body(self.store, rf"(?:bool|std::vector<life::Stored>) {name}\(")
            self.assertIn("if (!s.enabled", body, f"{name} must do nothing with the key off")
        save = function_body(self.store, r"bool Save\(life::Stored const& stored\)\s*\{")
        self.assertIn("Writable(s)", save)
        self.assertIn("ON DUPLICATE KEY UPDATE", save)                       # one atomic statement: no DELETE and INSERT to split
        self.assertIn("CharacterExecuteGuardedCompat(sql)", save)            # one synchronous, guarded statement
        self.assertIn("ReadRows()", save)                                   # the in-memory view changes only after the read-back
        erase = function_body(self.store, r"bool Erase\(uint64_t commander\)\s*\{")
        self.assertIn("Writable(s)", erase)
        self.assertIn("CharacterExecuteGuardedCompat(Trinity::StringFormat(\"DELETE FROM `tb_commander_binding`", erase)
        self.assertIn("ReadRows()", erase)
        # The guard only ever names this store's table: a table name can never be built from anywhere but this file.
        self.assertIn("table != kTable", guard)
        # A reload re-reads: the load state, the schema flag and the rows all start over.
        config = function_body(self.store, r"void LoadConfig\(\)\s*\{")
        for reset in ("s.loadTried = false", "s.tableReady = false", "s.rows.clear()"):
            self.assertIn(reset, config)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tb_commander_binding`", self.store)

    def test_a_failed_delete_is_pending_and_never_restored(self):
        # A DELETE that did not land leaves the row durable: it is marked pending, kept out of Rows(), retried by the upkeep, and cleared by a
        # later Save of the same commander (a rebind is newer than the delete it replaces).
        erase = function_body(self.store, r"bool Erase\(uint64_t commander\)\s*\{")
        self.assertIn("s.pendingErase.insert(commander)", erase)
        visible = function_body(self.store, r"std::vector<life::Stored> VisibleRows\(State& s\)\s*\{")
        self.assertIn("s.pendingErase.count(row.commander)", visible)
        pending = function_body(self.store, r"bool PendingErase\(uint64_t commander\)\s*\{")
        self.assertIn("s.pendingErase.count(commander) != 0", pending)
        retry = function_body(self.store, r"bool RetryErases\(\)\s*\{")
        self.assertIn("Erase(commander)", retry)
        save = function_body(self.store, r"bool Save\(life::Stored const& stored\)\s*\{")
        self.assertIn("s.pendingErase.erase(stored.commander)", save)
        # A config reload clears the cached rows and the load state, never the pending delete.
        config = function_body(self.store, r"void LoadConfig\(\)\s*\{")
        self.assertNotIn("pendingErase.clear", config)
        # No restore path reaches a pending commander: the row walk, the reload and the restore itself all skip it.
        load = function_body(self.cpp, r"void LoadBindings\(\)\s*\{")
        self.assertIn("store::PendingErase(row.commander)", load)
        restore = function_body(self.cpp, r"void RestoreOne\(ObjectGuid commander\)\s*\{")
        self.assertIn("store::PendingErase(commander.GetCounter())", restore)
        try_restore = function_body(self.cpp, r"void TryRestore\(\)\s*\{")
        self.assertIn("store::PendingErase(commander.GetCounter())", try_restore)

    def test_the_stored_rows_start_at_the_first_row(self):
        rows = function_body(self.store, r"std::vector<life::Stored> ReadRows\(\)\s*\{")
        # ForeverCore's Query returns the result standing on its first row (BasicStatementTask::Query calls NextRow): the reader must consume
        # that row before advancing. A `while (NextRow())` first drops it - a one-row table reads as empty and a refused delete looks landed.
        self.assertRegex(code(rows), r"do\s*\{\s*Field\* fields = result->Fetch\(\)")
        self.assertNotRegex(code(rows), r"while \(result->NextRow\(\)\)\s*\{\s*Field\* fields = result->Fetch\(\)")

    def test_the_restore_needs_the_commander_and_the_lead(self):
        restore = function_body(self.cpp, r"void RestoreOne\(ObjectGuid commander\)\s*\{")
        self.assertIn("if (!commanderPlayer || !AuthorizedCommander(commanderPlayer))", restore)   # a real player or our own bot, never a stranger
        self.assertIn("L::PlanRestore(row, true, online)", restore)
        self.assertIn("if (!plan.ready)", restore)                           # PlanRestore refuses a lead that is not in the world
        self.assertIn("BotMgr::Instance().GroupWith(bot, m.lead)", restore)  # the party is formed again through the lead
        self.assertIn("BotManager::Instance().BindBotMaster(bot, commander)", restore)
        self.assertIn("h3::SetCommander(bot, commander)", restore)
        # The host's own tables authorize every stored member (never a bot of another commander, never a real player) before any grouping or bind.
        self.assertIn("L::Claim(", restore)
        self.assertIn("RefuseRow(", restore)
        # An in-party binding puts the commander back into the party (otherwise the release rule ends it on the first tick).
        self.assertIn("group->AddMember(commanderPlayer)", restore)
        load = function_body(self.cpp, r"void LoadBindings\(\)\s*\{")
        self.assertIn("store::Rows()", load)
        self.assertIn("m.restored = false", load)                             # a row is not a standing binding until its party is back

    def test_a_restore_never_takes_a_bot_from_another_commander(self):
        try_restore = function_body(self.cpp, r"void TryRestore\(\)\s*\{")
        self.assertIn("h3::CommanderGuid(member).IsEmpty()", try_restore)   # only an UNBOUND member is joined again
        self.assertNotIn("h3::CommanderGuid(member) != commander", try_restore)
        restore = function_body(self.cpp, r"void RestoreOne\(ObjectGuid commander\)\s*\{")
        self.assertIn("if (!h3::CommanderGuid(bot).IsEmpty())", restore)    # the last check before a bind: h3 still has no commander
        self.assertIn("L::Owner", restore)
        self.assertIn("MasterOf(member)", restore)                          # the master table authorizes a member too
        self.assertIn("BotMgr::Instance().IsBot(member)", restore)
        # A binding dropped without a release deletes its row (an orphan would restore a binding nobody stands behind after a restart).
        prune = function_body(self.cpp, r"void PruneToStanding\(State& s\)\s*\{")
        self.assertIn("store::Erase(Ctr(it->first))", prune)
        commanded = function_body(self.cpp, r"bool IsCommandedBy\(Player\* commander, Player\* bot\)\s*\{")
        self.assertIn("h3::CommanderGuid(bot->GetGUID()) == commander->GetGUID()", commanded)

    def test_the_player_led_flag_is_read_from_the_group(self):
        check_player_led(self.cpp)
        # Neither a constant flag, nor a leader rule that ignores the commander, nor a gate that keeps an outside-party binding, nor a hook that
        # reads the leader from the group (it would write the PREVIOUS leader): each mutation must FAIL the pin.
        for mutation in (self.cpp.replace("inParty && group->IsLeader(commander->GetGUID())", "false"),
                         self.cpp.replace("facts.leaderGuid = group->GetLeaderGUID().GetCounter();", ""),
                         self.cpp.replace("RefreshPlayerLed(m, newLeader.GetCounter(), commander)",
                                          "RefreshPlayerLed(m, group->GetLeaderGUID().GetCounter(), commander)"),
                         self.cpp.replace("bool const commanderLeads = (m.flags & L::FlagInParty) && newLeader == commander;",
                                          "bool const commanderLeads = newLeader == commander;"),
                         self.cpp.replace("bool const playerLed = (m.flags & L::FlagInParty) && leaderGuid == commander.GetCounter();",
                                          "bool const playerLed = leaderGuid == commander.GetCounter();")):
            with self.subTest(mutation=mutation[:40]):
                with self.assertRaises(AssertionError):
                    check_player_led(mutation)

    def test_an_offline_or_zoning_commander_is_still_in_the_party(self):
        check_commander_in_group(self.cpp)
        # THE MUTATION: the in-world Player pointer the first round used reports an offline member as gone and releases before the grace.
        with self.assertRaises(AssertionError):
            check_commander_in_group(self.cpp.replace(
                "facts.commanderInGroup = group->IsMember(commander);",
                "facts.commanderInGroup = commanderPlayer && group->IsMember(commanderPlayer->GetGUID());"))

    def test_a_battleground_bind_commands_nothing_after_the_commander_leaves(self):
        # #315: the authorisation read the stored member and h3's row, while the clean-up runs once a second: between the commander's leave and
        # the next tick a command was still accepted.
        check_bg_command_gate(self.cpp)
        # THE MUTATION: the pre-fix check (the stored member plus h3's row, no live instance or team) still authorises the gap.
        with self.assertRaises(AssertionError):
            check_bg_command_gate(self.cpp.replace(
                "L::MayCommandBg(h3::CommanderGuid(bot->GetGUID()) == commander->GetGUID(), facts)",
                "L::MayCommand(true, false, h3::CommanderGuid(bot->GetGUID()) == commander->GetGUID(), true)"))
        # A gate that stops reading the team (or one instance) is the same failed case with half the facts.
        with self.assertRaises(AssertionError):
            check_bg_command_gate(self.cpp.replace("            facts.sameTeam = bot->GetBGTeam() == commander->GetBGTeam();\n", ""))
        with self.assertRaises(AssertionError):
            check_bg_command_gate(self.cpp.replace("            facts.commanderInInstance = member->instance && commander->GetBattlegroundId() == member->instance;\n", ""))

    def test_an_in_party_commander_leaving_ends_his_binding(self):
        check_commander_removal(self.cpp)
        # THE MUTATION: without the branch the core's promoted leader (leader-changed) or a disbanded party (group-changed) reports the removal.
        with self.assertRaises(AssertionError):
            check_commander_removal(self.cpp.replace(
                "        if ((m.flags & L::FlagInParty) && guid == commander)\n"
                "        {\n"
                "            DeferRelease(commander, L::End::CommanderLeftParty, Ctr(group->GetGUID()));\n"
                "            continue;\n"
                "        }\n", ""))
        # ... and releasing on the spot (the round's crash) no longer passes the pin either.
        with self.assertRaises(AssertionError):
            check_commander_removal(self.cpp.replace(
                "DeferRelease(commander, L::End::CommanderLeftParty, Ctr(group->GetGUID()))",
                "ReleaseCommander(commander, L::End::CommanderLeftParty)"))

    def test_a_group_hook_never_releases_inside_the_core(self):
        # The group hooks run while the core is still walking the group (Group::Disband deletes it): the release's BotsLeave
        # removes members, the group can disband and free itself, and the outer Group method keeps using it. Every hook only
        # records its event; the next tick releases it.
        check_group_hooks_defer(self.cpp, self.scripts)
        for old, new in (
            ("DeferRelease(commander, L::End::PlayerJoined, Ctr(group->GetGUID()))", "ReleaseCommander(commander, L::End::PlayerJoined)"),
            ("DeferRelease(commander, L::End::LeaderChanged, Ctr(group->GetGUID()))", "ReleaseCommander(commander, L::End::LeaderChanged)"),
            ("DeferRelease(commander, L::End::GroupChanged, Ctr(group->GetGUID()))", "ReleaseCommander(commander, L::End::GroupChanged)"),
            # THE MEDIUM: the binding's remembered counter (0 for a bind made before the 1 Hz refresh, stale after a switch) instead of the group
            # the hook was handed; the drain then releases a whole group on 0.
            ("DeferRelease(commander, L::End::GroupChanged, Ctr(group->GetGUID()))",
             "DeferRelease(commander, L::End::GroupChanged, m.groupId)"),
        ):
            with self.subTest(old=old):
                with self.assertRaises(AssertionError):
                    check_group_hooks_defer(self.cpp.replace(old, new))
        # A drain that falls back to the lead's current group when the counter names nothing: exactly the wrong party.
        with self.assertRaises(AssertionError):
            check_group_hooks_defer(self.cpp.replace(
                "Group* group = p.groupId ? sGroupMgr->GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(p.groupId)) : nullptr;",
                "Group* group = p.groupId ? sGroupMgr->GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(p.groupId)) : ReleaseGroup(p.commander, m);"))
        # The script's own overrides are the other place a release could run inside the core's group walk.
        with self.assertRaises(AssertionError):
            check_group_hooks_defer(self.cpp, self.scripts.replace(
                "TortoiseBots::cmd::NoteGroupRemove(group, guid);",
                "TortoiseBots::cmd::NoteGroupRemove(group, guid);\n        group->Disband();"))
        # No drain: the recorded events would never run.
        with self.assertRaises(AssertionError):
            check_group_hooks_defer(self.cpp.replace("    RunPendingReleases();\n", ""))
        # A stale event surviving an explicit rebind would release the binding the command just made.
        with self.assertRaises(AssertionError):
            check_group_hooks_defer(self.cpp.replace("    DropPending(commander->GetGUID());\n", ""))
        # A drain behind the 1 Hz gate delays every recorded release by up to a second and can lose one across a tick.
        with self.assertRaises(AssertionError):
            check_group_hooks_defer(self.cpp.replace("    RunPendingReleases();\n", "").replace(
                "    s.maintainAtMs = nowMs;\n", "    s.maintainAtMs = nowMs;\n    RunPendingReleases();\n"))

    def test_a_binding_with_a_recorded_release_commands_nothing(self):
        # THE MEDIUM: between a group hook and the tick that drains it, the binding's release reason is already true, so an order in that
        # window would reach a bot that left (or a party that changed). Both the resolved party and the per-bot check ask the pure gate,
        # and only bots still in the lead's live party are addressed.
        check_command_gate(self.cpp)
        for old, new in (
            ("HasPending(commander->GetGUID())", "false"),
            ("HasPending(commander)", "false"),
            ("group->IsMember(bot->GetGUID())", "true"),
            ("group && group->IsMember(bot)", "stand"),
            ("    DropPending(commander);\n", ""),
        ):
            with self.subTest(old=old):
                with self.assertRaises(AssertionError):
                    check_command_gate(self.cpp.replace(old, new, 1))

    def test_a_restored_in_party_commander_keeps_the_lead(self):
        check_restore_leadership(self.cpp)
        # THE MUTATION: the recreated party keeps the bot in charge and the next fact read strips the commander's lead.
        with self.assertRaises(AssertionError):
            check_restore_leadership(self.cpp.replace(
                "if (group->IsMember(commander) && !group->IsLeader(commander))\n                group->ChangeLeader(commander);", ""))

    def test_a_player_leading_his_own_party_gets_a_lead_from_it(self):
        check_lead_selection(self.cpp)
        # THE MUTATION: the pre-fix fallback (the first bot h3 happens to hold) returns a bot of another party.
        with self.assertRaises(AssertionError):
            check_lead_selection(self.cpp.replace("m.lead = PlayerGuid(L::PickLead(leadFacts));", "m.lead = bound.front();"))

    def test_the_dashboard_feeds_every_binding(self):
        self.assertIn("cmd::AppendBindings(bindings, shownRunLeaders);", self.dash)
        self.assertIn("cmd::FillRunOrders(bindings);", self.dash)
        self.assertIn("shownRunLeaders.push_back(uint32(rec->leader.GetCounter()));", self.dash)
        fill = function_body(self.cpp, r"void FillRunOrders\(std::vector<pure::CT::BindingInfo>& out\)\s*\{")
        self.assertIn("h.view.commanderGuid == binding.commanderGuid", fill)  # a run card's orders come from the card of the same commander

    def test_the_host_stand_ins_read_the_record(self):
        self.assertIn("cmd::IsCommandedBy(commander, bot)", self.ownership)
        self.assertIn("cmd::IsInOwnBindingGroup(commander, bot)", self.ownership)
        for word in ("PQuery", "CharacterDatabase", "WorldDatabase"):
            self.assertNotIn(word, self.ownership)

    def test_the_order_log_lives_in_the_executor(self):
        issue = function_body(self.orders, r"OrderAnswer Issue\(Party const& party, std::string const& order, uint32 nowMs, bool gather\)\s*\{")
        self.assertIn("cmd::NoteRunOrder(party.leader, order, answer.accepted, answer.code, answer.text);", issue)
        self.assertIn("IssueOrder(party, order, nowMs, gather)", issue)
        run_issue = function_body(self.orders, r"OrderAnswer Issue\(RunRec& rec, std::string const& order, uint32 nowMs, bool gather\)\s*\{")
        self.assertIn("Issue(party, order, nowMs, gather)", run_issue)
        self.assertNotIn("NoteRunOrder", code(text(HOST / "run" / "TbRun.cpp")))

    def test_the_command_line_and_the_off_build(self):
        self.assertIn("uint32 flags = 0;", re.search(r"struct Meta\s*\{.*?\};", self.cpp, re.S).group(0))
        self.assertIn("L::FlagInParty", self.cpp)
        for name in ("NoteGroupAdd", "NoteGroupRemove", "NoteGroupLeader", "NoteGroupDisband", "NoteDeath", "NoteLogin", "NoteLogout", "Maintain",
                     "LoadBindings", "IsCommandedBy", "IsInOwnBindingGroup", "FillRunOrders"):
            self.assertRegex(self.cpp.split("#else")[1], rf"(?:void|bool) {name}\(")


def check_player_led(source):
    """FlagPlayerLed is a live fact of an IN-PARTY binding, never a constant: the bind and the 1 Hz fact read take it from the group's leader, the
    leader hook keeps the binding when its in-party commander takes the lead and refreshes the flag from the leader the hook is about to store (the
    core fires it before it records the new leader), and a hand-over to anybody else still releases it. An outside-party binding is released as
    before."""
    bind = function_body(source, r"void NoteMaster\(std::vector<Player\*> const& bots, std::vector<L::Kind> const& kinds, Player\* leader, Player\* commander, uint32 bgInstance\)\s*\{")
    flags = re.search(r"L::FlagsOf\(([^;]*?)\);", bind)
    assert flags, "the bind's flags are gone"
    if "inParty && group->IsLeader(commander->GetGUID())" not in flags.group(1):
        raise AssertionError("the bind writes a constant player-led flag, not the group's leader")
    facts = function_body(source, r"L::Facts FactsFor\(ObjectGuid commander, Meta& m, Player\* commanderPlayer, Player\* lead\)\s*\{")
    if "facts.leaderGuid = group->GetLeaderGUID().GetCounter()" not in facts:
        raise AssertionError("the release facts do not name the lead's group leader")
    if "RefreshPlayerLed(m, group ? group->GetLeaderGUID().GetCounter() : 0, commander)" not in facts:
        raise AssertionError("the periodic fact read does not refresh the player-led flag from the group leader")
    refresh = function_body(source, r"bool RefreshPlayerLed\(Meta& m, uint64_t leaderGuid, ObjectGuid commander\)\s*\{")
    if "leaderGuid == commander.GetCounter()" not in refresh:
        raise AssertionError("the player-led flag is not read from the leader guid it is given")
    if "(m.flags & L::FlagInParty)" not in refresh:
        raise AssertionError("the player-led flag is set for a binding made outside the party")
    leader = function_body(source, r"void NoteGroupLeader\(Group\* group, ObjectGuid newLeader\)\s*\{")
    if "newLeader != m.lead && !commanderLeads" not in leader:
        raise AssertionError("a leader change to the binding's commander still releases it")
    if "(m.flags & L::FlagInParty) && newLeader == commander" not in leader:
        raise AssertionError("the leader hook keeps a binding made outside the party")
    if "RefreshPlayerLed(m, newLeader.GetCounter(), commander)" not in leader:
        raise AssertionError("the leader hook does not refresh the player-led flag from the leader it is about to store")
    # The hook fires BEFORE Group records the new leader (Group.cpp:721 vs :711): group->IsLeader() inside it answers the PREVIOUS leader, so
    # the flag must never be read from the group here (a hand-over to the lead would leave it stale for up to a second, and the release rule with it).
    if "group->IsLeader" in code(leader):
        raise AssertionError("the leader hook reads the group's leader, which still holds the previous leader inside the hook")


def check_commander_in_group(source):
    """The commander's membership is a group fact by guid: an offline or zoning commander who is still a member has not left the party, and his
    logout is the grace timer's business (an in-world Player pointer would release the binding on the next tick)."""
    facts = function_body(source, r"L::Facts FactsFor\(ObjectGuid commander, Meta& m, Player\* commanderPlayer, Player\* lead\)\s*\{")
    if "facts.commanderInGroup = group->IsMember(commander);" not in facts:
        raise AssertionError("the commander's membership is not read by guid from the group")
    membership = facts.split("facts.commanderInGroup = ", 1)[1].split(";", 1)[0]
    if "commanderPlayer" in membership:
        raise AssertionError("the commander's membership is read through an in-world player pointer")


def check_commander_removal(source):
    """The commander of an in-party binding leaving or being kicked ends that binding with commander-left-party, recorded from the removal hook
    and released on the next tick (never inside Group::RemoveMember, which goes on using - and can free - the group)."""
    remove = function_body(source, r"void NoteGroupRemove\(Group\* group, ObjectGuid guid\)\s*\{")
    if "(m.flags & L::FlagInParty) && guid == commander" not in remove:
        raise AssertionError("the commander of an in-party binding leaving the party is not noticed")
    if "DeferRelease(commander, L::End::CommanderLeftParty, Ctr(group->GetGUID()))" not in remove:
        raise AssertionError("the commander leaving an in-party binding is not recorded for a deferred release")
    if "ReleaseCommander(" in code(remove):
        raise AssertionError("the removal hook releases inside Group::RemoveMember (the group can be freed under the core)")


def check_group_hooks_defer(source, scripts=None):
    """No GroupScript hook may remove members or disband: the core is still walking the group (Group::Disband deletes it). Every hook records its
    event with DeferRelease and the very group the core handed it (a binding's remembered counter can be stale or 0, and a drain that looked
    it up afresh would reach into another party), and the tick's RunPendingReleases does the release from fresh guids."""
    hooks = {
        "NoteGroupAdd": r"void NoteGroupAdd\(Group\* group, ObjectGuid guid\)\s*\{",
        "NoteGroupRemove": r"void NoteGroupRemove\(Group\* group, ObjectGuid guid\)\s*\{",
        "NoteGroupLeader": r"void NoteGroupLeader\(Group\* group, ObjectGuid newLeader\)\s*\{",
        "NoteGroupDisband": r"void NoteGroupDisband\(Group\* group\)\s*\{",
    }
    for name, signature in hooks.items():
        body = code(function_body(source, signature))
        for call in ("ReleaseCommander(", "ApplyPartyAction(", "RemoveMember(", "ChangeLeader(", "Disband("):
            if call in body:
                raise AssertionError(f"{name} runs {call} inside a GroupScript hook")
        if re.search(r"\bRelease[A-Za-z_]*\s*\(", body):
            raise AssertionError(f"{name} calls a release helper inside a GroupScript hook")
        if "DeferRelease(" not in body:
            raise AssertionError(f"{name} neither releases nor records its event")
        recorded = re.findall(r"DeferRelease\([^;]*;", body)
        if not recorded or any("Ctr(group->GetGUID())" not in call for call in recorded):
            raise AssertionError(f"{name} does not record the group the hook saw")
        if re.search(r"DeferRelease\([^;]*m\.groupId", body):
            raise AssertionError(f"{name} records the binding's remembered group counter")
    pending = code(re.search(r"struct PendingRelease\s*\{.*?\};", source, re.S).group(0))
    for field in ("ObjectGuid commander", "L::End end", "uint32 groupId"):
        if field not in pending:
            raise AssertionError("a pending release does not keep the binding's guid, reason and group counter")
    if re.search(r"Group\s*\*", pending):
        raise AssertionError("a pending release holds a Group* across ticks (the core can free it)")
    drain = function_body(source, r"void RunPendingReleases\(\)\s*\{")
    if "s.metas.find(p.commander)" not in drain:
        raise AssertionError("a deferred release does not re-check that the binding still stands")
    if "sGroupMgr->GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(p.groupId))" not in drain:
        raise AssertionError("a deferred release does not re-find its group by counter")
    if "ReleaseGroup(p.commander" in code(drain):
        raise AssertionError("a deferred release falls back to the lead's current group (it can be another party by this tick)")
    if not re.search(r"p\.groupId \? sGroupMgr->GetGroupByGUID\([^;]*\) : nullptr;", code(drain)):
        raise AssertionError("a deferred release with no recorded counter still looks a group up")
    if "L::DeferredRelease(facts)" not in drain:
        raise AssertionError("a deferred release does not apply the pure deferral rule")
    maintain = function_body(source, r"void Maintain\(\)\s*\{")
    if "RunPendingReleases();" not in maintain:
        raise AssertionError("the tick never runs the releases its group hooks recorded")
    if maintain.index("RunPendingReleases();") > maintain.index("if (s.maintainAtMs"):
        raise AssertionError("the deferred releases wait for the 1 Hz gate instead of the next tick")
    bind = function_body(source, r"void NoteMaster\(std::vector<Player\*> const& bots, std::vector<L::Kind> const& kinds, Player\* leader, Player\* commander, uint32 bgInstance\)\s*\{")
    if "DropPending(commander->GetGUID());" not in bind:
        raise AssertionError("a rebind keeps a group event recorded for the binding it replaced")
    # The GroupScript class in TbScripts.cpp only forwards the events: a synchronous release or a group call added to the overrides would run
    # inside the core's group walk just like one in the hooks themselves.
    if scripts is not None:
        group_script = code(scripts.split("class tortoisebots_group")[1].split("class tortoisebots_commands")[0])
        for call in ("ReleaseCommander(", "ApplyPartyAction("):
            if call in group_script:
                raise AssertionError(f"the group script runs {call} inside the core's group hooks")
        if re.search(r"\w+->(?:RemoveMember|Disband|ChangeLeader|AddMember|SetLootMethod)\(", group_script):
            raise AssertionError("the group script calls a group mutator inside the core's group hooks")
        if re.search(r"\bRelease[A-Za-z_]*\s*\(", group_script):
            raise AssertionError("the group script calls a release helper inside the core's group hooks")
        if not re.search(r"NoteGroup(?:Add|Remove|Leader|Disband)\(", group_script):
            raise AssertionError("the group script no longer records the core's group events")


def check_command_gate(source):
    """A binding a group hook has recorded for release commands nothing: the hook fires before the core changes the group, so the tick
    that runs it is one event old and the change it saw is already true. The resolved party and the per-bot check both ask the pure gate,
    and a bot must still be a member of the lead's live party, so a follower that left is refused before the tick too."""
    pending = code(function_body(source, r"bool HasPending\(ObjectGuid commander\)\s*\{"))
    if "pending.commander == commander" not in pending:
        raise AssertionError("HasPending does not look the commander up among the recorded events")
    commanded = code(function_body(source, r"bool IsCommandedBy\(Player\* commander, Player\* bot\)\s*\{"))
    if "L::MayCommand(" not in commanded:
        raise AssertionError("IsCommandedBy does not apply the command gate")
    if "HasPending(commander->GetGUID())" not in commanded:
        raise AssertionError("IsCommandedBy ignores a release recorded by a group hook")
    if "group->IsMember(bot->GetGUID())" not in commanded:
        raise AssertionError("IsCommandedBy no longer checks the bot's live party membership")
    resolve = code(function_body(source, r"bool ResolveBinding\(ObjectGuid commander, ObjectGuid& lead, std::vector<ObjectGuid>& members\)\s*\{"))
    if "HasPending(commander)" not in resolve:
        raise AssertionError("ResolveBinding ignores a release recorded by a group hook")
    if "L::MayCommand(" not in resolve:
        raise AssertionError("ResolveBinding does not apply the command gate")
    if "group->IsMember(bot)" not in resolve:
        raise AssertionError("ResolveBinding returns bots that are no longer in the lead's party")
    release = code(function_body(
        source, r"void ReleaseCommander\(ObjectGuid commander, L::End end, Group\* partyGroup = nullptr, bool partyKnown = false\)\s*\{"))
    if "DropPending(commander);" not in release:
        raise AssertionError("a release leaves a group hook's event to fire against a later binding of the same commander")


def check_bg_command_gate(source):
    """A battleground bind authorises on its live facts, never the stored member alone (#315): h3 still binds the bot to this commander, both
    stand in the world, both stand in the instance the bind was made in, and both are still on one team. The 1 Hz check ends a stale row, but a
    command sent between two ticks must not use it: the commander's leave (or a transfer, or a logout) denies at once."""
    commanded = code(function_body(source, r"bool IsCommandedBy\(Player\* commander, Player\* bot\)\s*\{"))
    if "L::MayCommandBg(" not in commanded:
        raise AssertionError("IsCommandedBy does not apply the battleground command gate")
    for needle, why in (
            ("bot->IsInWorld()", "the bot's login is not read"),
            ("commander->IsInWorld()", "the commander's login is not read"),
            ("bot->GetBattlegroundId() == member->instance", "the bot's instance is not read"),
            ("commander->GetBattlegroundId() == member->instance", "the commander's instance is not read"),
            ("member->instance &&", "a member with no recorded instance would match an empty live one"),
            ("bot->GetBGTeam() == commander->GetBGTeam()", "the team is not read")):
        if needle not in commanded:
            raise AssertionError(f"the battleground command gate: {why}")


def check_restore_leadership(source):
    """An in-party binding whose commander led the party restores with him at its head: the recreated party is formed around the lead bot, and
    leaving the bot in charge would strip the commander on every restart."""
    restore = function_body(source, r"void RestoreOne\(ObjectGuid commander\)\s*\{")
    if "if (m.flags & L::FlagPlayerLed)" not in restore:
        raise AssertionError("a restored in-party binding does not put its commander back at the lead")
    if "group->ChangeLeader(commander);" not in restore:
        raise AssertionError("the restored party stays led by the lead bot")


def check_lead_selection(source):
    """The bind's lead is chosen from what the command named: a host bot of this commander, else (a player leading his own party names himself) a
    bot of that party with a tank preferred - never the arbitrary first bot h3 holds."""
    bind = function_body(source, r"void NoteMaster\(std::vector<Player\*> const& bots, std::vector<L::Kind> const& kinds, Player\* leader, Player\* commander, uint32 bgInstance\)\s*\{")
    if "leadFacts.namedLeaderIsBotOfCommander = leader && h3::CommanderGuid(leader->GetGUID()) == commander->GetGUID();" not in bind:
        raise AssertionError("the bind no longer tells a commander's own bot from a player it addressed")
    if "player->GetGroup() != leaderGroup" not in bind:
        raise AssertionError("the bind does not read the addressed player's party")
    if "m.lead = PlayerGuid(L::PickLead(leadFacts));" not in bind:
        raise AssertionError("the bind does not choose its lead (a player leading his own party would keep an arbitrary bot)")


class RowColumns(unittest.TestCase):
    def test_a_group_column_is_refused(self):
        source = text(CMD / "TbCmdBindingStore.cpp")
        check_row_columns(source)
        for mutation in (source.replace("`flags`, `bound_at` FROM", "`flags`, `bound_at`, `group_id` FROM"),
                         source.replace('kColumns = "`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`"',
                                        'kColumns = "`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`, `group_id`"')):
            with self.subTest(mutation=mutation[:60]):
                with self.assertRaises(AssertionError):
                    check_row_columns(mutation)


MUTATIONS = {
    "lead-offline": ("!commanderInWorld || !isOnline(stored.lead)", "!commanderInWorld"),
    # the pre-fix leader rule: only the lead bot's own lead counts, so a commander leading his own party is released at once
    "leader-any": ("if (!facts.leadIsLeader && !commanderLeads)", "if (!facts.leadIsLeader)"),
    # ...the same rule without the party gate: an outside-party binding survives its commander being made leader
    "leader-outside": ("bool const commanderLeads = HasFlag(stored, FlagInParty) && facts.leaderGuid == stored.commander;",
                       "bool const commanderLeads = facts.leaderGuid == stored.commander;"),
    # a named player's guid taken as the lead itself (the lead is never the commander)
    "lead-any-named": ("    if (facts.namedLeader && facts.namedLeaderIsBotOfCommander)\n        return facts.namedLeader;",
                       "    if (facts.namedLeader)\n        return facts.namedLeader;"),
    "dead-grace": ("if (!facts.leadAlive && facts.leadDeadMs >= facts.leadDeadGraceMs && facts.leadDeadGraceMs >= 0)",
                   "if (!facts.leadAlive)"),
    "away-grace": ("if (!facts.commanderOnline && facts.commanderGoneMs >= facts.commanderGraceMs && facts.commanderGraceMs >= 0)",
                   "if (!facts.commanderOnline)"),
    "commander-member": ("if (std::find(stored.members.begin(), stored.members.end(), stored.commander) != stored.members.end())",
                         "if (false)"),
    # the pre-fix command gate: a release recorded by a group hook is ignored, and a bot that left the party is still ordered
    "pending-commands": ("return bindingStands && !releasePending && boundToCommander && inLeadGroup;",
                         "return bindingStands && boundToCommander && inLeadGroup;"),
    "left-commands": ("return bindingStands && !releasePending && boundToCommander && inLeadGroup;",
                      "return bindingStands && !releasePending && boundToCommander;"),
    # a release with no recorded counter reaching into whatever group the lead or a member holds on the next tick
    "zero-group-releases": ("if (!facts.recordedGroup || !facts.recordedGroupExists)",
                            "if (facts.recordedGroup && !facts.recordedGroupExists)"),
    "members-overflow": ("if (c < '0' || c > '9' || guid > (UINT64_MAX - 9) / 10)",
                         "if (c < '0' || c > '9')"),
    "claimed-steal": ("bool const foreign = owner && ((owner->commander && owner->commander != commander) ||",
                      "bool const foreign = false && ((owner->commander && owner->commander != commander) ||"),
}

# The store check compiles TbCmdBindingStore.cpp itself, so these mutation steps edit that file; each list is applied in order.
STORE_MUTATIONS = {
    # the reader that advances before its first Fetch: the core's Query has already fetched row 1 (BasicStatementTask::Query), so this drops it
    "rows-skip-first": [
        ("    do\n    {\n        Field* fields = result->Fetch();", "    while (result->NextRow())\n    {\n        Field* fields = result->Fetch();"),
        ("    } while (result->NextRow());", "    }"),
    ],
    # a failed delete that is only logged: the durable row comes back through Rows() and the next start restores it
    "erase-forgotten": [
        ("        s.pendingErase.insert(commander);\n", ""),
    ],
    # a pending delete that is still offered to a restore
    "pending-restored": [
        ("        if (!s.pendingErase.count(row.commander))\n            rows.push_back(row);", "        rows.push_back(row);"),
    ],
    # the retry deletes the row a later bind wrote
    "retry-deletes-a-rebind": [
        ("    s.pendingErase.erase(stored.commander);     // an explicit write is newer than the delete it replaces\n", ""),
    ],
}


@unittest.skipUnless(os.environ.get("CMD_P2_MUTATE") == "1", "mutations compile the standalone check: run on the build VM with CMD_P2_MUTATE=1")
class Mutations(unittest.TestCase):
    def build(self, header_text):
        tmp = Path(tempfile.mkdtemp(prefix="cmdp2-"))
        (tmp / "forever" / "src" / "host" / "cmd").mkdir(parents=True)
        (tmp / "forever" / "tools").mkdir(parents=True)
        (tmp / "forever" / "src" / "host" / "cmd" / "TbCmdBindingLifePure.h").write_text(header_text)
        (tmp / "forever" / "tools" / "cmd_binding_life_check.cpp").write_text((TOOLS / "cmd_binding_life_check.cpp").read_text())
        exe = tmp / "check"
        subprocess.run(["g++", "-std=c++17", "-O0", "-I", str(tmp), "-I", str(ROOT), str(tmp / "forever" / "tools" / "cmd_binding_life_check.cpp"), "-o", str(exe)],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_unmutated_passes(self):
        out = self.build((CMD / "TbCmdBindingLifePure.h").read_text())
        self.assertEqual(out.returncode, 0, out.stdout)

    def test_each_mutation_fails_the_check(self):
        source = (CMD / "TbCmdBindingLifePure.h").read_text()
        for name, (old, new) in MUTATIONS.items():
            with self.subTest(mutation=name):
                self.assertIn(old, source, f"mutation {name} no longer applies")
                out = self.build(source.replace(old, new, 1))
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)

    def build_store(self, store_text):
        tmp = Path(tempfile.mkdtemp(prefix="cmdp2-store-"))
        for rel in ("forever/src/host/cmd/TbCmdBindingStore.h", "forever/src/host/cmd/TbCmdBindingLifePure.h",
                    "forever/src/compat/TbSqlNames.h", "forever/src/compat/TbCompat_CharStore.h", "runtime/RemoteCommanderPolicy.h",
                    "forever/tools/cmd_binding_store_check.cpp"):
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            (tmp / rel).write_text((ROOT / rel).read_text())
        fakes = tmp / "forever" / "tools" / "cmd_binding_store_fakes"
        fakes.mkdir(parents=True)
        for fake in (TOOLS / "cmd_binding_store_fakes").glob("*.h"):
            (fakes / fake.name).write_text(fake.read_text())
        (tmp / "forever" / "src" / "host" / "cmd" / "TbCmdBindingStore.cpp").write_text(store_text)
        exe = tmp / "check"
        subprocess.run(["g++", "-std=c++17", "-O0", "-I", str(fakes), "-I", str(tmp / "forever" / "src"), "-I", str(tmp),
                        str(tmp / "forever" / "tools" / "cmd_binding_store_check.cpp"),
                        str(tmp / "forever" / "src" / "host" / "cmd" / "TbCmdBindingStore.cpp"), "-o", str(exe)],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_unmutated_store_passes(self):
        out = self.build_store((CMD / "TbCmdBindingStore.cpp").read_text())
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_each_store_mutation_fails_the_check(self):
        source = (CMD / "TbCmdBindingStore.cpp").read_text()
        for name, edits in STORE_MUTATIONS.items():
            with self.subTest(mutation=name):
                mutated = source
                for old, new in edits:
                    self.assertIn(old, mutated, f"mutation {name} no longer applies")
                    mutated = mutated.replace(old, new, 1)
                out = self.build_store(mutated)
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)


if __name__ == "__main__":
    unittest.main()
