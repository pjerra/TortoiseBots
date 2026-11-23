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


def check_dopull_arg(src):
    body = function_body(strip_comments(src), "OrderAnswer DoPull(")
    if ".arg = p.pullOption" not in body:
        raise AssertionError("DoPull drops the parsed pull option (the `body` fix)")


def check_pull_run_options(src):
    body = function_body(strip_comments(src), "std::string Run(Player* bot, PlayerbotAI* ai, Order const& o)")
    for pin in ("ParsePullOrderOption(", "BeginCommand("):
        if pin not in body:
            raise AssertionError(f"the h3 pull run does not call {pin}")


def check_interrupt_action_order(src):
    c = strip_comments(src)
    start = c.find("kInterruptActions[]")
    if start < 0:
        raise AssertionError("kInterruptActions is missing")
    block = c[start:c.find("};", start)]
    order = [block.find(f'"{action}"') for action in ("counterspell", "kick", "death coil")]
    if min(order) < 0 or order != sorted(order):
        raise AssertionError("kInterruptActions is not in TB's probe order")
    for gone in ("spell lock", "repentance"):
        if f'"{gone}"' in block:
            raise AssertionError(f"kInterruptActions carries `{gone}`, which TB's port drops")


def check_interrupt_named(src):
    if "anyNamed && !probes[i].named" not in strip_comments(src):
        raise AssertionError("PickInterrupt ignores the named bot")


def check_interrupt_run(src):
    # The order ends ok only from a real cast request: Run hands the interrupt to InterruptTickAt (never "ok" from a queued action) and keeps
    # a not-yet-cast order pending for the next engine ticks.
    body = function_body(tactics_run_body(src), 'if (verb == "interrupt")')
    for pin in ("InterruptTickAt(", "PendingInterrupts()[bot->GetGUID()] = pending"):
        if pin not in body:
            raise AssertionError(f"the interrupt run does not use {pin}")
    if "QueueAction(" in body or 'return "ok"' in body:
        raise AssertionError("the interrupt run reports ok without a cast request")


def check_interrupt_cast_path(src):
    c = strip_comments(src)
    body = function_body(c, "bool TryInterruptCast(")
    for pin in ("InterruptNonMeleeSpells(false)", "SetStandState(UNIT_STAND_STATE_STAND)", "SetSelection(",
                '"interrupt target", action', "pick->Reset()", "DoSpecificAction(action", "return ai->CastSpell(action, target);"):
        if pin not in body:
            raise AssertionError(f"TryInterruptCast lacks {pin}")
    if body.index("DoSpecificAction(action") > body.index("return ai->CastSpell(action, target);"):
        raise AssertionError("TryInterruptCast must try the class action before the direct cast")


def check_interrupt_tick_result(src):
    body = function_body(strip_comments(src), "std::string InterruptTickAt(")
    for pin in ("NextInterruptTick(", "InterruptTickResult(tick)", "TryInterruptCast("):
        if pin not in body:
            raise AssertionError(f"InterruptTickAt lacks {pin}")


def check_interrupt_poll_hook(src):
    c = strip_comments(src)
    start = c.find("void BeforeEngineTick(")
    if start < 0:
        raise AssertionError("TbOrders.cpp lost BeforeEngineTick")
    body = function_body(c, "void BeforeEngineTick(")
    if "tactics::PollPendingInterrupt(" not in body:
        raise AssertionError("BeforeEngineTick never retries a pending interrupt")
    if body.index("tactics::PollPendingInterrupt(") > body.index("rec->second.queue.empty()"):
        raise AssertionError("the pending interrupt must be retried before the order queue is read")
    if 'tactics::CancelPendingInterrupt(bot)' not in body:
        raise AssertionError("another order does not end a pending interrupt")


def check_ranged_pull_diagnostic(src):
    c = strip_comments(src)
    if "GetRangedBlocker(bot->GetDistance(mob))" not in c or "opens as a body pull" not in src:
        raise AssertionError("a ranged pull that falls back to the body pull is not logged")


def check_interrupt_prefers_in_range(src):
    c = strip_comments(src)
    if "pass == 0 ? probes[i].inRange : probes[i].actions" not in c:
        raise AssertionError("PickInterrupt does not prefer a bot in range")


def check_interrupt_probe_in_range(src):
    body = function_body(strip_comments(src), "Player* PickInterruptBot(")
    if "CanCastSpell(key, target, 0, nullptr, false, true, true)" not in body or "probe.inRange.push_back(key)" not in body:
        raise AssertionError("PickInterruptBot does not probe the range")


def check_pullback_default_hold(src):
    body = function_body(strip_comments(src), "std::string Run(Player* bot, PlayerbotAI* ai, Order const& o)")
    if "option.hasSeconds ? option.seconds : kPullJoinDelaySeconds;" not in body or "PausePartyPullHold(bot, holdDelay," not in body:
        raise AssertionError("a default pullback no longer keeps the 3 s party hold")


def check_cmd_interrupt(src):
    body = function_body(strip_comments(src), "bool TryHandleTacticalVerb(")
    for pin in ('verb == "interrupt"', "PickInterruptBot("):
        if pin not in body:
            raise AssertionError(f"TryHandleTacticalVerb does not handle interrupt ({pin})")


M3_TOGGLES = ("aoe", "autocc", "sap", "loot")
M3_STRATEGY_KEYS = ("no aoe", "auto cc")


def check_slice_keys_m3(src):
    keys = slice_key_values(src)
    for key in M3_STRATEGY_KEYS:
        if key not in keys["strategy"]:
            raise AssertionError(f"SLICE_KEYS strategy lacks `{key}`")


def check_slice_w4_m3(src):
    lines = [line.strip() for line in src.splitlines()]
    if "ai/playerbot/strategy/generic/NoAoeStrategy.cpp" not in lines:
        raise AssertionError("SLICE-W4.txt lacks NoAoeStrategy.cpp")


def check_before_tick(src):
    body = function_body(strip_comments(src), "void BeforeEngineTick(Player* bot")
    if "tactics::BeforeTick(" not in body:
        raise AssertionError("BeforeEngineTick does not call tactics::BeforeTick")


def check_sap_run(src):
    body = tactics_run_body(src)
    sap = body.find('verb == "sap"')
    if sap < 0:
        raise AssertionError("TbTactics Run does not handle the sap toggle")
    rogue = body.find("CLASS_ROGUE")
    if rogue < 0 or rogue < sap:
        raise AssertionError("the sap Run does not skip non-rogues (CLASS_ROGUE missing after the branch)")


def check_corpserun_refusal(src):
    body = tactics_run_body(src)
    corpse = body.find('verb == "corpserun"')
    if corpse < 0:
        raise AssertionError("TbTactics Run does not handle corpserun")
    not_dead = body.find('"fail:not-dead"')
    if not_dead < 0 or not_dead < corpse:
        raise AssertionError("the corpserun Run does not refuse an alive bot with fail:not-dead")


def check_corpserun_preflight(src):
    body = function_body(strip_comments(src), "bool Preflight(Player* bot")
    corpse = body.find('verb == "corpserun"')
    if corpse < 0:
        raise AssertionError("TbTactics Preflight does not handle corpserun")
    not_dead = body.find('"not-dead"')
    if not_dead < 0 or not_dead < corpse:
        raise AssertionError("the corpserun Preflight does not refuse an alive bot with not-dead")


def check_body_release(src):
    body = tactics_run_body(src)
    corpse = body.find('verb == "corpserun"')
    if corpse < 0:
        raise AssertionError("TbTactics Run does not handle corpserun")
    release = body.find('"release"')
    if release < 0 or release < corpse:
        raise AssertionError("the corpserun Run does not route the body through the release action")


def check_before_tick_toggles(src):
    c = strip_comments(src)
    body = function_body(c, "void BeforeTick(Player* bot")
    for key in ("no aoe", "auto cc", "no sap", "loot"):
        if f'"{key}"' not in body:
            raise AssertionError(f"tactics::BeforeTick does not enforce `{key}`")
    if 'ChangeStrategy(wantNoAoe ? "+no aoe" : "-no aoe"' not in body:
        raise AssertionError("tactics::BeforeTick does not change the aoe strategy")


def check_aoe_strategy_sense(src):
    c = strip_comments(src)
    body = function_body(c, "void BeforeTick(Player* bot")
    if 'bool const wantNoAoe = !t.aoe;' not in body:
        raise AssertionError("BeforeTick does not invert the aoe toggle (no aoe present = off)")
    if 'HasStrategy("no aoe"' not in body:
        raise AssertionError("BeforeTick does not read HasStrategy(\"no aoe\")")


def check_status_validates(orders_src, cmd_src):
    """Codex finding: a toggle `status` read must run the per-bot check (no engine, sap on a non-rogue) like a change does."""
    o = strip_comments(orders_src)
    i = o.find("ToggleOp::Status")
    if i < 0 or "ToggleStatusFault(" not in o[i:i + 1500]:
        raise AssertionError(".tb order toggle status does not call tactics::ToggleStatusFault")
    c = strip_comments(cmd_src)
    j = c.find("ToggleOp::Status")
    if j < 0 or "ToggleStatusFault(" not in c[j:j + 1500]:
        raise AssertionError(".tb cmd toggle status does not call tactics::ToggleStatusFault")


def check_status_fault_codes(src):
    body = function_body(strip_comments(src), "bool ToggleStatusFault(Player* bot")
    for needle in ('"no-rogue"', '"no-engine"', "ReadToggle("):
        if needle not in body:
            raise AssertionError(f"ToggleStatusFault lacks {needle}")


SOURCES = {
    "orders": lambda: read(HOST / "TbOrders.cpp"),
    "cmd_orders": lambda: read(CMD / "TbCmdOrders.cpp"),
    "tactics": lambda: read(HOST / "TbTactics.cpp"),
    "tactics_pure": lambda: read(HOST / "TbTacticsPure.h"),
    "cmd_tactics": lambda: read(CMD / "TbCmdTactics.cpp"),
    "run_orders": lambda: read(HOST / "run" / "TbRunOrders.cpp"),
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

    def test_dopull_passes_the_pull_option(self):
        check_dopull_arg(self.sources["run_orders"])

    def test_h3_pull_run_parses_the_option(self):
        check_pull_run_options(self.sources["orders"])

    def test_interrupt_probe_order(self):
        check_interrupt_action_order(self.sources["tactics_pure"])

    def test_interrupt_honours_the_named_bot(self):
        check_interrupt_named(self.sources["tactics_pure"])

    def test_interrupt_run_queues_the_mature_action(self):
        check_interrupt_run(self.sources["tactics"])

    def test_interrupt_order_casts_and_retries(self):
        check_interrupt_cast_path(self.sources["tactics"])
        check_interrupt_tick_result(self.sources["tactics"])

    def test_pending_interrupt_is_retried_every_tick(self):
        check_interrupt_poll_hook(self.sources["orders"])

    def test_ranged_pull_fallback_is_logged(self):
        check_ranged_pull_diagnostic(self.sources["orders"])

    def test_interrupt_prefers_in_range(self):
        check_interrupt_prefers_in_range(self.sources["tactics_pure"])

    def test_interrupt_probes_range(self):
        check_interrupt_probe_in_range(self.sources["tactics"])

    def test_pullback_default_hold(self):
        check_pullback_default_hold(self.sources["orders"])

    def test_cmd_handles_interrupt(self):
        check_cmd_interrupt(self.sources["cmd_tactics"])

    def test_slice_keys_hold_the_m3_strategy_keys(self):
        check_slice_keys_m3(self.sources["gen"])

    def test_slice_w4_holds_noaoe_strategy(self):
        check_slice_w4_m3(self.sources["w4"])

    def test_before_engine_tick_calls_before_tick(self):
        check_before_tick(self.sources["orders"])

    def test_sap_run_skips_non_rogues(self):
        check_sap_run(self.sources["tactics"])

    def test_corpserun_run_refuses_alive(self):
        check_corpserun_refusal(self.sources["tactics"])

    def test_corpserun_preflight_refuses_alive(self):
        check_corpserun_preflight(self.sources["tactics"])

    def test_corpserun_body_routes_through_release(self):
        check_body_release(self.sources["tactics"])

    def test_before_tick_enforces_toggles(self):
        check_before_tick_toggles(self.sources["tactics"])

    def test_aoe_strategy_sense(self):
        check_aoe_strategy_sense(self.sources["tactics"])

    def test_status_reads_validate_per_bot(self):
        check_status_validates(self.sources["orders"], self.sources["cmd_tactics"])

    def test_status_fault_codes(self):
        check_status_fault_codes(self.sources["tactics"])


MUTATIONS = [
    ("drop-stop-verb", "orders", lambda s: re.sub(r'"stop",', "", s, count=1), check_is_verb),
    ("skip-cmd-dispatch", "cmd_orders", lambda s: s.replace("TryHandleTacticalVerb(", "TryHandleTaskVerb("), check_cmd_dispatch),
    ("skip-run-dispatch", "orders", lambda s: s.replace("tactics::Run(", "Run(", 1), check_run_dispatch),
    ("skip-stop-body", "tactics", lambda s: s.replace("CombatStopWithPets(true);", ""), check_stop_run),
    ("drop-d4", "tactics", lambda s: s.replace('EndPullWindow("order")', 'EndPullWindow("run-end")'), check_engage_window_end),
    ("drop-rebuff-anchor", "tactics", lambda s: s.replace('"force rebuff begin ms"', '"force rebuff begin"'), check_rebuff_window),
    ("drop-slice-strategy", "gen", lambda s: s.replace('"strategy": "dps assist, close, follow, guard, no aoe, auto cc"', '"strategy": "dps assist, close, follow, no aoe, auto cc"'), check_slice_keys),
    ("drop-w4-guard", "w4", lambda s: s.replace("ai/playerbot/strategy/generic/GuardStrategy.cpp\n", ""), check_slice_w4),
    ("shrink-need-action", "orders", lambda s: s.replace("needAction[3]", "needAction[2]"), check_need_action),
    ("drop-applykit-guard", "orders", lambda s: s.replace('ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT)', 'ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)'), check_applykit_keeps_guard),
    ("drop-alive-refusal", "tactics", lambda s: s.replace("if (!bot->IsAlive())", "if (false)", 1), check_preflight_alive_first),
    ("cmd-missing-verb-old-list", "cmd_orders", lambda s: s.replace(", stop, flee, guard, stance, engage, rebuff, aoe, autocc, sap, loot, corpserun\");", "\");", 1), check_cmd_missing_verb_lists_m1),
    ("drop-pull-arg", "run_orders", lambda s: s.replace(".arg = p.pullOption;", "", 1), check_dopull_arg),
    ("reverse-interrupt-order", "tactics_pure", lambda s: s.replace('"counterspell", "silence", "kick"', '"kick", "silence", "counterspell"', 1), check_interrupt_action_order),
    ("ignore-in-range-interrupt", "tactics_pure", lambda s: s.replace("pass == 0 ? probes[i].inRange : probes[i].actions", "probes[i].actions", 1), check_interrupt_prefers_in_range),
    ("drop-range-probe", "tactics", lambda s: s.replace("probe.inRange.push_back(key)", "(void)key", 1), check_interrupt_probe_in_range),
    ("pullback-hold-config-delay", "orders", lambda s: s.replace("PausePartyPullHold(bot, holdDelay,", "PausePartyPullHold(bot, delay,", 1), check_pullback_default_hold),
    ("drop-direct-interrupt-cast", "tactics", lambda s: s.replace("return ai->CastSpell(action, target);", "return false;", 1), check_interrupt_cast_path),
    ("drop-own-cast-stop", "tactics", lambda s: s.replace("bot->InterruptNonMeleeSpells(false);", "", 1), check_interrupt_cast_path),
    ("drop-pick-reset", "tactics", lambda s: s.replace("pick->Reset();", "", 1), check_interrupt_cast_path),
    ("interrupt-ok-from-queue", "tactics", lambda s: s.replace("std::string const result = InterruptTickAt(bot, ai, pending, requester, finished);", "ai->GetCurrentEngine()->QueueAction(arg, ACTION_PASSTROUGH, ai::Event());\n        std::string const result = \"ok\"; (void)finished;", 1), check_interrupt_run),
    ("drop-interrupt-poll", "orders", lambda s: s.replace("tactics::PollPendingInterrupt(", "tactics::Forget(", 1), check_interrupt_poll_hook),
    ("drop-ranged-log", "orders", lambda s: s.replace("opens as a body pull", "opens", 1), check_ranged_pull_diagnostic),
    ("ignore-named-interrupt", "tactics_pure", lambda s: s.replace("anyNamed && !probes[i].named", "false", 1), check_interrupt_named),
    ("drop-slice-m3-strategy", "gen", lambda s: s.replace('"strategy": "dps assist, close, follow, guard"', '"strategy": "dps assist, close, follow, guard"').replace(", no aoe, auto cc", "").replace("no aoe, auto cc, ", "").replace(", no aoe", "").replace("no aoe, ", "").replace(", auto cc", "").replace("auto cc, ", ""), check_slice_keys_m3),
    ("drop-w4-noaoe", "w4", lambda s: s.replace("ai/playerbot/strategy/generic/NoAoeStrategy.cpp\n", ""), check_slice_w4_m3),
    ("drop-before-tick-call", "orders", lambda s: s.replace("tactics::BeforeTick(", "", 1), check_before_tick),
    ("drop-sap-rogue-check", "tactics", lambda s: s.replace('if (verb == "sap" && bot->GetClass() != CLASS_ROGUE)\n            return "fail:no-rogue";', "", 1), check_sap_run),
    ("drop-corpserun-not-dead", "tactics", lambda s: s.replace('if (bot->IsAlive())\n            return "fail:not-dead";', "", 1), check_corpserun_refusal),
    ("invert-aoe-sense", "tactics", lambda s: s.replace('bool const wantNoAoe = !t.aoe;', 'bool const wantNoAoe = t.aoe;'), check_aoe_strategy_sense),
    ("drop-order-status-validation", "orders", lambda s: s.replace("ToggleStatusFault(", "ReadToggle(", 1), lambda src: check_status_validates(src, read(CMD / "TbCmdTactics.cpp"))),
    ("drop-cmd-status-validation", "cmd_tactics", lambda s: s.replace("ToggleStatusFault(", "ReadToggle(", 1), lambda src: check_status_validates(read(HOST / "TbOrders.cpp"), src)),
    ("drop-no-rogue-status-fault", "tactics", lambda s: s.replace('code = "no-rogue";', 'code = "x";', 1), check_status_fault_codes),
    ("drop-before-tick-enforce", "tactics", lambda s: s.replace('if (ai->HasStrategy("no aoe", BotState::BOT_STATE_COMBAT) != wantNoAoe)\n            ai->ChangeStrategy(wantNoAoe ? "+no aoe" : "-no aoe", BotState::BOT_STATE_COMBAT);', "", 1), check_before_tick_toggles),
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
