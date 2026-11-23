#!/usr/bin/env python3
"""Wiring pins and mutations for the `.tb strategy` additions of #133 round 3: `~name` (toggle), `reset`, and the
`role:<tank|healer|dps>` target, all in forever/src/host/TbCc.cpp (`h8::HandleStrategy`).

The core calls cannot run without a server, so these tests pin the order in which the handler reads its line, decides a
target and writes, and the reply ops it may answer; the engine itself is reached through `PlayerbotAI` (`HasStrategy`,
`ChangeStrategy`, `ResetStrategies`). Every assertion fails on the pre-round source (no `~`, no `reset`, no role target,
`~` refused). With CHOICE_CMD_MUTATE=1 (run on the build VM) each mutation below is applied to the real source and must
turn the matching check red:
  toggle-always-add       `~name` adds even when the state already holds the strategy
  role-ignored            the role target applies to every bot, not only the ones whose stored role is that word
"""
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
CC = ROOT / "forever" / "src" / "host" / "TbCc.cpp"
CC_H = ROOT / "forever" / "src" / "host" / "TbCc.h"
SCRIPTS = ROOT / "forever" / "src" / "TbScripts.cpp"
DOC = ROOT / "forever" / "docs" / "SLICE8-H-NOTES.md"

HANDLER_SIG = r"bool HandleStrategy\(ChatHandler\* handler, Trinity::ChatCommands::Tail tail\)\s*\{"
PARSE_SIG = r"bool ParseStrategyLine\(ChatHandler\* handler, Words const& args, size_t from, StrategyLine& line\)\s*\{"
APPLY_SIG = r"void ApplyStrategyLine\(Reply& reply, Player\* bot, StrategyLine const& line, uint32& applied, uint32& refused\)\s*\{"
ROLE_SIG = r"bool RoleTargetOf\(std::string const& word, uint8& role, bool& ok\)\s*\{"


def read(path):
    return Path(path).read_text()


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


# ---- the checks the mutations must break (plain functions so the mutate runner can call them on mutated text) ----

def check_toggle(cpp):
    body = function_body(cpp, APPLY_SIG)
    # `~` reads the state it finds and takes the other side: remove when held, add when not.
    assert "item.op == '~' ? (ai->HasStrategy(item.name, line.state) ? '-' : '+') : item.op" in body, \
        "the toggle must read the state before it chooses add or remove"
    assert 'S("op", item.op == \'~\' ? "toggle"' in body, "a toggle is answered op=toggle"
    assert "ai->ChangeStrategy(" in body, "the change goes through ChangeStrategy"
    assert body.index("ai->ChangeStrategy(") < body.index("bool const has = ai->HasStrategy(item.name, line.state);"), \
        "the change is read back afterwards"
    parse = function_body(cpp, PARSE_SIG)
    assert "c == '~'" in parse, "`~` is a legal change word"
    assert "piece[0] != '~'" in parse, "a change may start with ~"
    assert "a +, - or ~ without a strategy name" in parse, "a bare ~ is refused"
    assert '"unknown-strategy"' in cpp, "every name is still checked against the bot's context"
    assert "sPlayerbotDbStore.Save" in cpp and "bExplicitDbStoreSave" in cpp, "changes are saved like TB (#135), unless ExplicitDbStoreSave"


def check_reset(cpp):
    body = function_body(cpp, HANDLER_SIG)
    parse = function_body(cpp, PARSE_SIG)
    assert 'head == "reset"' in parse, "the line recognises the word reset"
    assert 'S("op", "reset")' in body, "a reset is answered op=reset"
    assert "ai->ResetStrategies()" in body, "reset is the engine's own rebuild (PlayerbotAI::ResetStrategies)"
    assert body.index("ResetStrategies()") < body.index('S("op", "reset")'), \
        "the rebuild happens before its ok record"
    assert '"reset takes one bot' in body, "the role target refuses reset (it takes one bot)"
    assert "sPlayerbotDbStore.Save" in cpp and "bExplicitDbStoreSave" in cpp, "changes are saved like TB (#135), unless ExplicitDbStoreSave"


def check_role(cpp):
    body = function_body(cpp, HANDLER_SIG)
    assert body.index("roleScope") < body.index("ResolveBot("), "the role target is read before a bot name is resolved"
    role = body[body.index("roleScope"):body.index("bool failed = false;")]
    assert "SenderPlayer(handler)" in role, "the group is the sender's"
    assert "GetMemberSlots()" in role, "the role target walks the sender's group"
    assert "IsHostBot(member)" in role, "only host bots are targets"
    assert "GetPlayerForcedRole(member->GetGUID()) != role" in role, "only bots stored with that role are targets"
    assert "continue;" in role, "a bot of another role is skipped"
    assert "std::sort(bots.begin(), bots.end()" in role, "the targets are ordered so the cap and the records are stable"
    assert "FirstUnknownStrategy(" in role, "every target's names are checked"
    assert role.index("FirstUnknownStrategy(") < role.index("ApplyStrategyLine("), \
        "no bot changes before every target is validated"
    assert 'P("target", { "role", RoleWord(role) })' in role, "the end record names the role target as role:<word>"
    assert 'N("bots", static_cast<int64>(bots.size()))' in role, "the end record counts the bots"
    assert 'N("applied", applied)' in role and 'N("refused", refused)' in role, "the end record counts the changes"


def check_role_words(cpp):
    body = function_body(cpp, ROLE_SIG)
    for word in ("tank", "healer", "dps"):
        assert f'name == "{word}"' in body, f"the role word {word} is not accepted"
    assert 'word.rfind("role:", 0) != 0' in body, "the role target is the role:<word> prefix"


def check_parse(cpp):
    parse = function_body(cpp, PARSE_SIG)
    assert "size_t start = 0;" in parse, "the change list is walked from byte 0 of the rejoined rest, not from a word index"
    body = function_body(cpp, HANDLER_SIG)
    assert "RoleTargetOf(Lower(args.w[0])" in body, "the role target is lowered like every .tb word"


def check_comment(hdr):
    assert "role:<tank|healer|dps>" in hdr, "TbCc.h does not name the role target"
    assert "~name" in hdr, "TbCc.h does not name the toggle"
    assert "reset" in hdr, "TbCc.h does not name reset"


def check_doc(doc):
    section = doc[doc.index("### `.tb strategy"):]
    assert "op=toggle" in section, "the notes do not show op=toggle"
    assert "op=reset" in section, "the notes do not show op=reset"
    assert "role:<tank|healer|dps>" in section, "the notes do not name the role target"
    assert "ResetStrategies" in section, "the notes do not name the rebuild reset runs"
    assert "(toggle) is refused" not in section, "the notes still refuse ~"


def check_registered(scripts):
    m = re.search(r'\{\s*"strategy",\s*TortoiseBots::h8::HandleStrategy,', scripts)
    assert m, "TbScripts.cpp no longer registers .tb strategy"


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = read(CC)
        self.hdr = read(CC_H)
        self.doc = read(DOC)
        self.scripts = read(SCRIPTS)

    def test_toggle_reads_the_state(self):
        check_toggle(self.cpp)

    def test_reset_is_the_engine_rebuild(self):
        check_reset(self.cpp)

    def test_role_target_filters_by_stored_role(self):
        check_role(self.cpp)

    def test_role_words(self):
        check_role_words(self.cpp)

    def test_the_line_is_parsed_from_its_own_words(self):
        check_parse(self.cpp)

    def test_the_header_names_the_new_forms(self):
        check_comment(self.hdr)

    def test_the_notes_show_the_new_ops(self):
        check_doc(self.doc)

    def test_the_verb_is_registered(self):
        check_registered(self.scripts)


# ---- mutations (run on the build VM: CHOICE_CMD_MUTATE=1, or TB_STRATEGY_MUTATE=1) ----

MUTANTS = [
    # name, original, mutant, check
    ("toggle-always-add",
     "char const op = item.op == '~' ? (ai->HasStrategy(item.name, line.state) ? '-' : '+') : item.op;",
     "char const op = item.op == '~' ? '+' : item.op;",
     check_toggle),
    ("role-ignored",
     "if (PlayerbotAIStorage::Instance().GetPlayerForcedRole(member->GetGUID()) != role)\n                continue;",
     "if (false)\n                continue;",
     check_role),
]


def run_mutations():
    cpp = read(CC)
    for name, _, _, check in MUTANTS:
        try:
            check(cpp)
        except AssertionError as error:
            print(f"ERROR: the unmutated source fails {name}'s check: {error}", file=sys.stderr)
            return 1
    survivors = []
    for name, original, mutant, check in MUTANTS:
        if cpp.count(original) != 1:
            print(f"ERROR: {name}: anchor found {cpp.count(original)} times, expected 1", file=sys.stderr)
            return 1
        try:
            check(cpp.replace(original, mutant, 1))
        except AssertionError:
            print(f"MUTATION CAUGHT: {name}")
            continue
        survivors.append(name)
    if survivors:
        print(f"ERROR: mutations survived: {survivors}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if os.environ.get("CHOICE_CMD_MUTATE") == "1" or os.environ.get("TB_STRATEGY_MUTATE") == "1":
        sys.exit(run_mutations())
    unittest.main()
