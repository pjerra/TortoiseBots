#!/usr/bin/env python3
"""The Wailing Caverns rows the module ships are the rows the generator writes.

  python3 forever/tools/test_wc_plan_rows.py        (no server, no core)

It regenerates the plan from the same inputs `wc_plan_rows.py` reads (the navmesh-checked Turtle route and the map 43 spawns)
and checks: the committed migration is byte-identical to the generated one (a hand-edited row fails here), the counts, the
required bosses and that every pull row's entries are spawnable or known-summoned, and that each unit row stands on a spawn
of its entry (which is what `.tb plan check` matches).
"""
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATOR = os.path.join(ROOT, 'tools', 'wc_plan_rows.py')
MIGRATION = os.path.join(ROOT, 'sql', 'world', '2026_11_20_12_tortoisebots_plan_wailing_caverns.sql')

failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print('FAIL: %s' % what)


def load():
    spec = importlib.util.spec_from_file_location('wc_plan_rows', GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    wc = load()
    route = wc.parse_route(wc.DEFAULT_ROUTE, wc.PLAN)
    check(len(route) > 250, 'the Turtle route has its rows (%d)' % len(route))
    legs = wc.base_legs(route)
    legs += wc.event_legs(len(legs), legs)
    spawns = wc.load_spawns(wc.DEFAULT_SPAWNS)
    units = wc.unit_rows(legs, spawns)
    bosses = wc.boss_rows(legs)
    steps = [s for _, _, rows in legs for s in rows]

    check(len(legs) == 33, 'legs = 33 (%d)' % len(legs))
    check(len(steps) > 450, 'steps > 450 (%d)' % len(steps))
    check(len(units) > 150, 'unit rows > 150 (%d)' % len(units))
    check(bosses == [3653, 3669, 3670, 3674, 3673, 5775, 3654], 'boss rows in walk order: %s' % bosses)
    kinds = {}
    for s in steps:
        kinds[s.kind] = kinds.get(s.kind, 0) + 1
    check(kinds.get('gossip') == 1, 'exactly one gossip row')
    check(kinds.get('hold', 0) >= 6, 'the event holds (%d)' % kinds.get('hold', 0))
    check(kinds.get('move_hop_exact') == 2, 'the two hop rows (%d)' % kinds.get('move_hop_exact', 0))
    check(kinds.get('pull', 0) > 80, 'the pulls (%d)' % kinds.get('pull', 0))

    by_entry = {}
    for spawn in spawns:
        by_entry.setdefault(spawn['entry'], []).append(spawn)
    summoned = {5762, 5763}      # the Naralex event's own summons: no spawn rows
    for s in steps:
        if s.kind != 'pull':
            continue
        for entry in [int(e) for e in s.entries.split(',') if e.strip()]:
            check(entry in by_entry or entry in summoned, 'pull entry %d has spawns or is a known summon' % entry)
    # A unit row stands on a spawn of its entry (the check's 5 yd match), never on another entry's.
    for u in units:
        near = [s for s in by_entry.get(u['entry'], [])
                if abs(s['x'] - u['x']) < 0.01 and abs(s['y'] - u['y']) < 0.01 and abs(s['z'] - u['z']) < 0.01]
        check(bool(near) or u['role'] == 'boss', 'unit row entry %d at %.1f %.1f is a spawn' % (u['entry'], u['x'], u['y']))
    # The event: the gate, the gossip and Mutanus at the script's summon point.
    holds = [s for s in steps if s.kind == 'hold' and (s.flags & 8)]
    check(any(h.entry == 4 and h.param == 4 for h in holds), 'the Disciple gate reads data 4 = 4')
    check(any(h.entry == 5 and h.param == 3 for h in holds), 'Mutanus ends data 5 = 3')
    mut = [s for s in steps if s.kind == 'boss' and s.entry == 3654]
    check(len(mut) == 1 and abs(mut[0].x - 142.7) < 0.01 and abs(mut[0].y - 254.0) < 0.01, 'Mutanus at the script position')
    gossips = [s for s in steps if s.kind == 'gossip']
    check(gossips and gossips[0].entry == 3678 and gossips[0].param == 1, 'the Disciple gossip (menu line 1)')

    header = '%d legs, %d steps, %d unit rows, %d bosses' % (len(legs), len(steps), len(units), len(bosses))
    generated = wc.emit_sql(legs, units, bosses, header)
    check(os.path.exists(MIGRATION), 'the migration exists')
    if os.path.exists(MIGRATION):
        shipped = open(MIGRATION, encoding='utf-8').read()
        check(shipped == generated, 'the committed migration equals the generator output (rows not hand-set)')
    print('test_wc_plan_rows: %d checks, %d failed' % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
