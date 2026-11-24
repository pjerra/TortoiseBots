#!/usr/bin/env python3
"""Check the bot talent builds against the class trees the server validates, and keep the C++ table in step.

forever/data/waterys/talent-builds.csv holds one row per build step (class, spec, order, id, rank). The id is a Talent.db2
id when the trait node has one (TbTalents.cpp maps it through the talent's spell), otherwise the trait node id itself
(>= 100000; Forever-only talents such as Weaponmaster have no Talent.db2 row). Ranks are added up per id in step order.

Every build must, point by point in order (forever/data/waterys/talent-trees-70959.csv, from talent_trees_extract.py):
  - name a node of its class tree;
  - stay at or under the node's max rank;
  - have spent the row's gate (5 points per row) in the same tab first;
  - have its parent nodes filled the way TraitMgr::ValidateConfig wants: every "required" parent full, and at least
    one parent full;
and spend exactly 51 points.

    python3 forever/tools/talent_builds.py --check            # rules + the C++ table matches the CSV (exit 1 on error)
    python3 forever/tools/talent_builds.py --write-header     # regenerate kBuildSteps in forever/src/host/TbTalentsPure.h
"""
import argparse
import collections
import csv
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BUILDS = os.path.join(ROOT, 'forever', 'data', 'waterys', 'talent-builds.csv')
TREES = os.path.join(ROOT, 'forever', 'data', 'waterys', 'talent-trees-70959.csv')
HEADER = os.path.join(ROOT, 'forever', 'src', 'host', 'TbTalentsPure.h')

TRAIT_NODE_ID_MIN = 100000
POINTS_AT_60 = 51
CLASS_IDS = {'warrior': 1, 'paladin': 2, 'hunter': 3, 'rogue': 4, 'priest': 5, 'shaman': 7, 'mage': 8, 'warlock': 9,
             'druid': 11}
# Spec name -> tab, in the order of TbTalentsPure.h SpecName(); specs not listed here (shaman "tank") are kept in the
# CSV for reference but are not part of the C++ table.
SPEC_TABS = {
    'warrior': ['arms', 'fury', 'protection'], 'paladin': ['holy', 'protection', 'retribution'],
    'hunter': ['beastmastery', 'marksmanship', 'survival'], 'rogue': ['assassination', 'combat', 'subtlety'],
    'priest': ['discipline', 'holy', 'shadow'], 'shaman': ['elemental', 'enhancement', 'restoration'],
    'mage': ['arcane', 'fire', 'frost'], 'warlock': ['affliction', 'demonology', 'destruction'],
    'druid': ['balance', 'feral', 'restoration'],
}


def read_csv(path):
    with open(path, newline='') as f:
        return list(csv.DictReader(line for line in f if not line.startswith('#')))


def load_trees(path=TREES):
    trees = collections.defaultdict(dict)
    for r in read_csv(path):
        node = {'node': int(r['node']), 'tab': int(r['tab']), 'gate': int(r['gate']), 'max': int(r['max_rank']),
                'name': r['name'], 'talent_id': int(r['talent_id']) if r['talent_id'] else 0,
                'parents': [(int(p.split(':')[0]), p.split(':')[1]) for p in r['parents'].split(';') if p]}
        trees[r['class']][node['node']] = node
    return trees


def resolve(tree, step_id):
    if step_id >= TRAIT_NODE_ID_MIN:
        return tree.get(step_id)
    return next((n for n in tree.values() if n['talent_id'] == step_id), None)


def check_build(tree, steps):
    """steps: [(id, rank)] in order. Returns (errors, points per tab)."""
    errors = []
    ranks = collections.Counter()
    spent = collections.Counter()
    for i, (step_id, rank) in enumerate(steps, 1):
        node = resolve(tree, step_id)
        if not node:
            errors.append('step %d: id %d is not a node of this class tree' % (i, step_id))
            continue
        for _ in range(rank):
            n = node['node']
            what = 'step %d %s (%d)' % (i, node['name'], step_id)
            if ranks[n] >= node['max']:
                errors.append('%s: rank %d is over the max %d' % (what, ranks[n] + 1, node['max']))
            if spent[node['tab']] < node['gate']:
                errors.append('%s: row needs %d points in its tab first, has %d' % (what, node['gate'], spent[node['tab']]))
            if node['parents']:
                full = [(p, kind, ranks[p] >= tree[p]['max']) for p, kind in node['parents'] if p in tree]
                missing = [tree[p]['name'] for p, kind, ok in full if kind == 'required' and not ok]
                if missing or not any(ok for _, _, ok in full):
                    errors.append('%s: parent not filled (%s)' % (what, ', '.join(missing or [tree[p]['name'] for p, _, _ in full])))
            ranks[n] += 1
            spent[node['tab']] += 1
    total = sum(spent.values())
    if total != POINTS_AT_60:
        errors.append('spends %d points, not %d' % (total, POINTS_AT_60))
    return errors, spent


def builds_by_spec(rows):
    specs = collections.OrderedDict()
    for r in sorted(rows, key=lambda r: (r['class'], r['spec'], int(r['order']))):
        if r['talent_id'] and int(r['rank']) > 0:
            specs.setdefault((r['class'], r['spec']), []).append((int(r['talent_id']), int(r['rank'])))
    return specs


def table_steps(rows):
    """The kBuildSteps rows in CSV file order (class/spec blocks as they stand), tab from SPEC_TABS."""
    out = []
    for r in rows:
        if not r['talent_id'] or int(r['rank']) <= 0 or r['spec'] not in SPEC_TABS[r['class']]:
            continue
        out.append((CLASS_IDS[r['class']], SPEC_TABS[r['class']].index(r['spec']), int(r['talent_id']), int(r['rank'])))
    return out


def header_steps(text):
    body = text.split('kBuildSteps[] = {', 1)[1].split('};', 1)[0]
    return [tuple(map(int, m)) for m in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', body)]


def render_steps(steps):
    return ''.join('    { %2d, %d, %6d, %d },\n' % s for s in steps)


def check_all(builds_path=BUILDS, trees_path=TREES, header_path=HEADER):
    rows = read_csv(builds_path)
    trees = load_trees(trees_path)
    errors = []
    for cls, specs in SPEC_TABS.items():
        for spec in specs:
            if (cls, spec) not in builds_by_spec(rows):
                errors.append('%s %s: no build' % (cls, spec))
    for (cls, spec), steps in builds_by_spec(rows).items():
        errs, _ = check_build(trees[cls], steps)
        errors += ['%s %s: %s' % (cls, spec, e) for e in errs]
    if header_path and header_steps(open(header_path).read()) != table_steps(rows):
        errors.append('TbTalentsPure.h kBuildSteps does not match talent-builds.csv (run --write-header)')
    return errors


def write_header(builds_path=BUILDS, header_path=HEADER):
    text = open(header_path).read()
    head, rest = text.split('kBuildSteps[] = {\n', 1)
    tail = rest.split('};', 1)[1]
    open(header_path, 'w').write(head + 'kBuildSteps[] = {\n' + render_steps(table_steps(read_csv(builds_path))) + '};' + tail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--write-header', action='store_true')
    a = ap.parse_args()
    if a.write_header:
        write_header()
    errors = check_all()
    for e in errors:
        print('ERROR: ' + e)
    rows = read_csv(BUILDS)
    trees = load_trees()
    for (cls, spec), steps in builds_by_spec(rows).items():
        _, spent = check_build(trees[cls], steps)
        print('%-8s %-14s %d/%d/%d' % (cls, spec, spent[0], spent[1], spent[2]))
    print('%s: %d builds, %d error(s)' % ('FAIL' if errors else 'PASS', len(builds_by_spec(rows)), len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
