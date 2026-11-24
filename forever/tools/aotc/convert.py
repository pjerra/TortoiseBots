"""Turn the aotc.gg JSON dumps (scrape.js + recv.py) into the CSV files in forever/data/aotc/.

Usage: python3 -I convert.py <raw-dir> <out-dir> <talent-trees-csv>
"""
import csv
import json
import re
import sys
from pathlib import Path

raw, out, trees_csv = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)

BRACKETS = ['10–19', '20–29', '30–39', '40–49', '50–59', '60', 'All levels']
BR_OUT = {'10–19': '10-19', '20–29': '20-29', '30–39': '30-39', '40–49': '40-49', '50–59': '50-59', '60': '60', 'All levels': 'all'}


def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


# our trait trees: (class, normalised name) -> rows (one talent can sit in one tab only)
ours = {}
with trees_csv.open(encoding='utf-8') as f:
    rows = [l for l in f if not l.startswith('#')]
for r in csv.DictReader(rows):
    ours.setdefault((r['class'], norm(r['name'])), []).append(r)


def write(name, header, rows):
    with (out / name).open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(rows)
    print(f'{name}: {len(rows)} rows')


picks, avgs, splits, unmatched = [], [], [], set()
for p in sorted(raw.glob('talents_*.json')):
    d = json.loads(p.read_text(encoding='utf-8'))
    cls, spec = d['cls'], d['spec']
    for br in BRACKETS:
        b = d['brackets'].get(br)
        if not b:
            continue
        for ti, tree in enumerate(b['trees']):
            m = re.match(r'Average ([\d.]+) points in (.+?) over ([\d,]+) characters', tree['avg'] or '')
            tree_name = m.group(2) if m else f'tree{ti}'
            if m:
                avgs.append([cls, spec, BR_OUT[br], ti, tree_name, m.group(1), m.group(3).replace(',', '')])
            for name, spell, row, col, pct, chars, elig, usual, mx in tree['t']:
                # rare labels the page script left raw: "<0.1%: 3 of N ..." and "no character with enough points"
                lm = re.match(r'^(.*?): (?:<?([\d.]+)%: ([\d,]+) of ([\d,]+) characters with enough points(?:, usual rank (\d+) of (\d+))?|no character with enough points)$', name)
                if lm and pct is None:
                    name = lm.group(1)
                    if lm.group(2):
                        chars, elig = int(lm.group(3).replace(',', '')), int(lm.group(4).replace(',', ''))
                        pct = round(100.0 * chars / elig, 3) if elig else 0
                        usual = int(lm.group(5) or 0)
                        mx = int(lm.group(6)) if lm.group(6) else mx
                    else:
                        pct, chars, elig, usual = 0, 0, 0, 0
                hit = ours.get((cls, norm(name)))
                node = hit[0]['node'] if hit else ''
                tid = hit[0]['talent_id'] if hit else ''
                our_max = hit[0]['max_rank'] if hit else ''
                if not hit:
                    unmatched.add((cls, tree_name, name, spell))
                picks.append([cls, spec, BR_OUT[br], ti, tree_name, row, col, name, spell, mx, pct, chars, elig, usual, node, tid, our_max])
    for split, pct, chars in d['brackets'].get('60', {}).get('splits', []):
        splits.append([cls, spec, split, pct, chars])

write('talent-picks.csv',
      ['class', 'spec', 'levels', 'tab', 'tree', 'row', 'col', 'talent', 'spell_id', 'max_rank', 'pick_pct', 'chars', 'eligible_chars', 'usual_rank', 'our_trait_node', 'our_talent_id', 'our_max_rank'],
      picks)
write('talent-tree-points.csv', ['class', 'spec', 'levels', 'tab', 'tree', 'avg_points', 'chars'], avgs)
write('talent-splits-60.csv', ['class', 'spec', 'split', 'pct', 'chars'], splits)
write('talent-unmatched.csv', ['class', 'tree', 'talent', 'spell_id'], sorted(unmatched))

# gear / enchants / stat priority (when present)
for kind in ('gear', 'enchants'):
    rows = []
    for p in sorted(raw.glob(f'{kind}_*.json')):
        d = json.loads(p.read_text(encoding='utf-8'))
        for pop, entries in d['pops'].items():
            for slot, name, item_id, pct, chars, total in entries:
                sm = re.match(r'^(.*?)(\d+) pieces$', slot)
                if sm:  # item set bonus rows: the heading glues the set name and its size
                    slot = f'Set: {sm.group(1)} ({sm.group(2)} pieces)'
                rows.append([d['cls'], d['spec'], pop, slot, name, item_id, pct, chars, total])
    if rows:
        write(f'{kind}-60.csv', ['class', 'spec', 'population', 'slot', 'name', 'wowhead_id', 'pct', 'chars', 'of_chars'], rows)

rows = []
for p in sorted(raw.glob('overview_*.json')):
    d = json.loads(p.read_text(encoding='utf-8'))
    # tokens: "Healing 245 › Spell Power 136 ≈ Intellect (about equal) 132 › ..."; "≈" ties a stat to the one before it
    order, tied, name = 0, False, None
    for tok in d['tokens']:
        if tok == '≈':
            tied = True
        elif tok in ('›', '(about equal)'):
            continue
        elif re.fullmatch(r'\d+', tok) and name:
            if not tied:
                order += 1
            rows.append([d['cls'], d['spec'], order, name, tok, 'yes' if tied else ''])
            name, tied = None, False
        else:
            name = tok
if rows:
    write('stat-priority-60.csv', ['class', 'spec', 'order', 'stat', 'weight', 'about_equal_to_previous'], rows)

p = raw / 'census.json'
if p.exists():
    d = json.loads(p.read_text(encoding='utf-8'))
    t = d['text']
    write('census-levels.csv', ['level', 'chars'], d['levels'])
    num = lambda s: int(s.replace(',', ''))
    lvl60 = {m.group(1).lower(): (num(m.group(2)), m.group(3)) for m in re.finditer(r'\n(\w+)\n\n([\d,]+)\n([\d.]+)%', t.split('Level 60 by class')[1].split('WoW Forever race distribution')[0])}
    rows = []
    for m in re.finditer(r'\n(\w+)\n: Alliance ([\d.]+)% \(([\d,]+)\), Horde ([\d.]+)% \(([\d,]+)\)', t):
        cls = m.group(1).lower()
        a, h = num(m.group(3)), num(m.group(5))
        rows.append([cls, a + h, a, h, *lvl60.get(cls, ('', ''))])
    write('census-classes.csv', ['class', 'chars', 'alliance', 'horde', 'chars_60', 'pct_of_class_at_60'], rows)
    female = {}
    for part in ('By class, female share', 'By race, female share'):
        seg = t.split(part)[1].split('By race, female share' if 'class' in part else 'Classes and races')[0]
        for m in re.finditer(r'\n([A-Za-z ]+)\n([\d.]+)%', seg):
            female[m.group(1).strip()] = m.group(2)
    rows = []
    for fac in ('Alliance', 'Horde'):
        seg = t.split('Races by faction')[1].split('Classes and races by ruleset')[0]
        seg = seg.split(fac + '\n')[1].split('Horde\n')[0] if fac == 'Alliance' else seg.split('\nHorde\n')[1]
        for m in re.finditer(r'\n([A-Za-z ]+)\n\n([\d.]+)%\n', seg):
            rows.append([m.group(1).strip(), fac.lower(), m.group(2), female.get(m.group(1).strip(), '')])
    write('census-races.csv', ['race', 'faction', 'pct_of_faction', 'female_pct'], rows)
    write('census-class-gender.csv', ['class', 'female_pct'],
          [[c.lower(), female[c]] for c in ('Priest', 'Druid', 'Mage', 'Hunter', 'Rogue', 'Warlock', 'Shaman', 'Paladin', 'Warrior') if c in female])
    rows = []
    for title, kind in (('Most played pairings', 'most'), ('Least played pairings', 'least')):
        seg = t.split(title)[1].split('Least played pairings' if kind == 'most' else 'New to Forever')[0]
        for m in re.finditer(r'\n(\d+)\n\n([A-Za-z ]+?)\n\n(?:NEW\n)?([\d.]+)%\n([\d.]+k)', seg):
            rows.append([kind, m.group(1), m.group(2), m.group(3), m.group(4)])
    write('census-race-class-pairs.csv', ['list', 'rank', 'pair', 'pct_of_all', 'chars'], rows)

p = raw / 'page_stats.json'
if p.exists():
    t = json.loads(p.read_text(encoding='utf-8'))['text']
    rows = []
    for mode in ('Normal', 'PvP'):
        seg = t.split('Causes of death: Normal vs PvP')[1].split('How players travel')[0].split('\n' + mode + '\n')[1]
        for m in re.finditer(r'(Monsters|Other players|Dungeons and raids|Environment): (\d+)% \(([\d,]+) deaths\)', seg):
            rows.append(['death_cause_' + mode.lower(), m.group(1), m.group(2), m.group(3).replace(',', '')])
            if m.group(1) == 'Environment':
                break
    seg = t.split('How players travel')[1].split('New WoW Forever bosses')[0]
    rows += [['travel', m.group(1), m.group(2), ''] for m in re.finditer(r'\n([A-Z][a-z ]+)\n([\d.]+)%', seg)]
    seg = t.split('What players consume')[1]
    rows += [['consume', m.group(1), m.group(2), ''] for m in re.finditer(r'\n([A-Z][a-z ]+)\n([\d.]+)%', seg)]
    fav = re.search(r'Favorite bandage: (.*)', seg)
    if fav:
        rows += [['bandage', m.group(1).strip(), m.group(2), ''] for m in re.finditer(r'([A-Za-z ]+?) (\d+)%', fav.group(1))]
    write('behaviour-stats.csv', ['what', 'name', 'pct', 'count'], rows)
    seg = t.split('New WoW Forever bosses')[1].split('What players consume')[0]
    rows = []
    for m in re.finditer(r'\n(.+?)\n([\d.]+)%', seg):
        glued = m.group(1).strip()  # "Edwin VanCleefDeadmines": the instance starts at the last lower-to-upper step
        cut = [i + 1 for i in range(len(glued) - 1) if glued[i].islower() and glued[i + 1].isupper()][-1]
        rows.append([glued[:cut], glued[cut:], m.group(2)])
    write('new-forever-bosses.csv', ['boss', 'instance', 'pct_of_characters_killed'], rows)

p = raw / 'page_legacy_points.json'
if p.exists():
    t = json.loads(p.read_text(encoding='utf-8'))['text']
    rows, group = [], ''
    lines = [l.strip() for l in t.split('How to earn Legacy Points')[2].split('\n') if l.strip()]
    for i, l in enumerate(lines):
        if i + 1 < len(lines) and re.match(r'^\d+ Legacy Points?$', lines[i + 1]):
            group = l
        elif re.match(r'^\d+ Legacy Points?$', l) or i + 1 >= len(lines):
            continue
        elif lines[i + 1].endswith('.') and not l.endswith('.'):
            rows.append([group, l, lines[i + 1]])
    write('legacy-achievements.csv', ['group', 'achievement', 'criteria'], rows)

p = raw / 'population.json'
if p.exists():
    d = json.loads(p.read_text(encoding='utf-8'))
    write('spec-population.csv', ['class', 'spec', 'pct_of_class_60', 'class_chars_60'], d['rows'])
