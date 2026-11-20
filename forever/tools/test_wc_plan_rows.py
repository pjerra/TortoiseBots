#!/usr/bin/env python3
"""The Wailing Caverns rows the module ships are the rows the generator writes.

  python3 forever/tools/test_wc_plan_rows.py        (no server, no core)

It regenerates the plan from the same inputs `wc_plan_rows.py` reads (the proven
ForeverCore route in data/wc-route-forevercore.tsv and the map 43 spawns) and
checks: the committed migration is byte-identical to the generated one (a
hand-edited row fails here), the counts, the route stays inside the ForeverCore
spawns (no Turtle-only geometry), every back leg starts where the previous one
ended (the way back is the route walked again, not a gap), the one-way ramp hop
is the exact/hop pair, Kresh carries the skip-unreachable bit, the escort holds
carry kPlanFlagEscortNpc, there is no data-5 wait (the pinned core never sets
it), and every unit row stands on a spawn of its entry (what `.tb plan check`
matches).
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
    route = wc.parse_route(wc.DEFAULT_ROUTE)
    check(len(route) == 126, 'the proven route has its 126 steps (%d)' % len(route))
    legs = wc.base_legs(route)
    legs += wc.event_legs(len(legs), legs)
    spawns = wc.load_spawns(wc.DEFAULT_SPAWNS)
    units = wc.unit_rows(legs, spawns)
    bosses = wc.boss_rows(legs)
    steps = [s for _, _, rows in legs for s in rows]

    check(len(legs) == 31, 'legs = 31 (%d)' % len(legs))
    check(len(steps) == 302, 'steps = 302 (%d)' % len(steps))
    check(len(units) == 193, 'unit rows = 193 (the ForeverCore spawns, %d)' % len(units))
    check(bosses == [3653, 3669, 3670, 3674, 3673, 5775, 3654], 'boss rows in walk order: %s' % bosses)
    kinds = {}
    for s in steps:
        kinds[s.kind] = kinds.get(s.kind, 0) + 1
    check(kinds.get('gossip') == 1, 'exactly one gossip row')
    check(kinds.get('hold', 0) == 5, 'the gate hold and the four escort holds (%d)' % kinds.get('hold', 0))
    check(kinds.get('move_hop_exact') == 2, 'the two hop rows (%d)' % kinds.get('move_hop_exact', 0))
    check(kinds.get('pull', 0) == 87, 'the pulls (%d)' % kinds.get('pull', 0))

    # The route stays inside the ForeverCore map: its own spawns end at x -374 and no Turtle-only leg is kept.
    xs = [s.x for s in steps if s.kind in ('move', 'move_hop_exact', 'pull', 'boss')]
    check(min(xs) >= -374.0, 'no route point west of x = -374 (min %.1f)' % min(xs))
    names = [name for _, name, _ in legs]
    check('vangros' not in names and 'zandara' not in names, 'the Turtle-only legs are absent: %s' % names)
    check(names[:13] == ['anacondra_a', 'anacondra_b', 'cobrahn_a', 'kresh', 'cobrahn_b', 'cobrahn_c', 'pythas_a',
                         'pythas_b', 'pythas_c', 'skum', 'west_column', 'serpentis', 'verdan'], 'base leg order')

    # The way back is the route walked again: every back leg starts at the point the previous back leg ended at.
    backs = [(name, rows) for _, name, rows in legs if name.startswith('back_')]
    check(len(backs) == 13, 'the way back has its 13 legs (%d)' % len(backs))
    for i in range(1, len(backs)):
        prev_end = backs[i - 1][1][-1]
        first = backs[i][1][0]
        check(abs(prev_end.x - first.x) < 0.01 and abs(prev_end.y - first.y) < 0.01 and abs(prev_end.z - first.z) < 0.01,
              '%s starts where %s ended (%.1f %.1f vs %.1f %.1f)' % (backs[i][0], backs[i - 1][0], first.x, first.y,
                                                                    prev_end.x, prev_end.y))
    hops = [s for s in steps if s.kind == 'move_hop_exact']
    for hop in hops:
        before = [s for _, _, rows in legs for s in rows if s.seq == hop.seq - 1 and s.leg == hop.leg]
        check(bool(before) and (before[0].flags & wc.FLAG_EXACT), 'a move_hop_exact follows an exact move (leg %d seq %d)' % (hop.leg, hop.seq))

    # Kresh: the skip-unreachable bit, so a patroller elsewhere moves the run on (logged).
    kresh = [s for s in steps if s.kind == 'boss' and s.entry == wc.KRESH]
    check(len(kresh) == 1 and (kresh[0].flags & wc.FLAG_SKIP_UNREACHABLE) and kresh[0].timeout == 300,
          'Kresh carries flags 4096 and the 300 s away-wait')

    # The event: the gate, the gossip, the escort holds with the escort-NPC flag, Mutanus; no data-5 wait.
    holds = [s for s in steps if s.kind == 'hold']
    check(any(h.entry == 4 and h.param == 4 for h in holds), 'the Disciple gate reads data 4 = 4')
    check(not any(h.entry == 5 for h in holds), 'no data-5 hold (the pinned core never sets data 5)')
    escort = [h for h in holds if h.flags & wc.FLAG_ESCORT_NPC]
    check(len(escort) == 4, 'the four escort holds carry kPlanFlagEscortNpc (%d)' % len(escort))
    mut = [s for s in steps if s.kind == 'boss' and s.entry == 3654]
    check(len(mut) == 1 and abs(mut[0].x - 142.7) < 0.01 and abs(mut[0].y - 254.0) < 0.01, 'Mutanus at the script position')
    gossips = [s for s in steps if s.kind == 'gossip']
    check(gossips and gossips[0].entry == 3678 and gossips[0].param == 1, 'the Disciple gossip (menu line 1)')

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
