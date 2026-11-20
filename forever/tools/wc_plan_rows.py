#!/usr/bin/env python3
"""Build the ForeverCore rows of the Wailing Caverns plan (`ai_dungeon_*`, map 43).

The route is not re-invented here: every move, pull and boss row of the base
dungeon comes from the navmesh-checked rows of the Turtle plan
(`data/sql/world/20261025002700_world.sql`, turtle-agent 16a864d, "209 move hops,
209 complete, 0 incomplete, 0 refused"). The earlier ForeverCore export of the
same plan (2026_11_20_10) dropped most of the path rows, and its long straight
moves crossed the river in the Cobrahn cave (a live run paused there).

What this script adds, from the core's own data:

  * the Kresh leg as the ForeverCore export had it (boss row at his third
    waypoint, core guid 20027368 path node 3);
  * the Naralex event after Verdan: the way back (the base legs' moves in
    reverse, the one-way ramp hop taken down), the Disciple's gate (instance
    data 4 special), the gossip, the escort road with a units hold at each
    summoned wave, and Mutanus (no creature row on the map: the script summons
    him at 142.7 254 -102.2);
  * the unit rows of every pull (each pack member at its spawn position, from a
    dump of map 43's `creature` rows), which is what `.tb plan check` matches.

The emitted SQL is idempotent: it deletes the plan's own rows child first and
inserts them parent first.

Usage:
  python3 forever/tools/wc_plan_rows.py --out forever/sql/world/2026_11_20_12_tortoisebots_plan_wailing_caverns.sql
  python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv        # plan/leg/step/unit/boss rows for a test
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROUTE = os.path.join(ROOT, '..', 'data', 'sql', 'world', '20261025002700_world.sql')
DEFAULT_SPAWNS = os.path.join(ROOT, 'data', 'wc-map43-spawns.tsv')

PLAN = 'wailing_caverns'
MAP = 43

# The Disciple of Naralex (npc 3678) and his script (classic_wailing_caverns.cpp,
# ClassicDiscipleWaypoints): the points the escort pauses at are 7, 15 and 30.
DISCIPLE = 3678
DISCIPLE_AT = (-134.965, 125.402, -78.0945)
DISCIPLE_STAND = (-134.0, 128.5, -78.1)
MUTANUS = 3654
MUTANUS_AT = (142.7, 254.0, -102.2)
RITUAL_STAND = (114.6, 236.9, -96.0)
DATA_DISCIPLE = 4            # CWC_TYPE_DISCIPLE
DATA_MUTANUS = 5             # CWC_TYPE_MUTANUS
ENCOUNTER_SPECIAL = 4        # EncounterState: 4 special (the four Fanglords are down)
ENCOUNTER_IN_PROGRESS = 1
ENCOUNTER_DONE = 3

KRESH = 3653
KRESH_AT = (-3.641, 229.268, -106.189)
KRESH_TO = (-18.0, 234.0, -104.5)

# Turtle leg number -> Forever leg name, in the order the legs are walked.
BASE_LEGS = [
    (0, 'anacondra_a'),
    (1, 'anacondra_b'),
    (2, 'cobrahn_a'),      # cut at seq 12: the Kresh leg stands in between
    (3, 'cobrahn_c'),
    (4, 'cobrahn_d'),
    (5, 'pythas_a'),
    (6, 'pythas_b'),
    (7, 'pythas_c'),
    (9, 'skum'),
    (10, 'west_column'),
    (12, 'serpentis'),
    (13, 'verdan'),
]
# Turtle leg 2 is cut at its first row of seq 12: rows 0..11 are cobrahn_a, then the Kresh leg stands at
# its `to` point, then the rest of leg 2 is cobrahn_b.
COBRAHN_CUT = 12
KRESH_PULL_SEQ = 15          # Turtle leg 2's own pull of Kresh (his spawn): the boss leg replaces it

# The Disciple's road (script_waypoint 3678, the core's ClassicDiscipleWaypoints), point ids 1..30.
ROAD = {
    1: (-125.6, 132.9, -78.4), 2: (-113.8, 139.2, -80.9), 3: (-109.8, 157.5, -80.2),
    4: (-108.6, 175.2, -79.7), 5: (-108.6, 195.4, -80.6), 6: (-111.0, 219.0, -86.5),
    7: (-102.4, 232.8, -91.5), 8: (-82.4, 224.8, -93.5), 9: (-73.4, 214.7, -93.2),
    10: (-67.7, 208.0, -93.3), 11: (-43.3, 205.2, -96.3), 12: (-34.6, 221.3, -95.8),
    13: (-32.5, 238.5, -93.5), 14: (-42.1, 258.6, -92.8), 15: (-54.0, 276.2, -92.8),
    16: (-48.6, 287.5, -92.4), 17: (-47.2, 296.0, -90.8), 18: (-35.6, 309.0, -89.7),
    19: (-23.5, 311.3, -88.6), 20: (-8.6, 302.3, -87.4), 21: (-1.2, 293.2, -85.5),
    22: (10.3, 279.2, -85.8), 23: (23.1, 264.6, -86.6), 24: (31.9, 251.4, -87.6),
    25: (43.3, 233.0, -87.6), 26: (52.2, 208.7, -89.5), 27: (78.7, 208.8, -92.8),
    28: (88.3, 225.2, -94.4), 29: (98.7, 239.0, -95.8), 30: (114.6, 236.9, -96.0),
}

BOSS_NAMES = {
    3653: 'Kresh',
    3669: 'Lord Cobrahn',
    3670: 'Lord Pythas',
    3673: 'Lord Serpentis',
    3674: 'Skum',
    5775: 'Verdan the Everliving',
    3654: 'Mutanus the Devourer',
}


class Step:
    def __init__(self, leg, seq, kind, entry=0, x=0.0, y=0.0, z=0.0, radius=0.0, item=0, param=0, flags=0, timeout=0,
                 entries='', comment=''):
        self.leg = leg
        self.seq = seq
        self.kind = kind
        self.entry = entry
        self.x = float(x)
        self.y = float(y)
        self.z = float(z)
        self.radius = float(radius)
        self.item = item
        self.param = param
        self.flags = flags
        self.timeout = timeout
        self.entries = entries
        self.comment = comment


def parse_route(path, plan):
    txt = open(path, encoding='utf-8', errors='replace').read()
    row = re.compile(
        r"\('" + plan + r"', (\d+), (\d+), '([a-z_]+)', (\d+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), ([\d.]+), "
        r"(\d+), (\d+), (\d+), (\d+), '([^']*)'(?:, '((?:[^']|'')*)')?\)")
    steps = []
    for m in row.finditer(txt):
        steps.append(Step(int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), float(m.group(5)),
                          float(m.group(6)), float(m.group(7)), float(m.group(8)), int(m.group(9)), int(m.group(10)),
                          int(m.group(11)), int(m.group(12)), m.group(13), (m.group(14) or '').replace("''", "'")))
    return steps


def keep(step):
    """A base row this plan walks: everything but Kresh's own pull (a boss leg replaces it)."""
    return not (step.leg == 2 and step.seq == KRESH_PULL_SEQ)


def base_legs(route):
    """The Forever base legs, in walk order: leg names, steps (renumbered)."""
    out = []
    leg_no = 0
    for turtle_leg, name in BASE_LEGS:
        rows = [s for s in route if s.leg == turtle_leg and keep(s)]
        if turtle_leg == 2:
            # cobrahn_a (rows before the cut) is its own leg; cobrahn_b (the rest) comes after the Kresh leg.
            first = [s for s in rows if s.seq < COBRAHN_CUT]
            rest = [s for s in rows if s.seq >= COBRAHN_CUT]
            out.append((leg_no, 'cobrahn_a', renumber(first, leg_no)))
            leg_no += 1
            out.append((leg_no, 'kresh', kresh_leg(leg_no)))
            leg_no += 1
            out.append((leg_no, 'cobrahn_b', renumber(rest, leg_no)))
        else:
            out.append((leg_no, name, renumber(rows, leg_no)))
        leg_no += 1
    return out


def renumber(rows, leg):
    out = []
    for seq, s in enumerate(rows):
        step = Step(leg, seq, s.kind, s.entry, s.x, s.y, s.z, s.radius, s.item, s.param, s.flags, s.timeout,
                    s.entries, s.comment)
        out.append(step)
    return out


def kresh_leg(leg):
    """The boss row the ForeverCore export chose: his third path node, 300 s (a 525.9 yd loop at the walk speed)."""
    return [
        Step(leg, 0, 'move', x=KRESH_TO[0], y=KRESH_TO[1], z=KRESH_TO[2], comment='to'),
        Step(leg, 1, 'boss', entry=KRESH, x=KRESH_AT[0], y=KRESH_AT[1], z=KRESH_AT[2], timeout=300,
             comment='boss (his waypoint 3 of the loop)'),
    ]


def reverse_leg(rows, leg):
    """The same walk backwards, with the ramp hop taken down.

    The base Serpentis leg reaches the upper part of the map by an exact foot point and a
    `move_hop_exact` up to the ramp lip; backwards the lip is the exact point and the hop lands
    on the foot. Pulls and bosses are left out (the party killed them on the way in).
    """
    moves = [s for s in rows if s.kind in ('move', 'move_hop_exact')]
    hop = [i for i, s in enumerate(moves) if s.kind == 'move_hop_exact']
    out = []
    seq = 0

    def add(kind, x, y, z, flags=0, comment=''):
        nonlocal seq
        out.append(Step(leg, seq, kind, 0, x, y, z, 0.0, 0, 0, flags, 0, '', comment))
        seq += 1

    if hop:
        i = hop[0]
        for s in reversed(moves[i + 1:]):
            add('move', s.x, s.y, s.z, comment='back')
        lip = moves[i]          # the forward hop's landing: the lip, now the launch point
        foot = moves[i - 1]     # the forward exact point: the foot, now the landing
        add('move', lip.x, lip.y, lip.z, flags=64, comment='the ramp lip (exact: the hop down starts here)')
        add('move_hop_exact', foot.x, foot.y, foot.z, comment='the hop down to the foot (no mesh link)')
        for s in reversed(moves[:i - 1]):
            add('move', s.x, s.y, s.z, comment='back')
    else:
        for s in reversed(moves):
            add('move', s.x, s.y, s.z, comment='back')
    return out


def road_move(leg, seq, point, comment='road'):
    x, y, z = ROAD[point]
    return Step(leg, seq, 'move', x=x, y=y, z=z, comment=comment)


def event_legs(first_leg, base):
    """The Naralex event: the way back, the Disciple's gate and gossip, the escort, Mutanus."""
    legs = []
    leg = first_leg
    # 1. the way back: the base legs in reverse order (verdan is where the party stands; serpentis first).
    for _, name, rows in reversed(base):
        if name == 'kresh':
            # Only its `to` move: the boss row is not walked back.
            rows = [s for s in rows if s.kind == 'move']
        if not rows:
            continue
        legs.append((leg, 'back_' + name, reverse_leg(rows, leg)))
        leg += 1
    # 2. to the Disciple's stand, the gate, the gossip.
    rows = [
        Step(leg, 0, 'move', x=DISCIPLE_STAND[0], y=DISCIPLE_STAND[1], z=DISCIPLE_STAND[2], comment='to the Disciple'),
        Step(leg, 1, 'hold', entry=DATA_DISCIPLE, x=DISCIPLE_AT[0], y=DISCIPLE_AT[1], z=DISCIPLE_AT[2], radius=20.0,
             param=ENCOUNTER_SPECIAL, flags=8, timeout=60,
             comment='the four Fanglords dead: data 4 reads 4 (special)'),
        Step(leg, 2, 'gossip', entry=DISCIPLE, x=DISCIPLE_AT[0], y=DISCIPLE_AT[1], z=DISCIPLE_AT[2], param=1,
             timeout=60, comment='menu 202 option 1: let the event begin'),
    ]
    legs.append((leg, 'disciple', rows))
    leg += 1
    # 3. the escort road, with a units hold at each summoned wave (script_waypoint 3678: stops 7, 15, 30).
    stages = [
        ('escort_ravagers', range(1, 8), [('3636', 40.0, 240, 7)]),
        ('escort_vipers', range(8, 16), [('5755', 40.0, 240, 15)]),
    ]
    for name, points, holds in stages:
        rows = []
        seq = 0
        for p in points:
            rows.append(Step(leg, seq, 'move', x=ROAD[p][0], y=ROAD[p][1], z=ROAD[p][2], comment='road'))
            seq += 1
        for entries, radius, timeout, stop in holds:
            x, y, z = ROAD[stop]
            rows.append(Step(leg, seq, 'hold', 0, x, y, z, radius, 0, 0, 0, timeout, entries,
                             'the wave at stop %d' % stop))
            seq += 1
        legs.append((leg, name, rows))
        leg += 1
    rows = []
    seq = 0
    for p in range(16, 31):
        rows.append(Step(leg, seq, 'move', x=ROAD[p][0], y=ROAD[p][1], z=ROAD[p][2], comment='road'))
        seq += 1
    rows.append(Step(leg, seq, 'hold', 0, RITUAL_STAND[0], RITUAL_STAND[1], RITUAL_STAND[2], 45.0, 0, 0, 0, 180,
                     '5762', 'the Moccasins of the ritual'))
    seq += 1
    rows.append(Step(leg, seq, 'hold', 0, RITUAL_STAND[0], RITUAL_STAND[1], RITUAL_STAND[2], 45.0, 0, 0, 0, 300,
                     '5763', 'the Nightmare Ectoplasm wave'))
    legs.append((leg, 'escort_ritual', rows))
    leg += 1
    rows = [
        Step(leg, 0, 'boss', entry=MUTANUS, x=MUTANUS_AT[0], y=MUTANUS_AT[1], z=MUTANUS_AT[2], timeout=300,
             comment='Mutanus'),
        Step(leg, 1, 'hold', entry=DATA_MUTANUS, x=RITUAL_STAND[0], y=RITUAL_STAND[1], z=RITUAL_STAND[2], radius=50.0,
             param=ENCOUNTER_DONE, flags=8, timeout=120, comment='Mutanus dead: data 5 reads 3'),
        Step(leg, 2, 'hold', entry=DATA_DISCIPLE, x=RITUAL_STAND[0], y=RITUAL_STAND[1], z=RITUAL_STAND[2], radius=50.0,
             param=ENCOUNTER_DONE, flags=8, timeout=120, comment='the event over: data 4 reads 3'),
    ]
    legs.append((leg, 'mutanus', rows))
    return legs


def load_spawns(path):
    spawns = []
    for line in open(path, encoding='utf-8'):
        parts = line.rstrip('\n').split('\t')
        if len(parts) < 6:
            continue
        spawns.append({'guid': parts[0], 'entry': int(parts[1]), 'x': float(parts[2]), 'y': float(parts[3]),
                       'z': float(parts[4]), 'movement': int(parts[5])})
    return spawns


def dist3(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def unit_rows(legs, spawns):
    """One `ai_dungeon_step_unit` row per pack member, at its spawn position (the nearest unused spawn of the entry).

    Pulls: every spawn of a listed entry within the row's radius, one row each. Bosses: the boss's own spawn at
    the row's point (role boss) and the nearest spawn of each `first` entry within 25 yd (role first).
    """
    used = set()
    rows = []
    for leg, name, steps in legs:
        for step in steps:
            if step.kind == 'pull':
                entries = [int(e) for e in step.entries.split(',') if e.strip()]
                members = []
                for entry in entries:
                    found = []
                    for spawn in spawns:
                        if spawn['guid'] in used or spawn['entry'] != entry:
                            continue
                        d = dist3((spawn['x'], spawn['y'], spawn['z']), (step.x, step.y, step.z))
                        # The check's own reach (`kNearYd` 15) is the pack's: a spawn farther is another pack's.
                        if d <= max(step.radius, 15.0):
                            found.append((d, spawn))
                    for _, spawn in sorted(found, key=lambda p: p[0])[:12]:
                        used.add(spawn['guid'])
                        members.append(spawn)
                for idx, spawn in enumerate(sorted(members, key=lambda s: (s['entry'], s['guid']))):
                    rows.append(dict(leg=leg, seq=step.seq, idx=idx, entry=spawn['entry'], x=spawn['x'], y=spawn['y'],
                                     z=spawn['z'], role='pack'))
            elif step.kind == 'boss':
                own = None
                for spawn in spawns:
                    if spawn['entry'] != step.entry or spawn['guid'] in used:
                        continue
                    d = dist3((spawn['x'], spawn['y'], spawn['z']), (step.x, step.y, step.z))
                    if own is None or d < own[0]:
                        own = (d, spawn)
                idx = 0
                if own:
                    used.add(own[1]['guid'])
                    rows.append(dict(leg=leg, seq=step.seq, idx=idx, entry=step.entry, x=own[1]['x'], y=own[1]['y'],
                                     z=own[1]['z'], role='boss'))
                    idx += 1
                for entry in (int(e) for e in step.entries.split(',') if e.strip()):
                    best = None
                    for spawn in spawns:
                        if spawn['guid'] in used or spawn['entry'] != entry:
                            continue
                        d = dist3((spawn['x'], spawn['y'], spawn['z']), (step.x, step.y, step.z))
                        if d > 25.0:
                            continue
                        if best is None or d < best[0]:
                            best = (d, spawn)
                    if best:
                        used.add(best[1]['guid'])
                        rows.append(dict(leg=leg, seq=step.seq, idx=idx, entry=entry, x=best[1]['x'], y=best[1]['y'],
                                         z=best[1]['z'], role='first'))
                        idx += 1
    return rows


def boss_rows(legs):
    """One ai_dungeon_boss row per boss step, with the name from the world data."""
    out = []
    for _, _, steps in legs:
        for step in steps:
            if step.kind == 'boss' and step.entry not in out:
                out.append(step.entry)
    return out


def sql_quote(text):
    return "'" + text.replace("'", "''") + "'"


def emit_sql(legs, units, bosses, header):
    lines = []
    lines.append('-- Wailing Caverns (map %d) for the run leader on ForeverCore: base legs, the Kresh boss leg,' % MAP)
    lines.append('-- the return hop and the Naralex event (the Disciple\'s gate and gossip, the escort waves, Mutanus).')
    lines.append('-- Generated by forever/tools/wc_plan_rows.py from the navmesh-checked Turtle rows of')
    lines.append('-- data/sql/world/20261025002700_world.sql and the map 43 spawns; do not hand-set a row.')
    lines.append('-- Idempotent: the plan\'s own rows are deleted child first and inserted parent first; the boss table')
    lines.append('-- is shared and only upserted.')
    lines.append('')
    lines.append('START TRANSACTION;')
    lines.append('DELETE FROM `ai_dungeon_step_unit` WHERE `plan` = %s;' % sql_quote(PLAN))
    lines.append('DELETE FROM `ai_dungeon_step` WHERE `plan` = %s;' % sql_quote(PLAN))
    lines.append('DELETE FROM `ai_dungeon_leg` WHERE `plan` = %s;' % sql_quote(PLAN))
    lines.append('DELETE FROM `ai_dungeon_plan` WHERE `plan` = %s;' % sql_quote(PLAN))
    lines.append('INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, '
                 '`comment`) VALUES')
    lines.append('(%s, %d, %s, %s, %s, %s, %s);' % (sql_quote(PLAN), MAP, sql_quote('3840'), sql_quote(''),
                                                sql_quote('3671'), sql_quote('turtle-agent 16a864d + core scripts'),
                                                sql_quote(header)))
    lines.append('INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES')
    leg_rows = []
    for leg, name, _ in legs:
        leg_rows.append('(%s, %d, %s, %s)' % (sql_quote(PLAN), leg, sql_quote(name), sql_quote('')))
    lines.append(',\n'.join(leg_rows) + ';')
    lines.append('INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, '
                 '`item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES')
    step_rows = []
    for leg, _, steps in legs:
        for s in steps:
            step_rows.append('(%s, %d, %d, %s, %d, %.3f, %.3f, %.3f, %.1f, %d, %d, %d, %d, %s, %s)' % (
                sql_quote(PLAN), leg, s.seq, sql_quote(s.kind), s.entry, s.x, s.y, s.z, s.radius, s.item, s.param,
                s.flags, s.timeout, sql_quote(s.entries), sql_quote(s.comment)))
    lines.append(',\n'.join(step_rows) + ';')
    lines.append('INSERT INTO `ai_dungeon_step_unit` (`plan`, `leg`, `seq`, `idx`, `entry`, `x`, `y`, `z`, `role`, '
                 '`comment`) VALUES')
    unit_lines = []
    for u in units:
        unit_lines.append('(%s, %d, %d, %d, %d, %.3f, %.3f, %.3f, %s, %s)' % (
            sql_quote(PLAN), u['leg'], u['seq'], u['idx'], u['entry'], u['x'], u['y'], u['z'], sql_quote(u['role']),
            sql_quote('')))
    lines.append(',\n'.join(unit_lines) + ';')
    lines.append('INSERT INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES')
    boss_lines = ['(%d, %d, %s)' % (MAP, entry, sql_quote(BOSS_NAMES.get(entry, ''))) for entry in bosses]
    lines.append(',\n'.join(boss_lines))
    lines.append('ON DUPLICATE KEY UPDATE `name` = IF(VALUES(`name`) <> \'\', VALUES(`name`), `name`);')
    lines.append('COMMIT;')
    return '\n'.join(lines) + '\n'


def emit_tsv(legs, units):
    """The test's input: the plan row (`P`), one line per step, then one per unit row (`U`); an empty `entries` is `-` (a TSV keeps no empty last field)."""
    out = ['P\t%d\t3840\t\t3671' % MAP]
    for leg, name, steps in legs:
        for s in steps:
            out.append('\t'.join([str(leg), name, str(s.seq), s.kind, str(s.entry), '%.3f' % s.x, '%.3f' % s.y,
                                  '%.3f' % s.z, '%.1f' % s.radius, str(s.item), str(s.param), str(s.flags),
                                  str(s.timeout), s.entries or '-']))
    for u in units:
        out.append('\t'.join(['U', str(u['leg']), str(u['seq']), str(u['idx']), str(u['entry']), '%.3f' % u['x'],
                              '%.3f' % u['y'], '%.3f' % u['z'], u['role']]))
    return '\n'.join(out) + '\n'


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--route', default=DEFAULT_ROUTE)
    ap.add_argument('--spawns', default=DEFAULT_SPAWNS)
    ap.add_argument('--out', default='')
    ap.add_argument('--tsv', default='')
    args = ap.parse_args(argv)
    route = parse_route(args.route, PLAN)
    if not route:
        print('no %s rows in %s' % (PLAN, args.route), file=sys.stderr)
        return 1
    legs = base_legs(route)
    legs += event_legs(len(legs), legs)
    spawns = load_spawns(args.spawns)
    units = unit_rows(legs, spawns)
    bosses = boss_rows(legs)
    steps = sum(len(s) for _, _, s in legs)
    header = '%d legs, %d steps, %d unit rows, %d bosses' % (len(legs), steps, len(units), len(bosses))
    if args.out:
        open(args.out, 'w', encoding='utf-8').write(emit_sql(legs, units, bosses, header))
        print('wrote %s: %s' % (args.out, header))
    if args.tsv:
        open(args.tsv, 'w', encoding='utf-8').write(emit_tsv(legs, units))
        print('wrote %s' % args.tsv)
    if not args.out and not args.tsv:
        print(header)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
