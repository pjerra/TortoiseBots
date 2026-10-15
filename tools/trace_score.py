#!/usr/bin/env python3
"""Score a run trace (bt-trace v1) with the first oracle set.

A trace is the JSONL file the server's run trace recorder writes
(`.bot trace start <run_id>`, runtime/RunTrace.cpp; schema in
docs/guides/run-trace.md). The oracles are pure functions of the trace, so an
old trace can be re-scored after an oracle changes and two builds compared on
the same scenario.

Oracles (ids are stable, they key dashboard rows and comparisons):

  action_loop      the same action FAILED / IMPOSSIBLE more than N times within
                   M seconds (the spellstone, stay and melee loops)
  threat_off_tank  a hostile stays on a party member who is not the tank for
                   longer than a hold time while a tank is alive and fighting
  idle_caster      a dps caster in combat, not out of mana, that neither casts
                   nor executes a single action for longer than a hold time
  deaths           any party bot died
  oom_healer       a healer sat out of mana while the party was in combat
  rest_failure     eat / drink / rest failing in a loop, or a bot standing out
                   of combat at low mana without recovering
  facing           in melee range of, or casting at, a hostile target while
                   facing away from it for longer than a hold time
  self_interrupt   casts cancelled while the bot was moving, several times
                   within a window
  stuck            a bot whose movement is active but whose position does not
                   change, or that oscillates between two spots
  in_fire          a bot standing in a harmful ground / area effect

The last four need data a producer may not have: each answers `na` when the
trace's header (`caps`) or its samples show that the producer cannot see it.

Usage:
    python3 tools/trace_score.py <trace.jsonl | directory> [--no-write]
        [--json] [--set loop_failures=8 ...] [--fail-on-violation]

By default the verdicts are written into the run's `<run_id>.summary.json`
(next to the trace; created when missing) as `oracles`, `verdict` and
`scored_unix`, so the dashboard reads one file per run.

Provenance: the idea of scoring a trace with pure oracles and the oracle
catalogue's shape come from mod-dungeon-clear's Pull Lab (jrad7/mod-dungeon-clear
@ da0bdfbc, src/Lab/DcLabOracles.*, AGPL-3.0-or-later); the oracles here are
new, for the 1.12 module (docs/PROVENANCE.md).
"""

import argparse
import bisect
import json
import os
import sys
import time
from collections import defaultdict, deque

SCORER = "trace_score.py v1"

DEFAULTS = {
    "loop_failures": 8,          # more than this many failures ...
    "loop_window_ms": 10000,     # ... within this window
    "threat_hold_ms": 3000,      # a hostile on a non-tank this long
    "idle_hold_ms": 4000,        # a caster doing nothing this long
    "idle_min_power_pct": 15,    # ... with at least this much mana
    "oom_pct": 5,                # a healer below this mana %
    "oom_hold_ms": 3000,         # ... this long, in combat
    "rest_fail_count": 5,        # eat/drink/rest failures ...
    "rest_fail_window_ms": 30000,  # ... within this window
    "rest_stall_ms": 20000,      # standing out of combat at low mana this long
    "rest_low_mana_pct": 30,     # ... below this mana %
    "facing_deg": 90,            # facing away: more than this many degrees off the target
    "facing_hold_ms": 2000,      # ... for this long
    "facing_melee_yd": 6,        # "in melee" = within this distance of the target
    "self_interrupt_count": 3,   # cancelled-while-moving casts ...
    "self_interrupt_window_ms": 30000,  # ... within this window, per bot
    "stuck_hold_ms": 5000,       # movement active, no progress for this long
    "stuck_min_move_yd": 1,      # ... less than this far from where it started
    "osc_window_ms": 10000,      # oscillation: look at this window
    "osc_max_span_yd": 8,        # ... the bot stays within this span
    "osc_min_path_yd": 30,       # ... but walks at least this far
    "osc_min_reversals": 4,      # ... reversing direction at least this often
    "fire_hold_ms": 1500,        # standing in a harmful ground effect this long
}

FAILURES = ("FAILED", "IMPOSSIBLE")
IDLE_CLASSES = ("mage", "warlock", "priest")
REST_WORDS = ("rest", "drink", "eat", "food", "sit")
MAX_EXAMPLES = 5


class Trace:
    def __init__(self):
        self.header = {}
        self.party = {}      # guid -> party record
        self.mobs = {}       # guid -> mobs record
        self.frames = []     # frame records, in time order
        self.events = []
        self.actions = []
        self.end = None
        self.bad_lines = 0
        self.run = ""

    @property
    def complete(self):
        return self.end is not None

    @property
    def interval_ms(self):
        iv = self.header.get("interval_ms")
        if isinstance(iv, int) and iv > 0:
            return iv
        if len(self.frames) > 1:
            return max(1, self.frames[1]["t"] - self.frames[0]["t"])
        return 500

    def name(self, guid):
        member = self.party.get(guid)
        return member.get("name", guid) if member else guid


def load_trace(path):
    """Read a JSONL trace. Unknown kinds and fields are ignored; unreadable
    lines are counted in bad_lines."""
    trace = Trace()
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except ValueError:
                trace.bad_lines += 1
                continue
            if not isinstance(rec, dict):
                trace.bad_lines += 1
                continue
            kind = rec.get("k")
            if not trace.run and isinstance(rec.get("run"), str):
                trace.run = rec["run"]
            if kind == "header":
                trace.header = rec
            elif kind == "party":
                trace.party[rec.get("g")] = rec
            elif kind == "mobs":
                trace.mobs[rec.get("g")] = rec
            elif kind == "frame":
                trace.frames.append(rec)
            elif kind == "event":
                trace.events.append(rec)
            elif kind == "action":
                trace.actions.append(rec)
            elif kind == "end":
                trace.end = rec
    trace.frames.sort(key=lambda r: r.get("t", 0))
    return trace


def result(oracle_id, name, verdict, count=0, first_ms=None, detail="", examples=None):
    out = {"id": oracle_id, "name": name, "verdict": verdict, "count": count,
           "first_ms": first_ms, "detail": detail}
    if examples:
        out["examples"] = examples[:MAX_EXAMPLES]
    return out


def fmt_t(ms):
    return "%d.%ds" % (ms // 1000, (ms % 1000) // 100)


def pct(value, maximum):
    return 100.0 * value / maximum if maximum else 0.0


def runs_of(frames, pred):
    """Maximal runs of consecutive frames where pred(frame) is truthy:
    (start_t, end_t, first_frame, last_frame)."""
    out = []
    start = None
    for frame in frames:
        if pred(frame):
            if start is None:
                start = frame
            last = frame
        elif start is not None:
            out.append((start["t"], last["t"], start, last))
            start = None
    if start is not None:
        out.append((start["t"], last["t"], start, last))
    return out


# -- action_loop -------------------------------------------------------------

def failure_stamps(trace):
    """Every failing action occurrence as (t, guid, name, outcome). A merged
    record (n occurrences between t and tl) is spread evenly over that span."""
    stamps = []
    for rec in trace.actions:
        if rec.get("res") not in FAILURES:
            continue
        n = int(rec.get("n", 1) or 1)
        t0 = int(rec.get("t", 0))
        t1 = int(rec.get("tl", t0))
        for i in range(n):
            t = t0 if n == 1 else t0 + (t1 - t0) * i // (n - 1)
            stamps.append((t, rec.get("g"), rec.get("name", "?"), rec.get("res")))
    stamps.sort(key=lambda s: s[0])
    return stamps


def oracle_action_loop(trace, cfg):
    name = "action failure loop"
    if not trace.actions:
        return result("action_loop", name, "na", detail="no action records")
    failures = cfg["loop_failures"]
    window = cfg["loop_window_ms"]
    tracks = {}  # (guid, action) -> [deque stamps, last_report or None]
    loops = []
    for t, guid, action, outcome in failure_stamps(trace):
        stamps, state = tracks.setdefault((guid, action), (deque(), {"last": None}))
        stamps.append(t)
        while stamps and t - stamps[0] > window:
            stamps.popleft()
        while len(stamps) > failures + 1:
            stamps.popleft()
        if len(stamps) > failures and (state["last"] is None or t - state["last"] > window):
            state["last"] = t
            loops.append((t, guid, action, outcome, len(stamps)))
    if not loops:
        return result("action_loop", name, "pass",
                      detail="no action failed more than %d times in %d s" % (failures, window // 1000))
    examples = ["%s %s %s x%d+ at %s" % (trace.name(g), a, o, c, fmt_t(t)) for t, g, a, o, c in loops]
    live = sum(1 for e in trace.events if e.get("ev") == "action_loop")
    return result("action_loop", name, "fail", len(loops), loops[0][0],
                  "%d loop(s); live detector flagged %d" % (len(loops), live), examples)


# -- threat_off_tank ----------------------------------------------------------

def oracle_threat_off_tank(trace, cfg):
    name = "threat off the tank"
    tanks = {g for g, m in trace.party.items() if m.get("role") == "tank"}
    if not tanks:
        return result("threat_off_tank", name, "na", detail="no tank in the party")
    hold = cfg["threat_hold_ms"]
    viols = []
    open_runs = {}   # (mob, target) -> (start_t, last_t)
    for frame in trace.frames:
        bots = {b.get("g"): b for b in frame.get("bots", [])}
        tank_ok = any(bots.get(t, {}).get("al") and bots.get(t, {}).get("cb") for t in tanks)
        seen = set()
        for mob in frame.get("mobs", []):
            target = mob.get("tg")
            member = trace.party.get(target)
            if (tank_ok and mob.get("cb") and target in bots and member is not None
                    and member.get("role") != "tank" and bots[target].get("al")
                    and not mob.get("cc") and not mob.get("ev")):
                key = (mob.get("g"), target)
                seen.add(key)
                start, _ = open_runs.get(key, (frame["t"], frame["t"]))
                open_runs[key] = (start, frame["t"])
        for key in list(open_runs):
            if key not in seen:
                start, last = open_runs.pop(key)
                if last - start >= hold:
                    viols.append((start, last, key))
    for key, (start, last) in open_runs.items():
        if last - start >= hold:
            viols.append((start, last, key))
    viols.sort(key=lambda v: v[0])
    if not viols:
        return result("threat_off_tank", name, "pass",
                      detail="no hostile stayed on a non-tank for %d s" % (hold // 1000))
    examples = []
    for start, last, (mob, target) in viols:
        label = trace.mobs.get(mob, {}).get("name", mob)
        examples.append("%s on %s (%s) for %.1fs at %s" % (
            label, trace.name(target), trace.party.get(target, {}).get("role", "?"),
            (last - start) / 1000.0, fmt_t(start)))
    return result("threat_off_tank", name, "fail", len(viols), viols[0][0],
                  "%d stretch(es) of a hostile on a non-tank" % len(viols), examples)


# -- idle_caster --------------------------------------------------------------

def ok_intervals(trace):
    """guid -> (sorted starts, prefix-max of ends) over OK action records."""
    per = defaultdict(list)
    for rec in trace.actions:
        if rec.get("res") == "OK":
            t0 = int(rec.get("t", 0))
            per[rec.get("g")].append((t0, int(rec.get("tl", t0))))
    index = {}
    for guid, spans in per.items():
        spans.sort()
        starts = [s for s, _ in spans]
        best = []
        top = -1
        for _, end in spans:
            top = max(top, end)
            best.append(top)
        index[guid] = (starts, best)
    return index


def acted(index, guid, prev_t, t):
    """True when the bot executed an action at some time in [prev_t, t]."""
    entry = index.get(guid)
    if not entry:
        return False
    starts, best = entry
    i = bisect.bisect_right(starts, t) - 1
    return i >= 0 and best[i] >= prev_t


def oracle_idle_caster(trace, cfg):
    name = "idle caster in combat"
    casters = {g for g, m in trace.party.items()
               if m.get("role") == "dps" and m.get("class") in IDLE_CLASSES}
    if not casters:
        return result("idle_caster", name, "na", detail="no dps caster in the party")
    if not trace.actions:
        return result("idle_caster", name, "na", detail="no action records")
    hold = cfg["idle_hold_ms"]
    min_power = cfg["idle_min_power_pct"]
    index = ok_intervals(trace)
    viols = []
    for guid in sorted(casters):
        prev = {"t": 0}

        def idle(frame, guid=guid, prev=prev):
            bot = next((b for b in frame.get("bots", []) if b.get("g") == guid), None)
            before = prev["t"]
            prev["t"] = frame["t"]
            if not bot or not bot.get("al") or not bot.get("cb") or bot.get("cs"):
                return False
            if not frame.get("mobs"):
                return False
            if bot.get("pt") == 0 and pct(bot.get("pw", 0), bot.get("mpw", 0)) < min_power:
                return False
            return not acted(index, guid, before, frame["t"])

        for start, end, _, _ in runs_of(trace.frames, idle):
            if end - start >= hold:
                viols.append((start, end, guid))
    viols.sort()
    if not viols:
        return result("idle_caster", name, "pass",
                      detail="no caster stood idle for %d s in combat" % (hold // 1000))
    examples = ["%s idle %.1fs at %s" % (trace.name(g), (e - s) / 1000.0, fmt_t(s)) for s, e, g in viols]
    return result("idle_caster", name, "fail", len(viols), viols[0][0],
                  "%d idle stretch(es)" % len(viols), examples)


# -- deaths -------------------------------------------------------------------

def oracle_deaths(trace, cfg):
    name = "party deaths"
    if not trace.frames and not trace.events:
        return result("deaths", name, "na", detail="no frames")
    deaths = sorted((e for e in trace.events if e.get("ev") == "death"), key=lambda e: e.get("t", 0))
    if not deaths:
        return result("deaths", name, "pass", detail="no party bot died")
    examples = []
    for e in deaths:
        killer = trace.mobs.get(e.get("b"), {}).get("name")
        examples.append("%s died at %s%s" % (trace.name(e.get("a")), fmt_t(e.get("t", 0)),
                                             " (target of %s)" % killer if killer else ""))
    return result("deaths", name, "fail", len(deaths), deaths[0].get("t", 0),
                  "%d death(s)" % len(deaths), examples)


# -- oom_healer ---------------------------------------------------------------

def oracle_oom_healer(trace, cfg):
    name = "healer out of mana"
    healers = sorted(g for g, m in trace.party.items() if m.get("role") == "heal")
    if not healers:
        return result("oom_healer", name, "na", detail="no healer in the party")
    hold = cfg["oom_hold_ms"]
    limit = cfg["oom_pct"]
    viols = []
    for guid in healers:
        def oom(frame, guid=guid):
            bots = frame.get("bots", [])
            bot = next((b for b in bots if b.get("g") == guid), None)
            if not bot or not bot.get("al") or bot.get("pt") != 0 or not bot.get("mpw"):
                return False
            if not any(b.get("al") and b.get("cb") for b in bots):
                return False
            return pct(bot.get("pw", 0), bot["mpw"]) < limit

        for start, end, _, _ in runs_of(trace.frames, oom):
            if end - start >= hold:
                viols.append((start, end, guid))
    viols.sort()
    if not viols:
        return result("oom_healer", name, "pass", detail="no healer below %d%% mana in combat" % limit)
    examples = ["%s below %d%% mana for %.1fs at %s" % (trace.name(g), limit, (e - s) / 1000.0, fmt_t(s))
                for s, e, g in viols]
    return result("oom_healer", name, "fail", len(viols), viols[0][0],
                  "%d stretch(es) out of mana" % len(viols), examples)


# -- rest_failure -------------------------------------------------------------

def is_rest_action(action_name):
    tokens = "".join(c if c.isalnum() else " " for c in action_name.lower()).split()
    return any(t in REST_WORDS for t in tokens)


def oracle_rest_failure(trace, cfg):
    name = "rest / drink failures"
    if not trace.actions and not trace.frames:
        return result("rest_failure", name, "na", detail="no data")
    viols = []

    # Eating and drinking that keeps failing.
    count = cfg["rest_fail_count"]
    window = cfg["rest_fail_window_ms"]
    per = defaultdict(list)
    for t, guid, action, _ in failure_stamps(trace):
        if is_rest_action(action):
            per[guid].append((t, action))
    for guid, stamps in per.items():
        i = 0
        while i < len(stamps):
            j = i
            while j + 1 < len(stamps) and stamps[j + 1][0] - stamps[i][0] <= window:
                j += 1
            if j - i + 1 >= count:
                viols.append((stamps[i][0], "%s: %s failed %d times within %d s at %s" % (
                    trace.name(guid), stamps[i][1], j - i + 1, window // 1000, fmt_t(stamps[i][0]))))
                i = j + 1
            else:
                i += 1

    # A bot standing out of combat at low mana that does not recover.
    stall = cfg["rest_stall_ms"]
    low = cfg["rest_low_mana_pct"]
    for guid in sorted(trace.party):
        def stuck(frame, guid=guid):
            bot = next((b for b in frame.get("bots", []) if b.get("g") == guid), None)
            return bool(bot and bot.get("al") and not bot.get("cb") and bot.get("pt") == 0
                        and bot.get("mpw") and pct(bot.get("pw", 0), bot["mpw"]) < low)

        for start, end, first, last in runs_of(trace.frames, stuck):
            if end - start < stall:
                continue
            a = next(b for b in first["bots"] if b.get("g") == guid)
            z = next(b for b in last["bots"] if b.get("g") == guid)
            moved = abs(a.get("x", 0) - z.get("x", 0)) + abs(a.get("y", 0) - z.get("y", 0))
            if z.get("pw", 0) <= a.get("pw", 0) and moved < 5:
                viols.append((start, "%s stood %.0fs out of combat at %d%% mana without recovering (from %s)" % (
                    trace.name(guid), (end - start) / 1000.0, pct(z.get("pw", 0), z["mpw"]), fmt_t(start))))

    viols.sort()
    if not viols:
        return result("rest_failure", name, "pass", detail="eating, drinking and resting worked")
    return result("rest_failure", name, "fail", len(viols), viols[0][0],
                  "%d rest failure(s)" % len(viols), [v[1] for v in viols])


# -- helpers for the movement / facing oracles --------------------------------

def has_cap(trace, cap, sample_key=None):
    """True when the producer can see `cap`: the header lists it, or (for older
    traces without a header list) a bot sample carries `sample_key`."""
    caps = trace.header.get("caps")
    if isinstance(caps, list):
        return cap in caps
    if sample_key:
        return any(sample_key in b for f in trace.frames for b in f.get("bots", []))
    return False


def bot_series(trace, guid):
    """[(t, bot sample)] for one bot, in time order."""
    out = []
    for frame in trace.frames:
        for b in frame.get("bots", []):
            if b.get("g") == guid:
                out.append((frame["t"], b))
                break
    return out


def dist2(a, b):
    return ((a.get("x", 0) - b.get("x", 0)) ** 2 + (a.get("y", 0) - b.get("y", 0)) ** 2) ** 0.5


def dist3(a, b):
    return (dist2(a, b) ** 2 + (a.get("z", 0) - b.get("z", 0)) ** 2) ** 0.5


# -- facing -------------------------------------------------------------------

def oracle_facing(trace, cfg):
    name = "facing the target"
    if not has_cap(trace, "facing", "fa"):
        return result("facing", name, "na", detail="the producer does not record facing")
    hold = cfg["facing_hold_ms"]
    limit = cfg["facing_deg"]
    melee = cfg["facing_melee_yd"]
    viols = []
    for guid in sorted(trace.party):
        def away(frame, guid=guid):
            bot = next((b for b in frame.get("bots", []) if b.get("g") == guid), None)
            if not bot or not bot.get("al") or not bot.get("cb") or bot.get("fa") is None:
                return False
            if bot["fa"] <= limit:
                return False
            if bot.get("cs"):
                return True
            if bot.get("mv"):
                return False   # turning on the move: kiting, retreating
            target = next((m for m in frame.get("mobs", []) if m.get("g") == bot.get("tg")), None)
            return bool(target and dist3(bot, target) <= melee)

        for start, end, first, _ in runs_of(trace.frames, away):
            if end - start >= hold:
                bot = next(b for b in first["bots"] if b.get("g") == guid)
                viols.append((start, end, guid, bot["fa"], bool(bot.get("cs"))))
    viols.sort()
    if not viols:
        return result("facing", name, "pass",
                      detail="no bot fought or cast for %d s facing more than %d deg off its target" % (hold // 1000, limit))
    examples = ["%s %s %d deg off its target for %.1fs at %s" % (
        trace.name(g), "casting" if cast else "in melee", fa, (e - s) / 1000.0, fmt_t(s))
        for s, e, g, fa, cast in viols]
    return result("facing", name, "fail", len(viols), viols[0][0],
                  "%d stretch(es) facing away" % len(viols), examples)


# -- self_interrupt -----------------------------------------------------------

def oracle_self_interrupt(trace, cfg):
    name = "cast interrupted by own movement"
    if not has_cap(trace, "interrupts"):
        return result("self_interrupt", name, "na", detail="the producer does not record interrupts")
    need = cfg["self_interrupt_count"]
    window = cfg["self_interrupt_window_ms"]
    per = defaultdict(list)
    total = 0
    for e in trace.events:
        if e.get("ev") == "cast_interrupted":
            total += 1
            if e.get("s") == "moving":
                per[e.get("a")].append(e.get("t", 0))
    viols = []
    for guid, times in per.items():
        times.sort()
        i = 0
        while i < len(times):
            j = i
            while j + 1 < len(times) and times[j + 1] - times[i] <= window:
                j += 1
            if j - i + 1 >= need:
                viols.append((times[i], guid, j - i + 1))
                i = j + 1
            else:
                i += 1
    viols.sort()
    if not viols:
        return result("self_interrupt", name, "pass",
                      detail="%d cast(s) interrupted, none repeatedly while moving" % total)
    examples = ["%s: %d casts cancelled while moving within %d s from %s" % (
        trace.name(g), n, window // 1000, fmt_t(t)) for t, g, n in viols]
    return result("self_interrupt", name, "fail", len(viols), viols[0][0],
                  "%d bot(s) kept cancelling their own casts" % len(viols), examples)


# -- stuck --------------------------------------------------------------------

def reversals(points):
    """Direction reversals along a path: consecutive moves whose dot product is
    negative (moves under half a yard are noise)."""
    count = 0
    prev = None
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if (dx * dx + dy * dy) ** 0.5 < 0.5:
            continue
        if prev is not None and prev[0] * dx + prev[1] * dy < 0:
            count += 1
        prev = (dx, dy)
    return count


def oracle_stuck(trace, cfg):
    name = "stuck or oscillating"
    if not has_cap(trace, "moving", "mv"):
        return result("stuck", name, "na", detail="the producer does not record movement")
    hold = cfg["stuck_hold_ms"]
    min_move = cfg["stuck_min_move_yd"]
    window = cfg["osc_window_ms"]
    max_span = cfg["osc_max_span_yd"]
    min_path = cfg["osc_min_path_yd"]
    min_rev = cfg["osc_min_reversals"]
    viols = []
    for guid in sorted(trace.party):
        series = [(t, b) for t, b in bot_series(trace, guid) if b.get("al")]

        # Movement active but no progress.
        i = 0
        while i < len(series):
            if not series[i][1].get("mv"):
                i += 1
                continue
            j = i
            while (j + 1 < len(series) and series[j + 1][1].get("mv")
                   and dist2(series[j + 1][1], series[i][1]) < min_move):
                j += 1
            if series[j][0] - series[i][0] >= hold:
                viols.append((series[i][0], "%s had movement active but made no progress for %.1fs at %s" % (
                    trace.name(guid), (series[j][0] - series[i][0]) / 1000.0, fmt_t(series[i][0]))))
                i = j + 1
            else:
                i += 1

        # Oscillation out of combat: a long walk inside a small area, turning back again and again.
        i = 0
        while i < len(series):
            j = i
            while j + 1 < len(series) and series[j + 1][0] - series[i][0] <= window:
                j += 1
            span_ok = series[j][0] - series[i][0] >= window * 0.8
            chunk = series[i:j + 1]
            if span_ok and all(b.get("mv") and not b.get("cb") for _, b in chunk):
                pts = [(b.get("x", 0), b.get("y", 0)) for _, b in chunk]
                xs = [p[0] for p in pts]
                ys = [p[1] for p in pts]
                span = max(max(xs) - min(xs), max(ys) - min(ys))
                path = sum(((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5 for a, b in zip(pts, pts[1:]))
                if span <= max_span and path >= min_path and reversals(pts) >= min_rev:
                    viols.append((series[i][0], "%s oscillated within %.0f yd (%.0f yd walked) for %.0fs at %s" % (
                        trace.name(guid), span, path, (series[j][0] - series[i][0]) / 1000.0, fmt_t(series[i][0]))))
                    i = j + 1
                    continue
            i += 1
    viols.sort()
    if not viols:
        return result("stuck", name, "pass", detail="no bot was stuck or oscillating")
    return result("stuck", name, "fail", len(viols), viols[0][0],
                  "%d stuck episode(s)" % len(viols), [v[1] for v in viols])


# -- in_fire ------------------------------------------------------------------

def oracle_in_fire(trace, cfg):
    name = "standing in a ground effect"
    if not has_cap(trace, "in_fire"):
        return result("in_fire", name, "na", detail="the producer does not record ground effects")
    hold = cfg["fire_hold_ms"]
    viols = []
    for guid in sorted(trace.party):
        def burning(frame, guid=guid):
            bot = next((b for b in frame.get("bots", []) if b.get("g") == guid), None)
            return bool(bot and bot.get("al") and bot.get("fr"))

        for start, end, first, _ in runs_of(trace.frames, burning):
            if end - start >= hold:
                spell = next(b for b in first["bots"] if b.get("g") == guid).get("fr")
                viols.append((start, end, guid, spell))
    viols.sort()
    if not viols:
        return result("in_fire", name, "pass", detail="no bot stood in a harmful ground effect for %.1f s" % (hold / 1000.0))
    examples = ["%s in spell %s for %.1fs at %s" % (trace.name(g), sp, (e - s) / 1000.0, fmt_t(s))
                for s, e, g, sp in viols]
    return result("in_fire", name, "fail", len(viols), viols[0][0],
                  "%d stretch(es) in a ground effect" % len(viols), examples)


ORACLES = (oracle_action_loop, oracle_threat_off_tank, oracle_idle_caster,
           oracle_deaths, oracle_oom_healer, oracle_rest_failure,
           oracle_facing, oracle_self_interrupt, oracle_stuck, oracle_in_fire)


def score(trace, overrides=None):
    cfg = dict(DEFAULTS)
    cfg.update(overrides or {})
    return [oracle(trace, cfg) for oracle in ORACLES]


def overall(results):
    return "fail" if any(r["verdict"] == "fail" for r in results) else "pass"


# -- summary ------------------------------------------------------------------

def summary_path_for(trace_path):
    base = trace_path[:-len(".jsonl")] if trace_path.endswith(".jsonl") else trace_path
    return base + ".summary.json"


def update_summary(path, trace, results, trace_path):
    """Write the verdicts into <run>.summary.json, keeping every other key."""
    summary = {}
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as fh:
                summary = json.load(fh)
        except (OSError, ValueError):
            summary = {}
    if not isinstance(summary, dict) or not summary:
        header = trace.header or {}
        counters = (trace.end or {}).get("counters", {})
        summary = {
            "v": 1, "schema": "bt-trace", "run": trace.run, "src": header.get("src", "server"),
            "status": "complete" if trace.complete else "incomplete",
            "end_reason": (trace.end or {}).get("reason", ""),
            "scope": header.get("scope", ""), "scope_name": header.get("scope_name", ""),
            "trace_file": os.path.basename(trace_path),
            "start_unix": header.get("start_unix", 0), "counters": counters,
        }
    summary["verdict"] = overall(results)
    summary["scored_unix"] = int(time.time())
    summary["scorer"] = SCORER
    summary["trace_complete"] = trace.complete
    summary["bad_lines"] = trace.bad_lines
    summary["oracles"] = [
        {k: r[k] for k in ("id", "name", "verdict", "count", "first_ms", "detail") if k in r}
        for r in results
    ]
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(summary, fh, separators=(",", ":"))
        fh.write("\n")
    os.replace(tmp, path)
    return summary


def parse_overrides(pairs):
    out = {}
    for pair in pairs or []:
        key, _, value = pair.partition("=")
        if key not in DEFAULTS:
            raise SystemExit("unknown threshold %r (known: %s)" % (key, ", ".join(sorted(DEFAULTS))))
        try:
            out[key] = int(value)
        except ValueError:
            raise SystemExit("threshold %s needs an integer, got %r" % (key, value))
    return out


def trace_files(target):
    if os.path.isdir(target):
        return sorted(os.path.join(target, f) for f in os.listdir(target) if f.endswith(".jsonl"))
    return [target]


def print_report(trace, results, path):
    print("%s  (%s, %d frames, %d actions, %d events%s)" % (
        trace.run or os.path.basename(path), "complete" if trace.complete else "INCOMPLETE",
        len(trace.frames), len(trace.actions), len(trace.events),
        ", %d unreadable line(s)" % trace.bad_lines if trace.bad_lines else ""))
    for r in results:
        print("  %-16s %-5s %s" % (r["id"], r["verdict"].upper(), r["detail"]))
        for ex in r.get("examples", []):
            print("      - " + ex)
    print("  verdict: %s" % overall(results).upper())


def main(argv=None):
    ap = argparse.ArgumentParser(description="Score a bt-trace v1 run trace.")
    ap.add_argument("trace", help="a .jsonl trace file, or a directory of them")
    ap.add_argument("--no-write", action="store_true", help="do not update <run>.summary.json")
    ap.add_argument("--json", action="store_true", help="print the results as JSON")
    ap.add_argument("--set", action="append", metavar="KEY=VALUE", help="override a threshold")
    ap.add_argument("--fail-on-violation", action="store_true", help="exit 1 when any oracle fails")
    args = ap.parse_args(argv)
    overrides = parse_overrides(args.set)

    files = trace_files(args.trace)
    if not files:
        print("no traces found in %s" % args.trace, file=sys.stderr)
        return 2
    failed = False
    out = []
    for path in files:
        try:
            trace = load_trace(path)
        except OSError as exc:
            print("cannot read %s: %s" % (path, exc), file=sys.stderr)
            return 2
        results = score(trace, overrides)
        failed = failed or overall(results) == "fail"
        if not args.no_write:
            update_summary(summary_path_for(path), trace, results, path)
        if args.json:
            out.append({"run": trace.run, "verdict": overall(results), "complete": trace.complete,
                        "oracles": results})
        else:
            print_report(trace, results, path)
    if args.json:
        print(json.dumps(out[0] if len(out) == 1 else out, indent=2))
    return 1 if (failed and args.fail_on_violation) else 0


if __name__ == "__main__":
    sys.exit(main())
