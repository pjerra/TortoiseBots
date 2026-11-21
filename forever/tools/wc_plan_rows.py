#!/usr/bin/env python3
"""Build the ForeverCore rows of the Wailing Caverns plan (`ai_dungeon_*`, map 43).

The route is not re-invented here: every base leg, pull and boss row comes from
`forever/data/wc-route-forevercore.tsv`, the route the ForeverCore server itself
proved (the public agent plan `dungeons/wailing_caverns.json` at forever-agent
109a2496, exported with `forever-agent plan-export --forever`; every waypoint was
confirmed by the server's own position in the live route walk,
forever-agent docs/forever/ROUTEWALK-NOTES.md: 3583.0 yd planned, 3582.3 walked,
every leg PASS, no NOPATH/STUCK/FELL/LIQUID). Nothing of the Turtle-only geometry
(west of x = -374, the dropped vangros/zandara legs) is used: the route never
goes west of x = -352.8, where ForeverCore's own spawns end.

What this script adds, from the core's own data:

  * the Kresh row's skip-unreachable bit (4096): a patroller elsewhere after the
    300 s away-wait moves the run on, logged, instead of pausing for good;
  * the Naralex event after Verdan: the way back (the base route's points in
    reverse, the one-way ramp hop walked as the live-probed lip -> foot pair),
    the Disciple's gate (instance data 4 = 4, set by the four Fanglords' own
    smart_scripts) and gossip (menu 202, line 1), the escort road with a units
    hold at each summoned wave, and Mutanus (no creature row on the map: the
    script summons him at 142.7 254 -102.2). The escort holds carry
    kPlanFlagEscortNpc (65536): a dead or gone Disciple stops the event with its
    name instead of timing out;
  * the unit rows of every pull (each pack member at its spawn position, from a
    dump of map 43's `creature` rows), which is what `.tb plan check` matches;
  * the wade bit (kPlanFlagWadeOk, 131072) on the rows whose own hop the mmaps
    probe found wet (`forever/data/wc-wet-hops.tsv`), so the rivers on the way
    can be waded where the water is shallow, and nowhere else.

No trailing "data 5 = 3" / "data 4 = 3" holds: on the pinned core Mutanus's own
smart_script sets BOSS STATE 4, not instance data 5 (`SMART_ACTION_SET_INST_DATA`
with type 1), so data 5 never reads 3 and neither does the Disciple's data 4; the
Mutanus boss step's own kill read is what ends the run.

The emitted SQL is idempotent: it deletes the plan's own rows child first and
inserts them parent first.

Usage:
  python3 forever/tools/wc_plan_rows.py --out forever/sql/world/2026_11_20_12_tortoisebots_plan_wailing_caverns.sql
  python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv        # plan/leg/step/unit/boss rows for a test
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROUTE = os.path.join(ROOT, 'data', 'wc-route-forevercore.tsv')
DEFAULT_SPAWNS = os.path.join(ROOT, 'data', 'wc-map43-spawns.tsv')
DEFAULT_WET = os.path.join(ROOT, 'data', 'wc-wet-hops.tsv')

PLAN = 'wailing_caverns'
MAP = 43

# The Disciple of Naralex (npc 3678) and his script (classic_wailing_caverns.cpp,
# ClassicDiscipleWaypoints): the points the escort pauses at are 7, 15 and 30.
DISCIPLE = 3678
DISCIPLE_STAND = (-134.0, 128.5, -78.1)
MUTANUS = 3654
MUTANUS_AT = (142.7, 254.0, -102.2)
RITUAL_STAND = (114.6, 236.9, -96.0)
DATA_DISCIPLE = 4            # CWC_TYPE_DISCIPLE
ENCOUNTER_SPECIAL = 4        # EncounterState: 4 special (the four Fanglords are down)

KRESH = 3653
KRESH_AT = (-3.641, 229.268, -106.189)
KRESH_TO = (-18.0, 234.0, -104.5)

FLAG_EXACT = 64
FLAG_SKIP_UNREACHABLE = 4096
FLAG_ESCORT_NPC = 65536       # DungeonPlanPolicy.h kPlanFlagEscortNpc
FLAG_WADE_OK = 131072         # DungeonPlanPolicy.h kPlanFlagWadeOk: the row's own hop may wade shallow water
WADE_KINDS = ('move', 'pull', 'boss')   # the kinds the loader accepts the wade flag on

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


def parse_route(path):
    """The proven route TSV: leg, name, seq, kind, entry, x, y, z, radius, flags, entries, comment (comments start with #)."""
    out = []
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) < 12:
            continue
        out.append(Step(int(parts[0]), int(parts[2]), parts[3], int(parts[4]), float(parts[5]), float(parts[6]),
                        float(parts[7]), float(parts[8]), 0, 0, int(parts[9]), int(parts[10]), parts[11],
                        '' if parts[12] == '-' else parts[12]))
    return out


def base_legs(route):
    """The proven base legs in their file order, with the Kresh boss row marked skip-unreachable."""
    legs = []
    order = []
    names = {}
    for s in route:
        if s.leg not in names:
            order.append(s.leg)
            names[s.leg] = None
    # The leg name is the second column; keep it with the step rows.
    for line in open(DEFAULT_ROUTE, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        parts = line.rstrip('\n').split('\t')
        if len(parts) > 1:
            names[int(parts[0])] = parts[1]
    for leg_no in order:
        rows = [s for s in route if s.leg == leg_no]
        if leg_no == 3 and rows and rows[1].kind == 'boss' and rows[1].entry == KRESH:
            rows[1].flags |= FLAG_SKIP_UNREACHABLE
            rows[1].comment = "boss (his waypoint 3 of the loop; skipped, not cleared, when he is alive elsewhere after the 300 s wait)"
        legs.append((leg_no, names[leg_no], rows))
    return legs


def reverse_leg(rows, leg):
    """The same walk backwards: every point the forward leg visits, in reverse (pulls and bosses are places too).

    The one-way ramp hop is walked as the live-probed pair: the lip (the forward hop's landing) is the exact launch point and the foot (the forward
    exact point) is the `move_hop_exact` destination. The hop was probed both ways on the live server (forever-agent
    dungeons/wailing_caverns-extra.json: 3 of 3 from the lip and 3 of 3 from the foot, 2026-10-22).
    """
    points = []
    for s in rows:
        if s.kind in ('move', 'move_hop_exact', 'pull', 'boss'):
            points.append(s)
    out = []
    seq = 0
    hop = [i for i, s in enumerate(points) if s.kind == 'move_hop_exact']
    if hop:
        i = hop[0]
        for s in reversed(points[i + 1:]):
            out.append(Step(leg, seq, 'move', x=s.x, y=s.y, z=s.z, comment='back'))
            seq += 1
        lip = points[i]          # the forward hop's landing: now the launch point
        foot = points[i - 1]     # the forward exact point: now the landing
        out.append(Step(leg, seq, 'move', x=lip.x, y=lip.y, z=lip.z, flags=FLAG_EXACT, comment='the ramp lip (exact: the hop down starts here)'))
        seq += 1
        out.append(Step(leg, seq, 'move_hop_exact', x=foot.x, y=foot.y, z=foot.z, comment='the hop down to the foot (no mesh link)'))
        seq += 1
        for s in reversed(points[:i - 1]):
            out.append(Step(leg, seq, 'move', x=s.x, y=s.y, z=s.z, comment='back'))
            seq += 1
    else:
        for s in reversed(points):
            out.append(Step(leg, seq, 'move', x=s.x, y=s.y, z=s.z, comment='back'))
            seq += 1
    return out


def event_legs(first_leg, base):
    """The Naralex event: the way back, the Disciple's gate and gossip, the escort, Mutanus."""
    legs = []
    leg = first_leg
    prev_end = None
    # 1. the way back: the base legs in reverse order (verdan is where the party stands).
    for _, name, rows in reversed(base):
        back = reverse_leg(rows, leg)
        if not back:
            continue
        if prev_end is not None and (abs(back[0].x - prev_end[0]) > 0.001 or abs(back[0].y - prev_end[1]) > 0.001):
            back.insert(0, Step(leg, 0, 'move', x=prev_end[0], y=prev_end[1], z=prev_end[2], comment='back (previous leg end)'))
            for seq, s in enumerate(back):
                s.seq = seq
        prev_end = (back[-1].x, back[-1].y, back[-1].z)
        legs.append((leg, 'back_' + name, back))
        leg += 1
    # 2. to the Disciple's stand, the gate, the gossip.
    rows = [
        Step(leg, 0, 'move', x=DISCIPLE_STAND[0], y=DISCIPLE_STAND[1], z=DISCIPLE_STAND[2], comment='to the Disciple'),
        Step(leg, 1, 'hold', entry=DATA_DISCIPLE, x=-134.965, y=125.402, z=-78.094, radius=20.0,
             param=ENCOUNTER_SPECIAL, flags=8, timeout=60,
             comment='the four Fanglords dead: data 4 reads 4 (special; set by the bosses own smart_scripts)'),
        Step(leg, 2, 'gossip', entry=DISCIPLE, x=-134.965, y=125.402, z=-78.094, param=1,
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
            rows.append(Step(leg, seq, 'hold', 0, x, y, z, radius, 0, 0, FLAG_ESCORT_NPC, timeout, entries,
                             'the wave at stop %d (a dead Disciple stops the event here)' % stop))
            seq += 1
        legs.append((leg, name, rows))
        leg += 1
    rows = []
    seq = 0
    for p in range(16, 31):
        rows.append(Step(leg, seq, 'move', x=ROAD[p][0], y=ROAD[p][1], z=ROAD[p][2], comment='road'))
        seq += 1
    # 300 s, not the wave's 240/180: the Disciple needs about 105 s for stops 15 -> 30 and 12 s more to the summon, and the party may
    # fight on the road meanwhile; a 180 s ceiling left about 60 s of margin and a slow road would pause a hold that was still coming.
    rows.append(Step(leg, seq, 'hold', 0, RITUAL_STAND[0], RITUAL_STAND[1], RITUAL_STAND[2], 45.0, 0, 0, FLAG_ESCORT_NPC,
                     300, '5762', 'the Moccasins of the ritual (a dead Disciple stops the event here; 300 s: the road is ~105 s and a fight adds time)'))
    seq += 1
    rows.append(Step(leg, seq, 'hold', 0, RITUAL_STAND[0], RITUAL_STAND[1], RITUAL_STAND[2], 45.0, 0, 0, FLAG_ESCORT_NPC,
                     300, '5763', 'the Nightmare Ectoplasm wave (a dead Disciple stops the event here)'))
    legs.append((leg, 'escort_ritual', rows))
    leg += 1
    # 4. Mutanus. No trailing data holds: on the pinned core his smart_script sets boss state 4 (type 1), not instance data 5, so data 5 never reads 3.
    rows = [
        Step(leg, 0, 'boss', entry=MUTANUS, x=MUTANUS_AT[0], y=MUTANUS_AT[1], z=MUTANUS_AT[2], timeout=300,
             comment='Mutanus (a script summons him; his death is the boss step own read)'),
    ]
    legs.append((leg, 'mutanus', rows))
    return legs


def load_wet_hops(path):
    """The probe's wet hops: leg, seq, kind, from xyz, to xyz (forever/data/wc-wet-hops.tsv, made on the server's mmaps)."""
    out = []
    for line in open(path, encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        parts = line.rstrip('\n').split('\t')
        if len(parts) < 9:
            continue
        out.append(dict(leg=int(parts[0]), seq=int(parts[1]), kind=parts[2],
                        fx=float(parts[3]), fy=float(parts[4]), fz=float(parts[5]),
                        tx=float(parts[6]), ty=float(parts[7]), tz=float(parts[8])))
    return out


def apply_wade_flags(legs, wet):
    """Sets ai::kPlanFlagWadeOk on the row of every hop the probe found wet, and on no other row.

    The hop is the row's own way: from the previous row's point (the row the party stands on before it) to this row's
    point. A listed hop that no longer matches its row by leg, seq, kind and both points sets nothing: a stale probe
    never flags a row it did not check. A row whose kind the loader does not take the flag on would be refused there;
    it is left unset and counted as unmatched.
    """
    by_key = {(w['leg'], w['seq']): w for w in wet}
    matched = 0
    prev = None
    for _, _, steps in legs:
        for s in steps:
            if prev is not None:
                w = by_key.get((s.leg, s.seq))
                if (w is not None and w['kind'] == s.kind and s.kind in WADE_KINDS and
                        all(abs(a - b) <= 0.01 for a, b in ((prev[0], w['fx']), (prev[1], w['fy']), (prev[2], w['fz']),
                                                            (s.x, w['tx']), (s.y, w['ty']), (s.z, w['tz'])))):
                    s.flags |= FLAG_WADE_OK
                    matched += 1
            prev = (s.x, s.y, s.z)
    return matched


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
    lines.append('-- Wailing Caverns (map %d) for the run leader on ForeverCore: the route the server proved, the Kresh boss leg,' % MAP)
    lines.append('-- the return hop and the Naralex event (the Disciple\'s gate and gossip, the escort waves, Mutanus).')
    lines.append('-- Generated by forever/tools/wc_plan_rows.py from forever/data/wc-route-forevercore.tsv (the forever-agent')
    lines.append('-- plan export of dungeons/wailing_caverns.json, every waypoint live-confirmed) and the map 43 spawns;')
    lines.append('-- do not hand-set a row.')
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
                                                sql_quote('3671'), sql_quote('forever-agent 109a2496 dungeons/wailing_caverns.json (route walk confirmed) + core scripts'),
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
    ap.add_argument('--wet', default=DEFAULT_WET)
    ap.add_argument('--out', default='')
    ap.add_argument('--tsv', default='')
    args = ap.parse_args(argv)
    route = parse_route(args.route)
    if not route:
        print('no route rows in %s' % args.route, file=sys.stderr)
        return 1
    legs = base_legs(route)
    legs += event_legs(len(legs), legs)
    wet = load_wet_hops(args.wet)
    if not wet:
        print('no wet hops in %s' % args.wet, file=sys.stderr)
        return 1
    applied = apply_wade_flags(legs, wet)
    if applied != len(wet):
        print('%d of %d wet hops did not match their row' % (len(wet) - applied, len(wet)), file=sys.stderr)
        return 1
    spawns = load_spawns(args.spawns)
    units = unit_rows(legs, spawns)
    bosses = boss_rows(legs)
    steps = sum(len(s) for _, _, s in legs)
    header = '%d legs, %d steps, %d unit rows, %d bosses, %d wade hops' % (len(legs), steps, len(units), len(bosses), applied)
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
