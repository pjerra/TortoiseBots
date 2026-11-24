#!/usr/bin/env python3
"""Tests of the TBF #159 plan files (forever/sql/world/2026_11_23_1x/2x_tortoisebots_plan_*.sql, written by
plan_rows_forever.py): the same match `.tb plan check` makes (TbPlanMatch.h, 5 yd matched, 15 yd near, one-to-one) run
against the committed spawn list of the plan's map must find EVERY unit row, and the rows must fit the plan tables'
columns (the live updater refuses a text longer than its column, MySQL error 1406).

usage: python3 forever/tools/test_plan_rows_forever.py        (exit 0 = PASS)"""
import glob
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import plan_rows_forever as P  # noqa: E402

ROOT = os.path.dirname(HERE)
FILES = sorted(glob.glob(os.path.join(ROOT, 'sql', 'world', '2026_11_23_[12]?_tortoisebots_plan_*.sql')))
WIDTH = {('ai_dungeon_plan', 'plan'): 32, ('ai_dungeon_plan', 'source'): 64, ('ai_dungeon_plan', 'comment'): 255,
         ('ai_dungeon_leg', 'name'): 32, ('ai_dungeon_leg', 'comment'): 255, ('ai_dungeon_step', 'kind'): 16,
         ('ai_dungeon_step', 'entries'): 255, ('ai_dungeon_step', 'comment'): 255}


def check(path):
    errs = []
    T = P.parse_sql(open(path).read())
    plan = T['ai_dungeon_plan'][1][0]
    for (t, c), w in WIDTH.items():
        for r in T.get(t, (None, []))[1]:
            if c in r and len(str(r[c])) > w:
                errs.append(f'{t}.{c} is {len(str(r[c]))} characters (column {w}): {str(r[c])[:40]}...')
    steps = {(s['leg'], s['seq']): s for s in T['ai_dungeon_step'][1]}
    units = T['ai_dungeon_step_unit'][1]
    for u in units:
        if (u['leg'], u['seq']) not in steps:
            errs.append(f"unit row leg {u['leg']} seq {u['seq']} has no step")
    legs = {l['leg'] for l in T['ai_dungeon_leg'][1]}
    for (leg, seq) in steps:
        if leg not in legs:
            errs.append(f'step leg {leg} seq {seq} has no leg row')
    for leg in legs:
        seqs = sorted(s for (l, s) in steps if l == leg)
        if seqs != list(range(len(seqs))):
            errs.append(f'leg {leg}: steps are not numbered 0..{len(seqs) - 1}')
    spawns, _ = P.read_spawns(plan['map'])
    by_entry = {}
    for s in spawns:
        by_entry.setdefault(s['entry'], []).append(s)
    pairs = []
    for i, u in enumerate(units):
        if not by_entry.get(u['entry']):
            errs.append(f"unit row leg {u['leg']} seq {u['seq']} names {u['entry']}, which has no spawn on map {plan['map']}")
            continue
        for s in by_entry[u['entry']]:
            d = math.dist((u['x'], u['y'], u['z']), (s['x'], s['y'], s['z']))
            if d <= P.NEAR_YD:
                pairs.append((d, i, s['guid']))
    pairs.sort()
    got, used = {}, set()
    for d, i, g in pairs:
        if i not in got and g not in used:
            got[i] = d
            used.add(g)
    # a patrol row may share its own step's pack spawn (TbPlanMatch pass 3)
    for i, u in enumerate(units):
        if i in got or u['role'] != 'patrol':
            continue
        if any(j in got and v['role'] != 'patrol' and (v['leg'], v['seq'], v['entry']) == (u['leg'], u['seq'], u['entry'])
               and math.dist((u['x'], u['y'], u['z']), (v['x'], v['y'], v['z'])) <= P.NEAR_YD for j, v in enumerate(units)):
            got[i] = 0.0
    for i, u in enumerate(units):
        if i not in got and by_entry.get(u['entry']):
            errs.append(f"unit row leg {u['leg']} seq {u['seq']} idx {u['idx']} {u['role']} {u['entry']} matches no free spawn")
    for s in steps.values():
        c = str(s['comment'])
        if s['kind'] == 'move' and c.startswith('pull group ') and c.endswith(' approach'):
            group = c[:-len(' approach')]
            if not any(t['leg'] == s['leg'] and t['kind'] == 'pull' and str(t['comment']) == group for t in steps.values()):
                errs.append(f"approach move leg {s['leg']} seq {s['seq']} leads to {group}, which is not in the plan")
        if s['kind'] == 'pull' and not any((u['leg'], u['seq']) == (s['leg'], s['seq']) for u in units):
            errs.append(f"pull step leg {s['leg']} seq {s['seq']} has no unit row")
    if plan['plan'] == 'shadowfang_keep':
        # the courtyard gossip row the script adds (the exporter refuses gossip): Ashcrombe, right before the door's wait
        w = [t for t in steps.values() if t['kind'] == 'wait_object' and t['entry'] == 18895]
        g = w and steps.get((w[0]['leg'], w[0]['seq'] - 1))
        if not g or g['kind'] != 'gossip' or g['entry'] != 3850 or g['param'] != 1:
            errs.append('shadowfang_keep: no gossip 3850 (menu line 1) right before the Courtyard Door wait')
        # live run 5 paused on this stables yard point: it is off this world's navmesh
        if any(t['kind'] == 'move' and abs(t['x'] + 260.549) < 0.01 and abs(t['y'] - 2283.210) < 0.01 for t in steps.values()):
            errs.append('shadowfang_keep: leg 5 still walks to -260.5 2283.2 (off the navmesh)')
        # live runs 2 and 6 paused on this gallery ledge point: no navmesh route reaches it
        if any(t['kind'] == 'move' and abs(t['x'] + 233.333) < 0.01 and abs(t['y'] - 2133.333) < 0.01 and abs(t['z'] - 83.402) < 0.05
               for t in steps.values()):
            errs.append('shadowfang_keep: a leg still walks to the gallery ledge -233.3 2133.3 83.4 (no navmesh route)')
    return plan['plan'], len(units), errs


def test_reanchor_rules():
    """The re-anchor on a made-up map: a pack row 8 yd off takes its spawn; a repeated patrol is dropped; a Turtle-only
    entry is dropped; a pack row 22 yd off is re-anchored (15 < d <= 30)."""
    spawns = [dict(guid=1, entry=10, x=0, y=0, z=0, move=0, path=0), dict(guid=2, entry=11, x=50, y=0, z=0, move=2, path=7),
              dict(guid=3, entry=10, x=100, y=0, z=0, move=0, path=0)]
    paths = {7: [(50, 0, 0), (60, 0, 0)]}
    units = [dict(leg=0, seq=1, idx=0, entry=10, x=8, y=0, z=0, role='pack'),
             dict(leg=0, seq=1, idx=1, entry=11, x=61, y=0, z=0, role='patrol'),
             dict(leg=0, seq=2, idx=0, entry=11, x=61, y=0, z=0, role='patrol'),
             dict(leg=0, seq=2, idx=1, entry=99, x=0, y=0, z=0, role='pack'),
             dict(leg=0, seq=3, idx=0, entry=10, x=122, y=0, z=0, role='pack')]
    log = []
    kept, absent, _ = P.reanchor(dict(map=1), [], units, spawns, paths, log)
    pos = {(u['leg'], u['seq'], u['entry']): (u['x'], u['y']) for u in kept}
    assert pos == {(0, 1, 10): (0, 0), (0, 1, 11): (50, 0), (0, 3, 10): (100, 0)}, pos
    assert absent == {99}, absent
    assert any('same patrol' in l for l in log) and any('re-anchored' in l for l in log), log


def main():
    test_reanchor_rules()
    print('re-anchor rules: PASS')
    bad = 0
    if not FILES:
        print('FAIL: no plan files found')
        return 1
    for f in FILES:
        name, n, errs = check(f)
        print(f"{name}: {n} unit rows, {'PASS' if not errs else 'FAIL'}")
        for e in errs[:10]:
            print('   ', e)
        bad += bool(errs)
    print('PASS' if not bad else f'FAIL ({bad} plans)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
