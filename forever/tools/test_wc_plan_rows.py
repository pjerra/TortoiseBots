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
it), that every unit row stands on a spawn of its entry (what `.tb plan check`
matches), and that every generated text field fits the varchar the plan tables'
own schema gives it (the live updater refused the row with error 1406 when one
did not).
"""
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATOR = os.path.join(ROOT, 'tools', 'wc_plan_rows.py')
MIGRATION = os.path.join(ROOT, 'sql', 'world', '2026_11_20_12_tortoisebots_plan_wailing_caverns.sql')
SCHEMA = os.path.join(ROOT, 'sql', 'world', '2026_11_20_00_tortoisebots_plans.sql')
FIXTURE = os.path.join(ROOT, 'data', 'wc-agent-route-fixture.tsv')

failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print('FAIL: %s' % what)


def schema_text_widths():
    """table -> {column: characters} for every varchar of the plan tables' own CREATE TABLE."""
    text = open(SCHEMA, encoding='utf-8').read()
    widths = {}
    for table, body in re.findall(r'CREATE TABLE IF NOT EXISTS `(\w+)` \((.*?)\n\) ENGINE', text, re.S):
        widths[table.lower()] = {c.lower(): int(n) for c, n in re.findall(r'`(\w+)` varchar\((\d+)\)', body)}
    return widths


def split_values(row):
    """One INSERT row's values as ('text'|'number', value): quoted strings ('' escapes) and bare numbers."""
    out = []
    i, n = 0, len(row)
    while i < n:
        while i < n and row[i] in ' ,':
            i += 1
        if i >= n:
            break
        if row[i] == "'":
            i += 1
            buf = []
            while i < n:
                if row[i] == "'":
                    if i + 1 < n and row[i + 1] == "'":
                        buf.append("'")
                        i += 2
                        continue
                    i += 1
                    break
                buf.append(row[i])
                i += 1
            out.append(('text', ''.join(buf)))
        else:
            j = i
            while j < n and row[j] != ',':
                j += 1
            out.append(('number', row[i:j].strip()))
            i = j
    return out


def inserts(sql):
    """Every INSERT of the generated plan as (table, columns, [(kind, value), ...])."""
    for table, cols, body in re.findall(r'INSERT INTO `(\w+)` \(([^)]*)\) VALUES\n(.*?);\n', sql, re.S):
        columns = [c.strip().strip('`') for c in cols.split(',')]
        for row in re.findall(r'^\((.*)\),?$', body, re.M):
            yield table, columns, split_values(row)


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
    wet = wc.load_wet_hops(wc.DEFAULT_WET)
    check(wc.apply_wade_flags(legs, wet) == len(wet), 'the generator sets the wade bit on every wet hop')
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

    # N2: the base rows are the agent's public route. The fixture is the hand-copied needed rows (its legs' `to` moves, its
    # pull-group centroids and its boss positions, forever/data/wc-agent-route-fixture.tsv), never a capture of a run.
    fixture = []
    for line in open(FIXTURE, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        parts = line.rstrip('\n').split('\t')
        fixture.append(dict(leg=int(parts[0]), name=parts[1], kind=parts[2], x=float(parts[3]), y=float(parts[4]),
                            z=float(parts[5]), entry=parts[6]))
    check(len(fixture) == 106, 'the fixture has the 106 needed rows (%d)' % len(fixture))
    fixture_names = []
    for f in fixture:
        if f['name'] not in fixture_names:
            fixture_names.append(f['name'])
    check(fixture_names == [name for _, name, _ in legs[:13]], 'the fixture legs are the plan\'s base legs in order')
    base = legs[:13]
    for f in fixture:
        if f['leg'] >= len(base):
            check(False, 'fixture leg %d is one of the 13 base legs' % f['leg'])
            continue
        near = [s for s in base[f['leg']][2] if s.kind == f['kind'] and
                abs(s.x - f['x']) <= 0.15 and abs(s.y - f['y']) <= 0.15 and abs(s.z - f['z']) <= 0.15]
        check(bool(near), 'fixture %s row %d %s at %.1f %.1f is in the plan' % (f['name'], f['leg'], f['kind'], f['x'], f['y']))
    base_pulls = [s for _, _, rows in base for s in rows if s.kind == 'pull']
    base_moves = [s for _, _, rows in base for s in rows if s.kind == 'move' and s.seq == 0]
    base_bosses = [s for _, _, rows in base for s in rows if s.kind == 'boss']
    check(len(base_pulls) == 87 and len(base_moves) == 13 and len(base_bosses) == 6,
          'the base legs hold the fixture\'s 87 pulls, 13 leg moves and 6 bosses (%d/%d/%d)' % (len(base_pulls), len(base_moves), len(base_bosses)))

    # The wade bit: only the hops the mmaps probe found wet carry it, and those all do.
    check(len(wet) == 21, 'the probe\'s wet hops are the 21 of forever/data/wc-wet-hops.tsv (%d)' % len(wet))
    flagged = [s for _, _, rows in legs for s in rows if s.flags & wc.FLAG_WADE_OK]
    check(len(flagged) == len(wet), 'every wet hop\'s row carries the wade bit and no other row does (%d/%d)' % (len(flagged), len(wet)))
    by_key = {(w['leg'], w['seq']): w for w in wet}
    prev = None
    flagged_keys = set()
    for _, _, rows in legs:
        for s in rows:
            w = by_key.get((s.leg, s.seq))
            if prev is not None and w is not None and w['kind'] == s.kind and all(
                    abs(a - b) <= 0.01 for a, b in ((prev[0], w['fx']), (prev[1], w['fy']), (prev[2], w['fz']),
                                                    (s.x, w['tx']), (s.y, w['ty']), (s.z, w['tz']))):
                check(bool(s.flags & wc.FLAG_WADE_OK), 'wet hop leg %d seq %d carries the wade bit' % (s.leg, s.seq))
                flagged_keys.add((s.leg, s.seq))
            prev = (s.x, s.y, s.z)
    check(len(flagged_keys) == len(wet), 'each wet hop matched a row (%d/%d)' % (len(flagged_keys), len(wet)))
    for s in flagged:
        check(s.kind in wc.WADE_KINDS, 'the wade bit is on a move, pull or boss row only (leg %d seq %d %s)' % (s.leg, s.seq, s.kind))
        check((s.leg, s.seq) in flagged_keys, 'no wade bit on a hop the probe did not list (leg %d seq %d)' % (s.leg, s.seq))

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

    header = '%d legs, %d steps, %d unit rows, %d bosses, %d wade hops' % (len(legs), len(steps), len(units), len(bosses), len(wet))
    generated = wc.emit_sql(legs, units, bosses, header)

    # Every text the generator writes fits its column: the schema is the plan tables' own CREATE TABLE (varchar sizes), read once here.
    widths = schema_text_widths()
    check(widths.get('ai_dungeon_plan', {}).get('source') == 64, 'the schema gives ai_dungeon_plan.source varchar(64)')
    check(len(wc.PLAN_SOURCE) <= widths.get('ai_dungeon_plan', {}).get('source', 0),
          'the plan source text fits its column (%d chars)' % len(wc.PLAN_SOURCE))
    rows_checked = 0
    for table, columns, values in inserts(generated):
        check(table in widths, 'the insert target %s is a plan table' % table)
        check(len(columns) == len(values), '%s row has %d values for %d columns' % (table, len(values), len(columns)))
        for column, (kind, value) in zip(columns, values):
            if kind != 'text':
                continue
            limit = widths.get(table, {}).get(column)
            check(limit is not None and len(value) <= limit,
                  '%s.%s text is %d chars, the column holds %s' % (table, column, len(value), limit))
        rows_checked += 1
    check(rows_checked > 300, 'every generated row was checked (%d)' % rows_checked)

    check(os.path.exists(MIGRATION), 'the migration exists')
    if os.path.exists(MIGRATION):
        shipped = open(MIGRATION, encoding='utf-8').read()
        check(shipped == generated, 'the committed migration equals the generator output (rows not hand-set)')
    print('test_wc_plan_rows: %d checks, %d failed' % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
