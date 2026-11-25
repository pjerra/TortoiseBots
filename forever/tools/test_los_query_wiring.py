#!/usr/bin/env python3
"""Wiring pins and mutations for `.tb los` (#308), the server line-of-sight query.

The core calls cannot run without a server, so the pure rules (argument forms, refusal codes, the rate bucket, the access
rule) are proven by forever/tools/test_los_query.cpp, compiled and run from here; these pins prove the handler stands
where it must and calls what it must: the same visibility call the bots' attack-target check makes
(`RtiTargetValue.h:97 bot->IsWithinLOSInMap(unit)`, the value behind "I dont see my rti attack target" at
`AttackAction.cpp:163`), with LINEOFSIGHT_ALL_CHECKS; the shared `.tb run` access pair; one line, keys in order.

With LOSQUERY_MUTATE=1 (run on the build VM) each mutation below is applied to the real sources and must turn the
matching check red:
  slower-refill       the rate refills one token per 200 ms (5 a second)
  bigger-burst        the bucket holds five tokens, no more
  rate-wall           the bucket refuses when empty
  access-open         a bot outside the caller's run or party is refused
  probing-allowed     another player's character is never read
  map-guessed         two positions without `map=` are refused, never guessed on map 0
  flag-ignored        the sight call's dynamic line-of-sight flag (`true`, LINEOFSIGHT_ALL_CHECKS) is kept
  other-function      the unit-unit call is the bots' `IsWithinLOSInMap`, not a point-to-point call
  order-swapped       the line's keys stay in their published order
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"
PURE = HOST / "TbLosQueryPure.h"
HDR = HOST / "TbLosQuery.h"
CPP = HOST / "TbLosQuery.cpp"
SCRIPTS = ROOT / "forever" / "src" / "TbScripts.cpp"
UNIT = ROOT / "forever" / "tools" / "test_los_query.cpp"
RtiTarget = ROOT / "ai" / "playerbot" / "strategy" / "values" / "RtiTargetValue.h"
ATTACK = ROOT / "ai" / "playerbot" / "strategy" / "actions" / "AttackAction.cpp"
COMPAT = ROOT / "forever" / "src" / "compat" / "TbCompat_Unit.h"
TB_DOC = ROOT / "forever" / "docs" / "TB-COMMANDS.md"
PLAY_DOC = ROOT / "docs" / "play" / "commands" / "los-query.md"
PLAY_INDEX = ROOT / "docs" / "play" / "commands" / "README.md"


def read(path):
    return pathlib.Path(path).read_text(encoding="utf-8")


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


# ---- the checks (plain functions so the mutate runner can call them on mutated text) ----

SEND_SIG = r"void SendAnswer\(ChatHandler\* handler, Words const& args, Resolved const& a, Resolved const& b, Reason reason\)\s*\{"


def check_registration(scripts):
    assert '#include "host/TbLosQuery.h"' in scripts, "TbScripts.cpp does not include the handler"
    row = '{ "los",    TortoiseBots::losq::HandleLos,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes }'
    assert row in scripts, "the `.tb los` row is missing from the command table"


def check_off_build(cpp):
    assert 'bool HandleLos(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return h3::NotBuilt(handler, args, "los"); }' in cpp, \
        "the option-OFF build must answer engine-not-built"


def check_sight_call(cpp):
    body = function_body(cpp, SEND_SIG)
    # The bots' own call, through the shared compat pair (LINEOFSIGHT_ALL_CHECKS, no ignored models).
    assert "IsWithinLOSInMapCompat(a.unit, b.unit, true)" in body, \
        "the unit-unit answer must be IsWithinLOSInMapCompat(a, b, true), the bots' own check"
    assert "IsWithinLOSCompat(a.unit, b.side->pos.x, b.side->pos.y, b.side->pos.z, true)" in body, \
        "a unit-to-position answer must be IsWithinLOSCompat(unit, x, y, z, true)"
    assert "IsWithinLOSCompat(b.unit, a.side->pos.x, a.side->pos.y, a.side->pos.z, true)" in body, \
        "a position-to-unit answer must be IsWithinLOSCompat(unit, x, y, z, true)"
    assert "IsInLineOfSightCompat(map, a.side->pos.x, a.side->pos.y, a.side->pos.z," in body, \
        "a position-position answer must be IsInLineOfSightCompat(map, ...)"
    assert "IsWithinLOSInMapCompat(unit, b.unit" not in body, "the other side is not the viewer"


def check_same_check_as_bots(cpp, rti, attack, compat):
    # What makes the query's answer the bots' answer: the rti value behind "I dont see my rti attack target" filters on
    # the core's IsWithinLOSInMap with the default checks, and the compat pair passes the same LINEOFSIGHT_ALL_CHECKS.
    assert "if (!bot->IsWithinLOSInMap(unit))" in rti, "RtiTargetValue.h no longer filters on bot->IsWithinLOSInMap(unit)"
    assert '"I dont see my rti attack target"' in attack, "AttackAction.cpp no longer answers that line"
    assert "from->IsWithinLOSInMap(to, checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP)" in compat, \
        "TbCompat_Unit.h no longer maps checkDynLos=true to LINEOFSIGHT_ALL_CHECKS"
    assert "IsWithinLOSInMapCompat(a.unit, b.unit, true)" in cpp


def check_access(cpp):
    assert "CheckRunObserverAccess(handler, player, code, detail)" in cpp, "the shared run-observer check is missing"
    assert "CheckPartyLeadAccess(handler, player, code, detail)" in cpp, "the shared party-lead check is missing"
    assert "IsHostBot(player)" in cpp, "the handler must tell a host bot from a real player's character"
    assert "AccessAllowed(isGm, { a.access, b.access })" in cpp, "the access decision must be the pure rule"


def check_rate(cpp):
    assert "Buckets()[caller].Take(now)" in cpp, "the caller's bucket must gate the query"
    assert "it->second.Idle(now)" in cpp, "an idle caller's bucket must be dropped"
    assert "IsHostBot(player)" in cpp


def check_instance_order(cpp):
    # A resolved unit carries its own instance; a position inherits its unit side's map and instance; a position-position
    # query takes the loaded copy's (FindLoadedMap is the first instance). The answer and the refusal both ask SamePlace.
    assert "out.state.instanceId = unit->GetInstanceId();" in cpp, "a resolved unit must carry its instance id"
    assert "a.state.instanceId = b.state.instanceId;" in cpp, "a position must inherit its unit side's instance"
    assert "b.state.instanceId = a.state.instanceId;" in cpp, "a position must inherit its unit side's instance"
    assert "return sMapMgr.FindMap(mapId, FirstInstanceIdCompat(mapId));" in cpp, \
        "a position-position query is answered on the map's first loaded instance"
    assert "side.state.instanceId = map->GetInstanceId();" in cpp, "a position takes the loaded copy's instance"
    assert 'rec.N("same_map", SamePlace(a.state, b.state) ? 1 : 0);' in cpp, \
        "same_map must be SamePlace, not the map id alone"


def check_position_path(cpp):
    # The position-position distance is horizontal only, so a height difference never enters it.
    assert "distance = FormatDist(HorizontalDistance(a.side->pos, b.side->pos));" in cpp, \
        "the position-position distance must be the shared horizontal distance"


def check_gm_skips_access(cpp):
    # A GM passes AccessAllowed anyway, and the observer check logs INFO for a GM: a GM caller must not run FillAccess.
    assert "if (!isGm)\n        {\n            FillAccess(a, handler, sender);\n            FillAccess(b, handler, sender);\n        }" in cpp, \
        "a GameMaster caller must skip the observer checks (and their INFO log line)"


def check_no_oracle(cpp):
    # A caller below GameMaster must not tell a foreign character from a name that resolved to nothing, and a no-access
    # answer must carry no resolved name, map, same_map or in_world.
    assert "reason = SeenByCaller(isGm, Decide(facts));" in cpp, "the answer must pass the caller-facing rule"
    body = function_body(cpp, SEND_SIG)
    assert "bool const hideWorld = AnswerHidesWorld(reason);" in body, "a no-access answer must hide the world fields"
    assert "rec.S(key, hideWorld ? side.typed : side.display);" in body, "a hidden answer shows the typed words only"
    for key in ('if (a.state.mapKnown && !hideWorld)', 'if (a.state.mapKnown && b.state.mapKnown && !hideWorld)',
                'if (a.state.found && b.state.found && !hideWorld)'):
        assert key in body, f"a no-access answer must hide the world fields: missing {key}"


def check_one_line(cpp):
    assert 'Reply(handler, args, "los").Single(rec)' in cpp, "the answer is one line: Reply(..).Single(rec)"
    body = function_body(cpp, SEND_SIG)
    keys = ['value("a", a)', 'value("b", b)', 'rec.N("map"', 'rec.N("los"', 'rec.S("dist"',
            'rec.N("same_map"', 'rec.N("in_world"', 'rec.S("reason"']
    positions = [body.find(key) for key in keys]
    assert all(p >= 0 for p in positions), f"a published key is missing from the record: {list(zip(keys, positions))}"
    assert positions == sorted(positions), f"the published key order changed: {list(zip(keys, positions))}"


def check_docs(tb_doc, play_doc, play_index):
    assert ".tb los" in tb_doc, "forever/docs/TB-COMMANDS.md does not document .tb los"
    for token in ("los a=", "los=<0|1>", "dist=", "same_map=", "in_world=", "reason=",
                  "unknown-unit", "not-in-world", "different-maps", "bad-argument", "no-access", "rate"):
        assert token in tb_doc, f"TB-COMMANDS.md is missing {token}"
    assert play_doc.is_file(), "docs/play/commands/los-query.md is missing"
    text = read(play_doc)
    for token in (".tb los", "los a=", "reason=", "stable"):
        assert token in text, f"los-query.md is missing {token}"
    assert ".tb los" in play_index, "the commands index does not name .tb los"


# ---- the unit test, compiled against the real or a mutated pure header ----

def run_unit(pure_text):
    with tempfile.TemporaryDirectory() as tmp:
        holder = pathlib.Path(tmp) / "host"
        holder.mkdir(parents=True)
        (holder / "TbLosQueryPure.h").write_text(pure_text)
        exe = pathlib.Path(tmp) / "test_los_query"
        built = subprocess.run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-I", tmp, "-I", str(ROOT / "forever" / "src"),
                                str(UNIT), "-o", str(exe)], capture_output=True, text=True)
        if built.returncode != 0:
            return False, built.stderr
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        return ran.returncode == 0, ran.stdout + ran.stderr


class Wiring(unittest.TestCase):
    def test_the_command_is_registered(self):
        check_registration(read(SCRIPTS))

    def test_the_header_names_the_handler(self):
        assert "bool HandleLos(ChatHandler* handler, Trinity::ChatCommands::Tail args);" in read(HDR)

    def test_the_option_off_build_answers_not_built(self):
        check_off_build(read(CPP))

    def test_the_answer_is_one_line_in_published_order(self):
        check_one_line(read(CPP))

    def test_a_refusal_is_no_oracle(self):
        check_no_oracle(read(CPP))

    def test_the_sight_call_is_the_bots_call(self):
        check_sight_call(read(CPP))
        check_same_check_as_bots(read(CPP), read(RtiTarget), read(ATTACK), read(COMPAT))

    def test_the_access_checks_are_the_run_checks(self):
        check_access(read(CPP))

    def test_the_caller_bucket_gates_the_query(self):
        check_rate(read(CPP))

    def test_the_instance_is_part_of_the_place(self):
        check_instance_order(read(CPP))

    def test_the_position_distance_is_horizontal(self):
        check_position_path(read(CPP))

    def test_a_gm_skips_the_observer_checks(self):
        check_gm_skips_access(read(CPP))

    def test_the_docs(self):
        check_docs(read(TB_DOC), PLAY_DOC, read(PLAY_INDEX))

    def test_the_policy_rules_hold(self):
        passed, output = run_unit(read(PURE))
        self.assertTrue(passed, msg=output)


PURE_MUTANTS = [
    # name, original, mutant, the check it must break (run_unit on the mutated header)
    ("slower-refill", "    static constexpr std::uint32_t kRefillMs = 200;",
     "    static constexpr std::uint32_t kRefillMs = 100;", "rate"),
    ("bigger-burst", "    static constexpr std::uint32_t kCapacity = 5;",
     "    static constexpr std::uint32_t kCapacity = 6;", "rate"),
    ("rate-wall", "    if (_tokens == 0)\n        return false;", "    if (_tokens == 1)\n        return false;", "rate"),
    ("access-open", "        if (!side.isHostBot || !side.botAccessAllowed)\n            return false;",
     "        if (false)\n            return false;", "access"),
    ("probing-allowed", "        if (!side.isHostBot || !side.botAccessAllowed)\n            return false;",
     "        if (!side.botAccessAllowed)\n            return false;", "access"),
    ("map-guessed", "    if (bothPositions && words.size() != 3)",
     "    if (false)", "map"),
    ("oracle-reopened", "    return !senderIsGm && reason == Reason::UnknownUnit ? Reason::NoAccess : reason;",
     "    return reason;", "oracle"),
    ("answer-leaks", "inline bool AnswerHidesWorld(Reason reason) { return reason == Reason::NoAccess; }",
     "inline bool AnswerHidesWorld(Reason reason) { return false; }", "oracle"),
    # The instance is part of the place: two copies of one dungeon differ, and a same-id instance on another map still is
    # another map; a position's distance never adds its height difference.
    ("instance-ignored", "    return !a.instanceKnown || !b.instanceKnown || a.instanceId == b.instanceId;",
     "    return true;", "instance"),
    ("map-id-dropped", "    if (!a.mapKnown || !b.mapKnown || a.mapId != b.mapId)\n        return false;",
     "    if (!a.mapKnown || !b.mapKnown)\n        return false;", "instance"),
    ("position-distance-with-z",
     "    float const dx = b.x - a.x;\n    float const dy = b.y - a.y;\n    return std::sqrt(dx * dx + dy * dy);",
     "    float const dx = b.x - a.x;\n    float const dy = b.y - a.y;\n    float const dz = b.z - a.z;\n"
     "    return std::sqrt(dx * dx + dy * dy + dz * dz);", "distance"),
]


def run_mutations():
    pure = read(PURE)
    cpp = read(CPP)
    passed, output = run_unit(pure)
    if not passed:
        print(f"ERROR: the unmutated header fails the unit test:\n{output}", file=sys.stderr)
        return 1
    survivors = []
    for name, original, mutant, _ in PURE_MUTANTS:
        if pure.count(original) != 1:
            print(f"ERROR: {name}: anchor found {pure.count(original)} times, expected 1", file=sys.stderr)
            return 1
        ok, _ = run_unit(pure.replace(original, mutant, 1))
        if ok:
            survivors.append(name)
        else:
            print(f"MUTATION CAUGHT: {name}")

    cpp_mutants = [
        ("flag-ignored", "IsWithinLOSInMapCompat(a.unit, b.unit, true)", "IsWithinLOSInMapCompat(a.unit, b.unit, false)",
         check_sight_call),
        ("other-function", "IsWithinLOSInMapCompat(a.unit, b.unit, true)",
         "IsWithinLOSCompat(a.unit, b.unit->GetPositionX(), b.unit->GetPositionY(), b.unit->GetPositionZ(), true)",
         check_sight_call),
        ("order-swapped", '    rec.N("los", los ? 1 : 0);\n    rec.S("dist", distance);',
         '    rec.S("dist", distance);\n    rec.N("los", los ? 1 : 0);', check_one_line),
        ("hidden-map-leaks", "if (a.state.mapKnown && !hideWorld)", "if (a.state.mapKnown)", check_no_oracle),
        ("instance-dropped", "out.state.instanceId = unit->GetInstanceId();", "out.state.instanceId = 0;",
         check_instance_order),
        ("first-instance-guessed", "return sMapMgr.FindMap(mapId, FirstInstanceIdCompat(mapId));",
         "return sMapMgr.FindMap(mapId, 0);", check_instance_order),
        ("gm-log-flood", "if (!isGm)\n        {", "if (true)\n        {", check_gm_skips_access),
    ]
    for name, original, mutant, check in cpp_mutants:
        if cpp.count(original) != 1:
            print(f"ERROR: {name}: anchor found {cpp.count(original)} times, expected 1", file=sys.stderr)
            return 1
        try:
            check(cpp)
        except AssertionError as error:
            print(f"ERROR: the unmutated source fails {name}'s check: {error}", file=sys.stderr)
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
    if os.environ.get("LOSQUERY_MUTATE") == "1":
        sys.exit(run_mutations())
    unittest.main()
