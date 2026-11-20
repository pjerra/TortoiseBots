#!/usr/bin/env python3
"""Wiring pins and mutations for the commander binding lifecycle and its persistence (package P2, forever/docs/CMD-P2-NOTES.md).

The core calls cannot run without a server, so these tests pin where the hooks stand and what the lifecycle must never do; every assertion fails on the
pre-P2 source (the hooks, the store, the row and the config key do not exist there). With CMD_P2_MUTATE=1 (run on the build VM: they compile
forever/tools/cmd_binding_life_check.cpp) the mutations break the pure half and the row checker, and each must FAIL:
  lead-offline  a restore whose lead is not in the world
  dead-grace    a dead lead released before its grace
  away-grace    an absent commander released before its grace
  group-column  a group column or id in the persisted row (the spec's failing case: the core reuses database group ids)
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
    insert = re.search(r"INSERT INTO `tb_commander_binding` \((.+?)\) VALUES", source)
    assert insert, "the stored row's insert is gone"
    written = re.findall(r"`([a-z_]+)`", insert.group(1))
    if written != ROW_COLUMNS:
        raise AssertionError(f"the stored row inserts {written}, not the five fields {ROW_COLUMNS}")
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
        release = function_body(self.cpp, r"void ReleaseCommander\(ObjectGuid commander, L::End end\)\s*\{")
        self.assertIn("h3::SetCommander(bot, ObjectGuid::Empty)", release)  # h3's table is the owner: a release clears its pairs
        self.assertIn("store::Erase(commander.GetCounter())", release)      # and the stored row
        self.assertIn("RestoreFollower(follower, m.lead)", release)         # the followers' own follow targets come back
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
        table = function_body(self.store, r"void EnsureTable\(State& s\)\s*\{")
        self.assertIn("CharacterDatabase.DirectExecute(", table)
        self.assertIn("information_schema.COLUMNS", table)
        # No query outside the guard/ensure: every CharacterDatabase.Query in the file is one of the two above.
        self.assertEqual(len(re.findall(r"CharacterDatabase\.Query\(", code(self.store))), 3)   # guard (info_schema + select) + ensure table
        self.assertEqual(len(re.findall(r"CharacterDatabase\.DirectExecute\(", code(self.store))), 1)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tb_commander_binding`", self.store)

    def test_the_restore_needs_the_commander_and_the_lead(self):
        restore = function_body(self.cpp, r"void RestoreOne\(ObjectGuid commander\)\s*\{")
        self.assertIn("if (!commanderPlayer)", restore)                      # no restore with the commander offline
        self.assertIn("L::PlanRestore(row, true, online)", restore)
        self.assertIn("if (!plan.ready)", restore)                           # PlanRestore refuses a lead that is not in the world
        self.assertIn("BotMgr::Instance().GroupWith(bot, m.lead)", restore)  # the party is formed again through the lead
        self.assertIn("BotManager::Instance().BindBotMaster(bot, commander)", restore)
        self.assertIn("h3::SetCommander(bot, commander)", restore)
        load = function_body(self.cpp, r"void LoadBindings\(\)\s*\{")
        self.assertIn("store::Rows()", load)
        self.assertIn("m.restored = false", load)                             # a row is not a standing binding until its party is back

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
        issue = function_body(self.orders, r"OrderAnswer Issue\(RunRec& rec, std::string const& order, uint32 nowMs, bool gather\)\s*\{")
        self.assertIn("cmd::NoteRunOrder(rec.leader, order, answer.accepted, answer.code, answer.text);", issue)
        self.assertIn("IssueOrder(rec, order, nowMs, gather)", issue)
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
                         source.replace("(`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`)",
                                        "(`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`, `group_id`)")):
            with self.subTest(mutation=mutation[:60]):
                with self.assertRaises(AssertionError):
                    check_row_columns(mutation)


MUTATIONS = {
    "lead-offline": ("!commanderInWorld || !isOnline(stored.lead)", "!commanderInWorld"),
    "dead-grace": ("if (!facts.leadAlive && facts.leadDeadMs >= facts.leadDeadGraceMs && facts.leadDeadGraceMs >= 0)",
                   "if (!facts.leadAlive)"),
    "away-grace": ("if (!facts.commanderOnline && facts.commanderGoneMs >= facts.commanderGraceMs && facts.commanderGraceMs >= 0)",
                   "if (!facts.commanderOnline)"),
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


if __name__ == "__main__":
    unittest.main()
