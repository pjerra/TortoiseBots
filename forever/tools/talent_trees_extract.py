#!/usr/bin/env python3
"""Extract the class talent trees the ForeverCore server validates against, from the client DB2 files.

The server learns talents through the trait system (TraitMgr::ValidateConfigForCommit), not Talent.db2. This writes
one row per trait node of each class tree, with what the build check needs: the tab (the host's split of the tree by
PosX, SliceSpecPolicy.h BuildNodeTabMap), the row and its spent-points gate (TraitCond via the node's groups), the
max rank (TraitNodeEntry), the Talent.db2 id whose spell maps to the node (how TbTalents.cpp resolves a build step;
empty for Forever-only talents) and the parent edges as the core keeps them (TraitEdge, minus the back edge of a pair
that points both ways, TraitMgr::Load).

    python3 forever/tools/talent_trees_extract.py --core <ForeverCore checkout> --db2 <dir with enUS/*.db2> \\
        --out forever/data/waterys/talent-trees-70959.csv

The DB2 reader is the core's own (tools/client_diff/wdc5.py), so the fields read the way the worldserver reads them.
"""
import argparse
import collections
import csv
import os
import sys

CLASSES = {1: 'warrior', 2: 'paladin', 3: 'hunter', 4: 'rogue', 5: 'priest', 7: 'shaman', 8: 'mage', 9: 'warlock',
           11: 'druid'}
TABLES = ['Talent', 'TalentTab', 'SpellName', 'SkillLine', 'SkillLineXTraitTree', 'TraitDefinition', 'TraitNodeEntry',
          'TraitNodeXTraitNodeEntry', 'TraitNode', 'TraitCond', 'TraitNodeGroupXTraitNode', 'TraitNodeGroupXTraitCond',
          'TraitEdge']
EDGE_REQUIRED = 3
EDGE_SUFFICIENT = 2


def load(core, db2_dir):
    sys.path.insert(0, os.path.join(core, 'tools', 'client_diff'))
    from wdc5 import Schemas, read_db2  # noqa: E402
    sch = Schemas(core)
    base = os.path.join(db2_dir, 'enUS') if os.path.isdir(os.path.join(db2_dir, 'enUS')) else db2_dir
    return {t: read_db2(os.path.join(base, t + '.db2'), sch)[0] for t in TABLES}


def build(db):
    nodes, tne, nx, defs = db['TraitNode'], db['TraitNodeEntry'], db['TraitNodeXTraitNodeEntry'], db['TraitDefinition']
    entries = collections.defaultdict(list)
    for r in sorted(nx.values(), key=lambda r: (r['TraitNodeID'], r['Index'])):
        entries[r['TraitNodeID']].append(r['TraitNodeEntryID'])
    groups = collections.defaultdict(list)
    for r in db['TraitNodeGroupXTraitNode'].values():
        groups[r['TraitNodeID']].append(r['TraitNodeGroupID'])
    gconds = collections.defaultdict(list)
    for r in db['TraitNodeGroupXTraitCond'].values():
        if r['TraitCondID'] in db['TraitCond']:
            gconds[r['TraitNodeGroupID']].append(db['TraitCond'][r['TraitCondID']])

    parents = collections.defaultdict(list)
    for e in db['TraitEdge'].values():
        if e['LeftTraitNodeID'] in nodes and e['RightTraitNodeID'] in nodes:
            parents[e['RightTraitNodeID']].append((e['LeftTraitNodeID'], e['Type']))

    def strength(t):
        return 2 if t == EDGE_REQUIRED else (1 if t == EDGE_SUFFICIENT else 0)

    kept = {n: list(ps) for n, ps in parents.items()}
    for n, ps in parents.items():
        for p, t in ps:
            if p >= n:
                continue
            rev = [x for x in parents.get(p, []) if x[0] == n]
            if not rev:
                continue
            if strength(t) != strength(rev[0][1]):
                keep_p_to_n = strength(t) > strength(rev[0][1])
            elif nodes[p]['PosY'] != nodes[n]['PosY']:
                keep_p_to_n = nodes[p]['PosY'] < nodes[n]['PosY']
            else:
                keep_p_to_n = True
            if keep_p_to_n:
                kept[p] = [x for x in kept.get(p, []) if x[0] != n]
            else:
                kept[n] = [x for x in kept[n] if x[0] != p]

    def node_spells(n):
        for e in entries[n]:
            d = defs.get(tne[e]['TraitDefinitionID'])
            if d:
                yield from (s for s in (d['SpellID'], d['VisibleSpellID'], d['OverridesSpellID']) if s)

    def node_name(n):
        for e in entries[n]:
            d = defs.get(tne[e]['TraitDefinitionID'])
            if not d:
                continue
            if d['OverrideName']:
                return d['OverrideName']
            for s in (d['SpellID'], d['VisibleSpellID']):
                if s and db['SpellName'].get(s, {}).get('Name'):
                    return db['SpellName'][s]['Name']
            return 'spell %d' % d['SpellID']
        return ''

    def gate(n):
        vals = [c['SpentAmountRequired'] for g in groups[n] for c in gconds[g] if c['TraitCurrencyID']]
        return max(vals) if vals else 0

    tree_by_skill = {r['SkillLineID']: r['TraitTreeID'] for r in db['SkillLineXTraitTree'].values()}
    class_mask = {c: 1 << (c - 1) for c in CLASSES}
    rows = []
    for cls, cname in CLASSES.items():
        # The class tree is the SkillLineXTraitTree tree that holds the class's Talent.db2 spells.
        talents = [t for t in db['Talent'].values()
                   if (t['ClassID'] in (0, cls)) and db['TalentTab'].get(t['TabID'], {}).get('f5', 0) & class_mask[cls]]
        talent_spells = {s for t in talents for s in [t['SpellID']] + [t['SpellRank%d' % i] for i in range(1, 10)] if s}
        best = None
        for tree in set(tree_by_skill.values()):
            tn = [n for n, v in nodes.items() if v['TraitTreeID'] == tree and entries[n]]
            hits = sum(1 for n in tn if set(node_spells(n)) & talent_spells)
            if hits and (best is None or hits > best[0]):
                best = (hits, tree)
        tree = best[1]
        tn = [n for n, v in nodes.items() if v['TraitTreeID'] == tree and entries[n]]

        spell_node = {}
        for n in sorted(tn):
            for s in node_spells(n):
                spell_node.setdefault(s, n)
        node_talent = {}
        for t in sorted(talents, key=lambda t: t['ID']):
            for s in [t['SpellID']] + [t['SpellRank%d' % i] for i in range(1, 10)]:
                if s and s in spell_node:
                    node_talent.setdefault(spell_node[s], t['ID'])
                    break

        # BuildNodeTabMap: rows from the first PosY with 3+ nodes, columns split where PosX jumps by 1500+.
        ys = sorted(nodes[n]['PosY'] for n in tn)
        top = next(y for y in ys if sum(1 for o in ys if abs(o - y) <= 100) >= 3)
        row_of = {n: round((nodes[n]['PosY'] - top) / 600) for n in tn}
        placed = sorted((nodes[n]['PosX'], n) for n in tn if 0 <= row_of[n] <= 6)
        tabs = [[]]
        for i, (x, n) in enumerate(placed):
            if i and x - placed[i - 1][0] >= 1500:
                tabs.append([])
            tabs[-1].append(n)
        tabs = [t for t in tabs if len(t) >= 8][:3]
        for tab, members in enumerate(tabs):
            for n in sorted(members, key=lambda n: (row_of[n], nodes[n]['PosX'])):
                rows.append({
                    'class': cname, 'tree': tree, 'tab': tab, 'row': row_of[n], 'gate': gate(n), 'node': n,
                    'talent_id': node_talent.get(n, ''), 'max_rank': max(tne[e]['MaxRanks'] for e in entries[n]),
                    'parents': ';'.join('%d:%s' % (p, {EDGE_REQUIRED: 'required', EDGE_SUFFICIENT: 'sufficient'}
                                                  .get(t, 'visual')) for p, t in kept.get(n, [])),
                    'name': node_name(n),
                })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--core', required=True)
    ap.add_argument('--db2', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--client-build', default='70959')
    a = ap.parse_args()
    rows = build(load(a.core, a.db2))
    with open(a.out, 'w', newline='') as f:
        f.write('# Class talent trees as the server validates them (trait trees), client build %s. '
                'Generated by forever/tools/talent_trees_extract.py; do not edit by hand.\n' % a.client_build)
        w = csv.DictWriter(f, fieldnames=['class', 'tree', 'tab', 'row', 'gate', 'node', 'talent_id', 'max_rank',
                                          'parents', 'name'], lineterminator='\n')
        w.writeheader()
        w.writerows(rows)
    print('%d nodes written to %s' % (len(rows), a.out))


if __name__ == '__main__':
    main()
