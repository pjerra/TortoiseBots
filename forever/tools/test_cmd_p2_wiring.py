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
                     "group->IsMember(commanderPlayer->GetGUID())"):
            self.assertIn(call, facts)                                     # the facts are read from the lead's live group, no stored number
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
        release = function_body(self.cpp, r"void ReleaseCommander\(ObjectGuid commander, L::End end\)\s*\{")
        self.assertIn("h3::SetCommander(bot, ObjectGuid::Empty)", release)  # h3's table is the owner: a release clears its pairs
        self.assertIn("bool const erased = store::Erase(commander.GetCounter())", release)  # and the stored row, read for its verdict
        self.assertIn("store::PendingErase(commander.GetCounter())", release)
        self.assertIn("the release is not complete", release)               # a durable row is never reported as a completed release
        self.assertIn("RestoreFollower(follower, m.lead)", release)         # the followers' own follow targets come back
        self.assertIn("ClearBotMaster(bot)", release)                       # a released bot keeps its former commander as neither master nor...
        self.assertIn("s.log.Retain(", release)                             # ...and the released card's order log goes with it at once
        self.assertIn("RC::DecideReleaseParty", function_body(self.cpp, r"void ApplyPartyAction\(ObjectGuid commander, Meta const& m, L::End end\)\s*\{"))
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
        # The ForeverCore schema guard: information_schema before any SELECT, and the CREATE on a synchronous connection before the check.
        guard = function_body(self.store, r"QueryResult CharacterQueryCompat\(char const\* sql, bool\* schemaMissing = nullptr\)\s*\{")
        self.assertIn("information_schema.COLUMNS", guard)
        self.assertIn("WorldQueryNamesCompat", guard)                       # the same single-table parser as the world guard
        self.assertIn("CharacterDatabase.Query(text.c_str())", guard)
        table = function_body(self.store, r"bool CheckSchema\(State& s, bool create\)\s*\{")
        self.assertIn("CharacterDatabase.DirectExecute(kCreate)", table)   # the CREATE only, once per config
        self.assertIn("information_schema.COLUMNS", table)
        self.assertIn("if (create && !s.tableTried)", table)               # DDL is not retried on every write
        # No query outside the guards: every CharacterDatabase.Query in the file is the column check or the guard (info_schema + select).
        self.assertEqual(len(re.findall(r"CharacterDatabase\.Query\(", code(self.store))), 3)   # column check + guard (info_schema + select)
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
        self.assertIn("DirectExecute(sql.c_str())", save)
        self.assertIn("ReadRows()", save)                                   # the in-memory view changes only after the read-back
        erase = function_body(self.store, r"bool Erase\(uint64_t commander\)\s*\{")
        self.assertIn("Writable(s)", erase)
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
        self.assertIn("h3::CommanderGuid(bot->GetGUID()) != commander->GetGUID()", commanded)

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
    "dead-grace": ("if (!facts.leadAlive && facts.leadDeadMs >= facts.leadDeadGraceMs && facts.leadDeadGraceMs >= 0)",
                   "if (!facts.leadAlive)"),
    "away-grace": ("if (!facts.commanderOnline && facts.commanderGoneMs >= facts.commanderGraceMs && facts.commanderGraceMs >= 0)",
                   "if (!facts.commanderOnline)"),
    "commander-member": ("if (std::find(stored.members.begin(), stored.members.end(), stored.commander) != stored.members.end())",
                         "if (false)"),
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
                    "forever/src/compat/TbSqlNames.h", "runtime/RemoteCommanderPolicy.h",
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
