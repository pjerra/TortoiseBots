#!/usr/bin/env python3
"""Wiring pins and mutation test for tactical orders M1 (stop, flee, guard, stance, engage, rebuff).

Run:
  python3 forever/tools/test_tactics_wiring.py
  TACTICS_MUTATE=1 python3 forever/tools/test_tactics_wiring.py
"""
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"
CMD = HOST / "cmd"


def read(path):
    return Path(path).read_text() if Path(path).exists() else ""


def strip_comments(source):
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, signature):
    """The braced body of the function whose declaration starts with `signature`."""
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body: {signature}")
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:i]
    raise AssertionError(f"unterminated body: {signature}")


M1_VERBS = ("stop", "flee", "guard", "stance", "engage", "rebuff")


def check_is_verb(src):
    body = function_body(strip_comments(src), "bool IsVerb(")
    for verb in M1_VERBS:
        if f'"{verb}"' not in body:
            raise AssertionError(f"IsVerb does not accept `{verb}`")


def check_cmd_dispatch(src):
    c = strip_comments(src)
    tactical = c.find("TryHandleTacticalVerb(")
    judge = c.find("run::Issue(")
    if tactical < 0:
        raise AssertionError("TbCmdOrders.cpp never calls TryHandleTacticalVerb")
    if judge < 0:
        raise AssertionError("TbCmdOrders.cpp does not route orders through run::Issue")
    if tactical > judge:
        raise AssertionError("TryHandleTacticalVerb must run before run::Issue")


def check_run_dispatch(src):
    c = strip_comments(src)
    tactical = c.find("tactics::Run(")
    unknown = c.find('"fail:unknown-verb"')
    if tactical < 0:
        raise AssertionError("TbOrders.cpp Run does not dispatch tactics::Run")
    if unknown < 0:
        raise AssertionError("TbOrders.cpp Run lost its unknown-verb fallback")
    if tactical > unknown:
        raise AssertionError("tactics::Run must run before the unknown-verb fallback")
    pre = c.find("tactics::Preflight(")
    if pre < 0:
        raise AssertionError("PreflightOrder does not dispatch tactics::Preflight")


def tactics_run_body(src):
    return function_body(strip_comments(src), "std::string Run(Player* bot")


def check_stop_run(src):
    body = function_body(tactics_run_body(src), 'if (verb == "stop")')
    for pin in ("CombatStopWithPets(true)", "GetMotionMaster()->Clear()"):
        if pin not in body:
            raise AssertionError(f"stop does not call {pin}")


def check_engage_window_end(src):
    c = tactics_run_body(src)
    if 'if (verb != "engage" && ai->HasEngagePullWindow())' not in c:
        raise AssertionError("tactics::Run lacks the D4 guard (verb != engage && HasEngagePullWindow)")
    if 'EndPullWindow("order")' not in c:
        raise AssertionError("tactics::Run never ends an engage window with EndPullWindow(\"order\")")


def check_rebuff_window(src):
    body = function_body(tactics_run_body(src), 'if (verb == "rebuff")')
    if '"force rebuff begin ms"' not in body:
        raise AssertionError("rebuff does not write the force rebuff window anchor")


def slice_key_values(src):
    block = re.search(r"^SLICE_KEYS = \{(.*?)^\}", src, re.S | re.M)
    if not block:
        raise AssertionError("slice_gen_context.py SLICE_KEYS block not found")
    out = {}
    for key in ("strategy", "action"):
        matches = re.findall(rf'"{key}":\s*((?:"[^"]*"\s*)+)', block.group(1), re.S)
        if not matches:
            raise AssertionError(f"SLICE_KEYS {key} list not found")
        out[key] = [word.strip() for literal in re.findall(r'"([^"]*)"', matches[0]) for word in literal.split(",") if word.strip()]
    return out


def check_slice_keys(src):
    keys = slice_key_values(src)
    if "guard" not in keys["strategy"]:
        raise AssertionError("SLICE_KEYS strategy lacks `guard`")
    for key in ("guard chat shortcut", "guard", "stance"):
        if key not in keys["action"]:
            raise AssertionError(f"SLICE_KEYS action lacks `{key}`")


def check_slice_w4(src):
    lines = [line.strip() for line in src.splitlines()]
    if "ai/playerbot/strategy/generic/GuardStrategy.cpp" not in lines:
        raise AssertionError("SLICE-W4.txt lacks GuardStrategy.cpp")


def check_need_action(src):
    if "char const* needAction[3]" not in strip_comments(src):
        raise AssertionError("the ah verb needs needAction[3] (needAction[2] is out of bounds)")


def check_applykit_keeps_guard(src):
    body = function_body(strip_comments(src), "void ApplyKit(")
    if 'HasStrategy("guard",' not in body:
        raise AssertionError("ApplyKit does not keep the guard movement mode")


def check_preflight_alive_first(src):
    body = function_body(strip_comments(src), "bool Preflight(Player* bot")
    alive = body.find("if (!bot->IsAlive())")
    first_verb = body.find('if (verb == "stop")')
    if alive < 0 or '"not-alive"' not in body:
        raise AssertionError("tactics::Preflight does not refuse a dead bot with not-alive")
    if first_verb < 0 or alive > first_verb:
        raise AssertionError("the dead-bot refusal must come before every verb's own branch")


def check_cmd_missing_verb_lists_m1(src):
    c = strip_comments(src)
    start = c.find('"missing-verb"')
    if start < 0:
        raise AssertionError("HandleCmd lost its missing-verb answer")
    line = c[start:c.find("\n", start)]
    for verb in M1_VERBS:
        if verb not in line:
            raise AssertionError(f"the `.tb cmd` missing-verb text does not list `{verb}`")


SOURCES = {
    "orders": lambda: read(HOST / "TbOrders.cpp"),
    "cmd_orders": lambda: read(CMD / "TbCmdOrders.cpp"),
    "tactics": lambda: read(HOST / "TbTactics.cpp"),
    "gen": lambda: read(ROOT / "forever" / "tools" / "slice_gen_context.py"),
    "w4": lambda: read(ROOT / "forever" / "SLICE-W4.txt"),
}


class Wiring(unittest.TestCase):
    def setUp(self):
        self.sources = {name: load() for name, load in SOURCES.items()}

    def test_is_verb_has_the_m1_verbs(self):
        check_is_verb(self.sources["orders"])

    def test_cmd_dispatches_tactics_before_the_judge(self):
        check_cmd_dispatch(self.sources["cmd_orders"])

    def test_order_run_and_preflight_dispatch_tactics(self):
        check_run_dispatch(self.sources["orders"])

    def test_stop_clears_combat_and_movement(self):
        check_stop_run(self.sources["tactics"])

    def test_every_tactical_run_but_engage_ends_the_engage_window(self):
        check_engage_window_end(self.sources["tactics"])

    def test_rebuff_writes_the_window_anchor(self):
        check_rebuff_window(self.sources["tactics"])

    def test_slice_keys_hold_the_m1_keys(self):
        check_slice_keys(self.sources["gen"])

    def test_slice_w4_holds_guard_strategy(self):
        check_slice_w4(self.sources["w4"])

    def test_ah_need_action_is_sized_three(self):
        check_need_action(self.sources["orders"])

    def test_applykit_keeps_guard(self):
        check_applykit_keeps_guard(self.sources["orders"])

    def test_preflight_refuses_a_dead_bot_for_every_verb(self):
        check_preflight_alive_first(self.sources["tactics"])

    def test_cmd_missing_verb_lists_the_m1_verbs(self):
        check_cmd_missing_verb_lists_m1(self.sources["cmd_orders"])


MUTATIONS = [
    ("drop-stop-verb", "orders", lambda s: re.sub(r'"stop",', "", s, count=1), check_is_verb),
    ("skip-cmd-dispatch", "cmd_orders", lambda s: s.replace("TryHandleTacticalVerb(", "TryHandleTaskVerb("), check_cmd_dispatch),
    ("skip-run-dispatch", "orders", lambda s: s.replace("tactics::Run(", "Run(", 1), check_run_dispatch),
    ("skip-stop-body", "tactics", lambda s: s.replace("CombatStopWithPets(true);", ""), check_stop_run),
    ("drop-d4", "tactics", lambda s: s.replace('EndPullWindow("order")', 'EndPullWindow("run-end")'), check_engage_window_end),
    ("drop-rebuff-anchor", "tactics", lambda s: s.replace('"force rebuff begin ms"', '"force rebuff begin"'), check_rebuff_window),
    ("drop-slice-strategy", "gen", lambda s: s.replace('"strategy": "dps assist, close, follow, guard"', '"strategy": "dps assist, close, follow"'), check_slice_keys),
    ("drop-w4-guard", "w4", lambda s: s.replace("ai/playerbot/strategy/generic/GuardStrategy.cpp\n", ""), check_slice_w4),
    ("shrink-need-action", "orders", lambda s: s.replace("needAction[3]", "needAction[2]"), check_need_action),
    ("drop-applykit-guard", "orders", lambda s: s.replace('ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT)', 'ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)'), check_applykit_keeps_guard),
    ("drop-alive-refusal", "tactics", lambda s: s.replace("if (!bot->IsAlive())", "if (false)", 1), check_preflight_alive_first),
    ("cmd-missing-verb-old-list", "cmd_orders", lambda s: s.replace(", stop, flee, guard, stance, engage, rebuff\");", "\");", 1), check_cmd_missing_verb_lists_m1),
]


def run_mutations():
    sources = {name: load() for name, load in SOURCES.items()}
    failures = []
    for name, target, mutate, check in MUTATIONS:
        mutated = dict(sources)
        mutated[target] = mutate(sources[target])
        try:
            check(mutated[target])
        except AssertionError as exc:
            print(f"MUTATION CAUGHT {name}: {exc}")
            continue
        failures.append(name)
    if failures:
        print("ERROR: mutations not caught: " + ", ".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if os.environ.get("TACTICS_MUTATE") == "1":
        sys.exit(run_mutations())
    unittest.main()
