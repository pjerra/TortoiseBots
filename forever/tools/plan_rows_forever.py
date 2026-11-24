#!/usr/bin/env python3
"""Re-anchor an exported dungeon plan to THIS world's spawns (TBF #159).

The agent's plan files (forever-agent `dungeons/<plan>.json`) were written on Turtle WoW. `plan-export --forever` turns
them into rows for this module, but the unit rows keep the Turtle positions, and `.tb plan check` matches unit rows to
the pinned world DB's creature spawns (forever/src/host/run/TbPlanMatch.h: 5 yd matched, 15 yd near, one-to-one).
This script reads the exporter's SQL and the map's spawn list (forever/data/plans/spawns-map<N>.tsv, written from the
pinned TDB) and writes the same plan with every unit row on a spawn of this world:

  1. pack, boss and first rows: each takes the nearest free spawn of its entry within 15 yd (the check's own rule,
     nearest pairs first, route order breaks ties);
  2. patrol rows: a patrol row that repeats an earlier step's patrol (same entry, same point) is dropped: the creature is
     killed at the first step that names it, and two rows cannot both match one spawn. The others take the free
     patroller of their entry whose spawn point or waypoint path passes nearest to the row (within 25 yd); a patrol row
     the module itself pairs with a pack row of its own step (TbPlanMatch pass 3) is left as it is;
  3. a pack row still without a spawn takes the nearest free spawn of its entry within 30 yd (logged with the distance);
  4. a row whose entry has NO spawn on this map (a Turtle-only creature) is dropped, and the entry leaves the step's
     `entries`; any other row still without a spawn is dropped and logged;
  5. a pull step left with no unit row is dropped (the pack is not in this world) and the leg's steps are renumbered.

Every kept row is written at its spawn's exact position, so the live check reads it as `matched`. Per-plan fixes (doors,
moved boss points) are applied after that from PLAN_FIXES below; each one says why. The report goes to stderr.

usage: python3 forever/tools/plan_rows_forever.py --sql <exported plan_<plan>.sql> --out <file.sql> [--report <file.md>]
"""
import argparse
import math
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'data', 'plans')

MATCH_YD = 5.0
NEAR_YD = 15.0
PATROL_YD = 25.0
REANCHOR_YD = 30.0

# ------------------------------------------------------------------------------------------------------------- parsing

def split_vals(s):
    vals, i, n = [], 0, len(s)
    while i < n:
        while i < n and s[i] in ' ,':
            i += 1
        if i >= n:
            break
        if s[i] == "'":
            i += 1
            b = []
            while True:
                if s[i] == "'" and i + 1 < n and s[i + 1] == "'":
                    b.append("'")
                    i += 2
                elif s[i] == "'":
                    i += 1
                    break
                else:
                    b.append(s[i])
                    i += 1
            vals.append(b and ''.join(b) or '')
        else:
            j = i
            while j < n and s[j] != ',':
                j += 1
            t = s[i:j].strip()
            i = j
            vals.append(float(t) if '.' in t else int(t))
    return vals


def parse_sql(text):
    """{table: (columns, [row dict])} for every INSERT of the exporter's format (one row a line)."""
    out = {}
    for m in re.finditer(r"INSERT INTO `(\w+)` \(([^)]*)\) VALUES\n(.*?)(?:;\n|\nON DUPLICATE)", text, re.S):
        t = m.group(1)
        cols = [c.strip(' `') for c in m.group(2).split(',')]
        rows = out.setdefault(t, (cols, []))[1]
        for line in m.group(3).split('\n'):
            line = line.strip().rstrip(',')
            if line.startswith('('):
                rows.append(dict(zip(cols, split_vals(line[1:-1]))))
    return out


def read_spawns(mp):
    spawns, paths = [], defaultdict(list)
    with open(os.path.join(DATA, f'spawns-map{mp}.tsv')) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            g, e, x, y, z, mt, kind, path = line.rstrip('\n').split('\t')
            if kind == 'creature':
                spawns.append(dict(guid=int(g), entry=int(e), x=float(x), y=float(y), z=float(z), move=int(mt), path=int(path)))
    pf = os.path.join(DATA, f'paths-map{mp}.tsv')
    if os.path.exists(pf):
        with open(pf) as f:
            for line in f:
                if line.startswith('#') or not line.strip():
                    continue
                p, n, x, y, z = line.split('\t')
                paths[int(p)].append((float(x), float(y), float(z)))
    return spawns, paths


def d3(a, b):
    return math.dist((a['x'], a['y'], a['z']), (b['x'], b['y'], b['z']))

# ------------------------------------------------------------------------------------------------------------- re-anchor

def reanchor(plan, steps, units, spawns, paths, log):
    by_entry = defaultdict(list)
    for s in spawns:
        by_entry[s['entry']].append(s)
    order = sorted(range(len(units)), key=lambda i: (units[i]['leg'], units[i]['seq'], units[i]['idx']))
    got = {}            # unit index -> spawn
    used = set()
    drop = {}           # unit index -> why

    def greedy(rows, limit, cand, how):
        pairs = []
        for i in rows:
            for s in cand(units[i]):
                d = how(units[i], s)
                if d <= limit:
                    pairs.append((round(d, 4), order.index(i), s['guid'], i, s))
        pairs.sort(key=lambda p: p[:3])
        for d, _, g, i, s in pairs:
            if i in got or i in drop or g in used:
                continue
            got[i] = s
            used.add(g)

    # 4 first: entries with no spawn at all on this map
    for i in order:
        if not by_entry.get(units[i]['entry']):
            drop[i] = f"entry {units[i]['entry']} has no spawn on map {plan['map']} (not in this world)"
    # 2a: repeated patrols
    seen = {}
    for i in order:
        u = units[i]
        if u['role'] != 'patrol' or i in drop:
            continue
        key = (u['entry'], round(u['x'], 1), round(u['y'], 1), round(u['z'], 1))
        if key in seen and seen[key] != (u['leg'], u['seq']):
            drop[i] = f"the same patrol as leg {seen[key][0]} seq {seen[key][1]} (killed there)"
        else:
            seen.setdefault(key, (u['leg'], u['seq']))
    # 1: pack, boss, first
    greedy([i for i in order if units[i]['role'] != 'patrol' and i not in drop], NEAR_YD,
           lambda u: by_entry[u['entry']], d3)

    # 2b: patrol rows against patrollers' spawn points and paths
    def path_dist(u, s):
        best = d3(u, s)
        for (x, y, z) in paths.get(s['path'], []):
            best = min(best, math.dist((u['x'], u['y'], u['z']), (x, y, z)))
        return best
    greedy([i for i in order if units[i]['role'] == 'patrol' and i not in drop], PATROL_YD,
           lambda u: [s for s in by_entry[u['entry']] if s['move'] == 2 or s['path']], path_dist)
    greedy([i for i in order if units[i]['role'] == 'patrol' and i not in drop], NEAR_YD,
           lambda u: by_entry[u['entry']], d3)
    # 2c: a patrol the module pairs with its own step's pack row (TbPlanMatch pass 3)
    shared = set()
    for i in order:
        u = units[i]
        if u['role'] != 'patrol' or i in got or i in drop:
            continue
        for j in order:
            v = units[j]
            if j != i and j in got and (v['leg'], v['seq'], v['entry']) == (u['leg'], u['seq'], u['entry']) and v['role'] != 'patrol' \
                    and d3(u, got[j]) <= NEAR_YD:
                shared.add(i)
                break
    # 3: pack rows still without a spawn, wider
    greedy([i for i in order if units[i]['role'] != 'patrol' and i not in got and i not in drop], REANCHOR_YD,
           lambda u: by_entry[u['entry']], d3)
    for i in order:
        if i in got and d3(units[i], got[i]) > NEAR_YD:
            log.append(f"  re-anchored leg {units[i]['leg']} seq {units[i]['seq']} {units[i]['role']} {units[i]['entry']}: "
                       f"{d3(units[i], got[i]):.1f} yd to spawn {got[i]['guid']}")
    for i in order:
        if i not in got and i not in drop and i not in shared:
            near = min((d3(units[i], s) for s in by_entry[units[i]['entry']] if s['guid'] not in used), default=None)
            drop[i] = (f"no free spawn of {units[i]['entry']} within {REANCHOR_YD if units[i]['role'] != 'patrol' else PATROL_YD:.0f} yd"
                       + (f" (nearest free {near:.1f} yd)" if near is not None else " (every spawn is taken)"))

    kept = []
    for i in order:
        u = dict(units[i])
        if i in drop:
            log.append(f"  dropped unit leg {u['leg']} seq {u['seq']} idx {u['idx']} {u['role']} {u['entry']}: {drop[i]}")
            continue
        if i in got:
            s = got[i]
            u['x'], u['y'], u['z'] = s['x'], s['y'], s['z']
            u['_spawn'] = s['guid']
        kept.append(u)
    absent = {units[i]['entry'] for i in drop if drop[i].startswith('entry ')}
    return kept, absent, used

# ------------------------------------------------------------------------------------------------------------- per-plan fixes

def step_at(steps, leg, seq):
    for s in steps:
        if s['leg'] == leg and s['seq'] == seq:
            return s
    raise KeyError((leg, seq))


def insert_step(steps, units, leg, seq, row):
    """Insert a step before (leg, seq); later steps of the leg and their unit rows move up by one."""
    for s in steps:
        if s['leg'] == leg and s['seq'] >= seq:
            s['seq'] += 1
    for u in units:
        if u['leg'] == leg and u['seq'] >= seq:
            u['seq'] += 1
    r = {c: 0 for c in steps[0]}
    r.update(dict(plan=steps[0]['plan'], leg=leg, seq=seq, entries='', comment=''), **row)
    steps.append(r)
    steps.sort(key=lambda s: (s['leg'], s['seq']))


def renumber_leg(steps, units, leg):
    """Steps of the leg 0.. again in their order; the unit rows follow their step."""
    ls = sorted([s for s in steps if s['leg'] == leg], key=lambda s: s['seq'])
    remap = {s['seq']: n for n, s in enumerate(ls)}
    for s in ls:
        s['seq'] = remap[s['seq']]
    for u in units:
        if u['leg'] == leg:
            u['seq'] = remap[u['seq']]


def fix_deadmines(steps, units, log):
    """The in-game checks of this world's Deadmines (client 70959, a real player's spot checks, see
    forever/data/waterys/deadmines-doors.csv): no lever opens a door here; the doors open on a click,
    the Iron Clad Door is blown open by the Defias Cannon (16398, needs the Defias Gunpowder 5397)."""
    # The Mast Room Door (16400) after Sneed and the Foundry Door (16399) after Gilnid: the core links them to the boss's
    # death, and a player clicks them open in this world. A click on an open door does nothing, so an `open` before the
    # wait covers both: a door the death already opened, and one it did not.
    for door, (x, y, z), what in ((16400, (-290.294, -536.960, 49.435), "Mast Room Door, after Sneed"),
                                  (16399, (-168.514, -579.861, 19.316), "Foundry Door, after Gilnid")):
        w = next(s for s in steps if s['kind'] == 'wait_object' and s['entry'] == door)
        insert_step(steps, units, w['leg'], w['seq'], dict(kind='open', entry=door, x=x, y=y, z=z,
                    comment=f'open: the {what}: a player click opens it in this world (client check)'))
        log.append(f"  fix: open {door} ({what}) before its wait (doors open on click in this world)")


def fix_shadowfang_keep(steps, units, log):
    """The courtyard door gossip. The exporter refuses the plan's gossip step (the forever format has no gossip field), so
    the export is made from the plan with that step taken out (forever/docs/PLANS-159-NOTES.md) and the row is put back
    here, in the module's own gossip row form (the Wailing Caverns plan's Disciple row: entry = the NPC, param = the menu
    line, 1-based). The plan is the Horde one (Deathstalker Adamant 3849, faction 68): this world's test party is
    Alliance, so the row names the Alliance prisoner, Sorcerer Ashcrombe 3850 (faction 12, spawn 20016262), menu 301
    line 1. Both have the same SmartAI: the gossip starts his walk to the door (smart_scripts 950000305 / 950000306)."""
    # Leg 0 pull group 2: one Tormented Officer (3873) on the wall above the entrance (z 97). On this world's navmesh the way
    # up is the whole keep: the exporter's approach hops walk through the courtyard (its door shut until Ashcrombe's gossip,
    # leg 4), round the keep and back, ~95 move rows. Live run 1 (2026-11-23) fought the courtyard packs on that walk
    # (4 deaths) and paused at step 18 (off the mesh in the courtyard). The pull and its walk there and back go; the officer
    # stands next to the keep route the later legs walk.
    p2 = next((s for s in steps if s['leg'] == 0 and s['kind'] == 'pull' and s['comment'] == 'pull group 2'), None)
    if p2:
        pulls = sorted((s for s in steps if s['leg'] == 0 and s['kind'] in ('pull', 'boss')), key=lambda s: s['seq'])
        i = pulls.index(p2)
        lo, hi = pulls[i - 1]['seq'], pulls[i + 1]['seq']
        gone = [s for s in steps if s['leg'] == 0 and lo < s['seq'] < hi]
        assert all(s is p2 or (s['kind'] == 'move' and s['comment'] == 'approach path') for s in gone), 'leg 0 between pulls 1 and 3'
        for s in gone:
            steps.remove(s)
        units[:] = [u for u in units if not (u['leg'] == 0 and u['seq'] == p2['seq'])]
        renumber_leg(steps, units, 0)
        log.append(f"  fix: leg 0 pull group 2 (3873 on the wall, z 97) and its {len(gone) - 1} approach moves through the keep dropped")
    # Leg 4 walks from Springvale back to the cells; its last path points drop off the entrance gallery (z 87 -> 83 -> 81 -> 77)
    # and climb back to the cells (z 81): Turtle's navmesh, not this world's. Live run 2 paused there ("no navmesh route
    # reaches" -233.3 2133.3 83.4). The run walks every move on the navmesh, so the points between the gallery and the
    # leg's stand go and the lead bot finds its own way down.
    to4 = next(s for s in steps if s['leg'] == 4 and s['kind'] == 'move' and s['comment'] == 'to')
    gal = next(s for s in steps if s['leg'] == 4 and s['kind'] == 'move' and abs(s['x'] + 234.667) < 0.01
               and abs(s['y'] - 2133.333) < 0.01 and s['z'] > 86.9)
    drop4 = [s for s in steps if s['leg'] == 4 and gal['seq'] < s['seq'] < to4['seq']]
    assert drop4 and all(s['kind'] == 'move' for s in drop4)
    for s in drop4:
        steps.remove(s)
    renumber_leg(steps, units, 4)
    log.append(f"  fix: leg 4: {len(drop4)} path points off the entrance gallery (Turtle's navmesh) dropped before the cells")
    w = next(s for s in steps if s['kind'] == 'wait_object' and s['entry'] == 18895)
    insert_step(steps, units, w['leg'], w['seq'], dict(kind='gossip', entry=3850, x=-240.904, y=2122.550, z=81.263, param=1,
                timeout_s=60, comment='talk to 3850 Sorcerer Ashcrombe, menu 301 line 1 (Alliance party): he walks to the Courtyard Door'))
    w['comment'] = ("wait for object 18895 active or gone: the Courtyard Door, after Ashcrombe's gossip (he walks to it; "
                    "the Horde party's prisoner is Deathstalker Adamant 3849)")
    log.append("  fix: gossip 3850 Sorcerer Ashcrombe before the Courtyard Door wait (the exporter refuses gossip; Alliance party)")
    # Legs 5 and 6 walk round the stables yard to the stairs (leg 5 pull group 3 -> 4, and leg 6 to Odo); leg 5 comes back
    # (7 -> 8). On the way out one point, -260.5 2283.2 80.3, is off this world's navmesh (live run 5, 2026-11-24: paused
    # at step 121 "not on walkable ground"); the way back passes the same spot at -234.1 2272.0 78.0. Each way out takes
    # the way back's point.
    out5 = [s for s in steps if s['kind'] == 'move' and abs(s['x'] + 260.549) < 0.01 and abs(s['y'] - 2283.210) < 0.01]
    back5 = [s for s in steps if s['leg'] == 5 and s['kind'] == 'move' and abs(s['x'] + 234.118) < 0.01
             and abs(s['y'] - 2271.990) < 0.01]
    assert sorted(s['leg'] for s in out5) == [5, 6] and len(back5) == 1, 'stables yard points'
    for s in out5:
        s.update(x=back5[0]['x'], y=back5[0]['y'], z=back5[0]['z'])
    log.append("  fix: legs 5 and 6: the stables yard point off the navmesh (-260.5 2283.2) takes leg 5's way back point")
    # The same ledge point off the entrance gallery (-233.3 2133.3 83.4, Turtle's navmesh) is on leg 5's way back down and
    # leg 6's way up (live run 6, 2026-11-24: paused at step 184, "no navmesh route reaches it"). The points on either
    # side (z 81.4 and z 87) are walkable, so only the ledge point goes.
    ledge = [s for s in steps if s['kind'] == 'move' and abs(s['x'] + 233.333) < 0.01 and abs(s['y'] - 2133.333) < 0.01
             and abs(s['z'] - 83.402) < 0.05]
    assert sorted(s['leg'] for s in ledge) == [5, 6], 'gallery ledge points'
    for s in ledge:
        steps.remove(s)
    renumber_leg(steps, units, 5)
    renumber_leg(steps, units, 6)
    log.append("  fix: legs 5 and 6: the gallery ledge point -233.3 2133.3 83.4 (Turtle's navmesh) dropped")


# Creatures nobody can fight in this world (unit_flags IMMUNE_TO_PC 256): as pack members a pull would wait for a kill that
# cannot happen. They leave the unit rows and every entry list, like a creature that is not in this world.
NEVER_KILLABLE = {
    10000: "Arugal on Shadowfang Keep's courtyard wall, an event NPC (live run 3, 2026-11-23: leg 5 pull group 3 paused "
           "twice, 'a fight lasted more than 180 s (kept on by nothing hostile engaged)')",
}

# Bosses with no creature row that this world's scripts bring in: their boss steps stay.
EVENT_BOSSES = {
    10813: "Balnazzar: Grand Crusader Dathrohan (10812) turns into him in the fight (Stratholme)",
}

PLAN_FIXES = {
    'deadmines': fix_deadmines,
    'shadowfang_keep': fix_shadowfang_keep,
}

# ------------------------------------------------------------------------------------------------------------- writing

def sql_val(v):
    if isinstance(v, str):
        return "'" + v.replace("'", "''") + "'"
    if isinstance(v, float):
        return f'{v:.3f}'
    return str(v)


def write(path, header, plan, P, steps, units):
    name = plan['plan']
    L = []
    L += header
    L.append(f"-- {name}: map {plan['map']}, {len(P['ai_dungeon_leg'][1])} legs, {len(steps)} steps, "
             f"{sum(1 for s in steps if s['kind'] == 'pull')} pull groups, {len(units)} unit rows")
    L.append('START TRANSACTION;')
    for t in ('ai_dungeon_step_unit', 'ai_dungeon_leg_need', 'ai_dungeon_step', 'ai_dungeon_leg', 'ai_dungeon_plan'):
        if t in P or t in ('ai_dungeon_step_unit', 'ai_dungeon_step', 'ai_dungeon_leg', 'ai_dungeon_plan'):
            L.append(f"DELETE FROM `{t}` WHERE `plan` = '{name}';")
    for t, rows in (('ai_dungeon_plan', [plan]), ('ai_dungeon_leg', P['ai_dungeon_leg'][1]),
                    ('ai_dungeon_leg_need', P.get('ai_dungeon_leg_need', (None, []))[1]),
                    ('ai_dungeon_step', steps), ('ai_dungeon_step_unit', units)):
        if not rows:
            continue
        cols = P[t][0]
        L.append(f"INSERT INTO `{t}` (" + ', '.join(f'`{c}`' for c in cols) + ') VALUES')
        L.append(',\n'.join('(' + ', '.join(sql_val(r[c]) for c in cols) + ')' for r in rows) + ';')
    if 'ai_dungeon_boss' in P:
        cols, rows = P['ai_dungeon_boss']
        L.append(f"INSERT INTO `ai_dungeon_boss` (" + ', '.join(f'`{c}`' for c in cols) + ') VALUES')
        L.append(',\n'.join('(' + ', '.join(sql_val(r[c]) for c in cols) + ')' for r in rows))
        L.append("ON DUPLICATE KEY UPDATE `name` = IF(VALUES(`name`) <> '', VALUES(`name`), `name`);")
    L.append('COMMIT;')
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sql', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--source', default='', help='the plan row source text (at most 64 characters)')
    a = ap.parse_args()
    text = open(a.sql).read()
    header = [l for l in text.split('\n') if l.startswith('--') and not re.match(r'-- \w+: map ', l)]
    header = header[:header.index('-- Tables: ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step, ai_dungeon_step_unit, ai_dungeon_boss.') + 1] \
        if '-- Tables: ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step, ai_dungeon_step_unit, ai_dungeon_boss.' in header else header
    header = ['-- TBF #159: re-anchored to this world by forever/tools/plan_rows_forever.py from the export below: every unit row',
              '-- stands on a spawn of the pinned world DB (forever/data/plans/spawns-map<N>.tsv); see the script for the rules.'] + header + ['']
    P = parse_sql(text)
    plan = dict(P['ai_dungeon_plan'][1][0])
    if a.source:
        plan['source'] = a.source[:64]
    steps = [dict(s) for s in P['ai_dungeon_step'][1]]
    units = [dict(u) for u in P['ai_dungeon_step_unit'][1]]
    spawns, paths = read_spawns(plan['map'])
    log = [f"{plan['plan']} (map {plan['map']}): {len(steps)} steps, {len(units)} unit rows in; {len(spawns)} creature spawns on the map"]
    units, absent, used = reanchor(plan, steps, units, spawns, paths, log)
    for e, why in NEVER_KILLABLE.items():
        if any(u['entry'] == e for u in units):
            units = [u for u in units if u['entry'] != e]
            absent.add(e)
            log.append(f"  dropped the unit rows of {e}: {why}")
    # entries that are not in this world leave the steps' lists
    for s in steps:
        if s['entries']:
            es = [e for e in str(s['entries']).split(',') if e.strip()]
            keep = [e for e in es if int(e) not in absent]
            if keep != es:
                log.append(f"  leg {s['leg']} seq {s['seq']} {s['kind']}: entries {s['entries']} -> {','.join(keep) or '(none)'}")
                s['entries'] = ','.join(keep)
    # a boss step for a boss with no spawn on this map: an event boss stays (its script summons it), any other one is a
    # Turtle-only boss and its step goes (the leg's packs stay: they are this world's)
    spawned = {s['entry'] for s in spawns}
    for s in list(steps):
        if s['kind'] == 'boss' and s['entry'] not in spawned:
            if s['entry'] in EVENT_BOSSES:
                log.append(f"  kept boss step leg {s['leg']} seq {s['seq']}: {s['entry']} has no spawn row: {EVENT_BOSSES[s['entry']]}")
            else:
                log.append(f"  dropped boss step leg {s['leg']} seq {s['seq']}: boss {s['entry']} has no spawn on this map (a Turtle-only boss)")
                units = [u for u in units if (u['leg'], u['seq']) != (s['leg'], s['seq'])]
                steps.remove(s)
    for k in ('kill_first', 'cc_priority', 'cc_always'):
        if plan.get(k):
            es = [e for e in str(plan[k]).split(',') if e.strip() and int(e) not in absent]
            plan[k] = ','.join(es)
    # pull steps with no unit row left
    have = {(u['leg'], u['seq']) for u in units}
    gone = [s for s in steps if s['kind'] == 'pull' and (s['leg'], s['seq']) not in have]
    for s in gone:
        log.append(f"  dropped pull step leg {s['leg']} seq {s['seq']} at {s['x']:.1f} {s['y']:.1f} {s['z']:.1f} ({s['entries']}): none of its pack is in this world")
        steps.remove(s)
        # the exporter's approach moves of that pull group (--approach-hops: comment "pull group N approach") go with it:
        # they lead to the pack's Turtle place (Deadmines live run 1 paused on one: "no walkable path toward ... -186 -533")
        m = re.match(r'pull group (\d+)$', str(s['comment']))
        if m:
            for move in [t for t in steps if t['leg'] == s['leg'] and t['kind'] == 'move'
                      and str(t['comment']) == f'pull group {m.group(1)} approach']:
                log.append(f"  dropped approach move leg {move['leg']} seq {move['seq']} at {move['x']:.1f} {move['y']:.1f} {move['z']:.1f}: its pull group {m.group(1)} is gone")
                steps.remove(move)
    # renumber
    for leg in sorted({s['leg'] for s in steps}):
        ls = sorted([s for s in steps if s['leg'] == leg], key=lambda s: s['seq'])
        remap = {s['seq']: n for n, s in enumerate(ls)}
        for s in ls:
            s['seq'] = remap[s['seq']]
        for u in units:
            if u['leg'] == leg:
                u['seq'] = remap[u['seq']]
    fix = PLAN_FIXES.get(plan['plan'])
    if fix:
        fix(steps, units, log)
    # idx again 0.. inside each step
    units.sort(key=lambda u: (u['leg'], u['seq'], u['idx']))
    n = defaultdict(int)
    for u in units:
        u['idx'] = n[(u['leg'], u['seq'])]
        n[(u['leg'], u['seq'])] += 1
    steps.sort(key=lambda s: (s['leg'], s['seq']))
    for u in units:
        u.pop('_spawn', None)
    write(a.out, header, plan, P, steps, units)
    log.append(f"  out: {len(steps)} steps, {len(units)} unit rows -> {a.out}")
    print('\n'.join(log), file=sys.stderr)


if __name__ == '__main__':
    main()
