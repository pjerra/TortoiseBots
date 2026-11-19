#!/usr/bin/env python3
"""Wiring pins and mutations for the commander binding card (package P1, forever/docs/CMD-P1-NOTES.md).

The core calls cannot run without a server, so the wiring tests pin where the hooks stand and what the card code must never do; each assertion fails on the
source before P1. The mutation tests (CMD_P1_MUTATE=1, they compile forever/tools/cmd_binding_check.cpp, so run them on the build VM) break the pure half
three ways and expect the standalone check to FAIL each time:
  key       cards keyed by group instead of commander (D1: two commanders in one group become one card)
  skiprun   the run's binding is skipped (the card of a run loses `run` and `system`)
  twice     the run's own pairs are kept (the failing case of spec section 7: a run's binding shows twice)
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"


def text(rel):
    return (HOST / rel).read_text()


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


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = text("cmd/TbCmdBinding.cpp")
        self.hdr = text("cmd/TbCmdBinding.h")
        self.pure = text("cmd/TbCmdBindingPure.h")
        self.orders = text("TbOrders.cpp")
        self.run = text("run/TbRun.cpp")
        self.scripts = (ROOT / "forever" / "src" / "TbScripts.cpp").read_text()

    def test_h3_table_is_the_only_owner(self):
        read = function_body(self.cpp, r"std::vector<HostCard> Read\(int64 nowUnix, uint32 nowMs\)\s*\{")
        self.assertIn("h3::CommanderBindings()", read)           # the pairs come from h3's table, valid ones only
        self.assertIn("h3::CommanderGuid(rec->leader)", read)    # the run's leader keeps its own commander
        self.assertNotIn("SetCommander", code(self.cpp))         # the card code never writes a binding
        self.assertNotIn("BindBotMaster", code(self.cpp))

    def test_no_group_number_is_stored_and_no_database_call(self):
        self.assertNotIn("GetDbStoreId", code(self.cpp + self.pure + self.hdr))      # D2: the DB store id is reused
        for word in ("CharacterDatabase", "WorldDatabase", "PQuery", "PExecute", "Execute("):
            self.assertNotIn(word, code(self.cpp))
        # the group key is the session counter and exists only as a value of a card, never as a map key of the module's state
        self.assertIn("GetGUID().GetCounter()", function_body(self.cpp, r"uint32 GroupKey\(Group const\* group\)\s*\{"))
        meta = code(re.search(r"struct Meta\s*\{.*?\};", self.cpp, re.S).group(0))
        self.assertNotIn("group", meta.lower().replace("guid", ""))

    def test_cards_are_keyed_by_commander(self):
        self.assertIn("std::map<uint32_t, std::vector<uint32_t>> byCommander;", self.pure)
        self.assertIn("byCommander[p.commander]", self.pure)
        self.assertNotIn("groupLeader[", self.pure.split("inline std::vector<Card> BuildCards")[1].split("// ---- the order log")[0].replace("groupLeader.find", ""))

    def test_hooks_stand_where_the_answer_is_known(self):
        master = function_body(self.orders, r"bool HandleMaster\(ChatHandler\* handler, Trinity::ChatCommands::Tail tail\)\s*\{")
        self.assertLess(master.index("SetCommander(guid, commander->GetGUID())"), master.index("cmd::NoteMaster(who.bots, who.leader, commander)"))
        self.assertLess(master.index("cmd::NoteMaster("), master.index('Rec end("master")'))
        order = function_body(self.orders, r"bool HandleOrder\(ChatHandler\* handler, Trinity::ChatCommands::Tail tail\)\s*\{")
        self.assertIn("cmd::NoteOrder(who.bots, verb, rest, queued, refused, firstRefusal)", order)
        self.assertLess(order.index("cmd::NoteOrder("), order.index('Rec end("order")'))
        self.assertIn("firstRefusal = code", order)
        tick = self.run.split("OrderAnswer const answer = Issue(")[1][:400]
        self.assertTrue(tick.lstrip().startswith("rec, order") or "cmd::NoteRunOrder(rec.leader, order, answer.accepted, answer.code, answer.text);" in tick)
        self.assertRegex(self.run, r"OrderAnswer const answer = Issue\([^\n]*\n\s*cmd::NoteRunOrder\(rec\.leader, order, answer\.accepted, answer\.code, answer\.text\);")

    def test_the_command_is_registered_and_the_off_build_answers(self):
        self.assertIn('{ "commander", TortoiseBots::cmd::HandleCommander', self.scripts)
        self.assertIn('#include "host/cmd/TbCmdBinding.h"', self.scripts)
        off = self.cpp.split("#else   // not built with the slice")[1]
        self.assertIn("engine-not-built cmd=commander", off)
        self.assertIn("void NoteRunOrder(", off)

    def test_the_header_names_no_legacy_type(self):
        for word in ("PlayerbotAI", "AiObjectContext", "Engine", "RunRec"):
            self.assertNotIn(word, code(self.hdr))
            self.assertNotIn(word, code(self.pure))

    def test_status_reply_is_the_grammar_v2_with_the_run_record(self):
        body = function_body(self.cpp, r"bool HandleCommander\(ChatHandler\* handler, Trinity::ChatCommands::Tail tail\)\s*\{")
        for kind in ('Rec b("binding")', 'Rec r("member")', 'Rec r("order")', "run::RunRecord(*h.run, nowMs)"):
            self.assertIn(kind, body)
        self.assertIn('Reply reply(handler, args, "commander")', body)
        self.assertIn("RequireBuilt(handler, args, \"commander\")", body)


MUTATIONS = {
    "key": ("AddUnique(byCommander[p.commander], p.bot);",
            "AddUnique(byCommander[groupLeader.count(p.bot) ? groupLeader.at(p.bot) : p.commander], p.bot);"),
    "skiprun": ("        if (!r.leader)\n            continue;", "        if (true)\n            continue;"),
    "twice": ("        byCommander.erase(r.leader);", "        (void)r;"),
}


@unittest.skipUnless(os.environ.get("CMD_P1_MUTATE") == "1", "mutations compile the standalone check: run on the build VM with CMD_P1_MUTATE=1")
class Mutations(unittest.TestCase):
    def build(self, header_text):
        tmp = Path(tempfile.mkdtemp(prefix="cmdp1-"))
        (tmp / "forever" / "src" / "host" / "cmd").mkdir(parents=True)
        (tmp / "forever" / "tools").mkdir(parents=True)
        (tmp / "forever" / "src" / "host" / "cmd" / "TbCmdBindingPure.h").write_text(header_text)
        (tmp / "forever" / "tools" / "cmd_binding_check.cpp").write_text((ROOT / "forever" / "tools" / "cmd_binding_check.cpp").read_text())
        exe = tmp / "check"
        subprocess.run(["g++", "-std=c++17", "-O0", "-I", str(tmp), "-I", str(ROOT), str(tmp / "forever" / "tools" / "cmd_binding_check.cpp"), "-o", str(exe)],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_unmutated_passes(self):
        out = self.build((HOST / "cmd" / "TbCmdBindingPure.h").read_text())
        self.assertEqual(out.returncode, 0, out.stdout)

    def test_each_mutation_fails_the_check(self):
        source = (HOST / "cmd" / "TbCmdBindingPure.h").read_text()
        for name, (old, new) in MUTATIONS.items():
            with self.subTest(mutation=name):
                self.assertIn(old, source, f"mutation {name} no longer applies")
                out = self.build(source.replace(old, new, 1))
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)


if __name__ == "__main__":
    unittest.main()
