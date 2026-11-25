#!/usr/bin/env python3
"""Offline audit of the spec weight scales against the Forever player data.

Reads the shipped scales (forever/sql/world/2026_11_20_20_tortoisebots_weightscales.sql),
the stat priority of level-60 Forever players (forever/data/aotc/stat-priority-60.csv) and,
with --items, an item stat dump, and answers two questions:

  1. per spec, how do the shipped weights compare with what players actually stack?
     Stats are shown in one unit: the value of one point of an item stat. The scales
     price crit/hit/dodge/parry/block per percent, so those weights are divided by the
     level-60 CombatRatings ratio (parsed from ai/playerbot/policy/item/ClassicItemStatPolicy.h)
     before comparing; the ratio is constant across levels. The equip-spell path
     already passes percent. The aotc weights are the site's own numbers, compared
     in the order and the ratio they state.
  2. with --items, score the items Forever players wear most (gear-60.csv) per slot with
     the same stat rules as RandomItemMgr::CalculateStatWeight and list the items the
     scoring places below the top of their slot. The gates that drop a whole item
     (armor class, weapon subclass, caster/attacker mismatch) are not modelled: every
     gear-60 row is already an item that spec wears, so only the stat score is compared.

--items takes a CSV with an id column and a stats column in the armory_item format
("statType:value,...", ItemModType ids) plus optional armor/dmgMin/dmgMax/delay/
subclass/inventoryType/name. The world database's armory_item table is that export:

  mysql -N -B -e "SELECT id,name,quality,itemLevel,class,subclass,inventoryType,armor,\\
    dmgMin,dmgMax,delay,stats FROM world.armory_item" > armory_item.tsv

(columns comma- or tab-separated). Without --items the weight comparison still runs; the
gear part prints what is missing and why it needs the server's item data.

Usage:
  python3 forever/tools/check_gear_weights.py
  python3 forever/tools/check_gear_weights.py --items armory_item.tsv
  python3 forever/tools/check_gear_weights.py --weights <other.sql> --items armory_item.tsv --top 10
"""
import argparse
import csv
import os
import re
import sys
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEIGHTS_SQL = os.path.join(ROOT, "forever/sql/world/2026_11_20_20_tortoisebots_weightscales.sql")
POLICY_H = os.path.join(ROOT, "ai/playerbot/policy/item/ClassicItemStatPolicy.h")
PRIORITY_CSV = os.path.join(ROOT, "forever/data/aotc/stat-priority-60.csv")
GEAR_CSV = os.path.join(ROOT, "forever/data/aotc/gear-60.csv")
ITEMS_CSV = os.path.join(ROOT, "forever/data/waterys/items.csv")

# aotc class/spec -> the scale id it scores with. feral is the cat build: the
# level-60 feral players in gear-60 wear agility leather and a two-hand spear,
# and the bear scale (30) is reached from the tank role, not from a spec name.
SPEC_SCALE = {
    ("warrior", "arms"): 1,
    ("warrior", "fury"): 2,
    ("warrior", "protection"): 3,
    ("paladin", "holy"): 4,
    ("paladin", "protection"): 5,
    ("paladin", "retribution"): 6,
    ("hunter", "beast-mastery"): 7,
    ("hunter", "marksmanship"): 8,
    ("hunter", "survival"): 9,
    ("rogue", "assassination"): 10,
    ("rogue", "combat"): 11,
    ("rogue", "subtlety"): 12,
    ("priest", "discipline"): 13,
    ("priest", "holy"): 14,
    ("priest", "shadow"): 15,
    ("shaman", "elemental"): 20,
    ("shaman", "enhancement"): 21,
    ("shaman", "restoration"): 22,
    ("mage", "arcane"): 23,
    ("mage", "fire"): 24,
    ("mage", "frost"): 25,
    ("warlock", "affliction"): 26,
    ("warlock", "demonology"): 27,
    ("warlock", "destruction"): 28,
    ("druid", "balance"): 29,
    ("druid", "feral"): 32,
    ("druid", "restoration"): 31,
}

# aotc stat names that are the same under another name in the weights.
AOTC_STAT = {
    "Stamina": "sta", "Strength": "str", "Agility": "agi", "Intellect": "int",
    "Spirit": "spi", "Attack Power": "atkpwr", "Spell Power": "splpwr",
    "Healing": "splheal", "Mana per 5": "manargn", "Defense": "defrtng",
}

# Specs whose Hit/Crit stat is the spell one. Everyone else scores melee/ranged
# hit and crit, including protection paladins, enhancement shamans and feral.
CASTER_SPECS = {
    ("mage", "arcane"), ("mage", "fire"), ("mage", "frost"),
    ("warlock", "affliction"), ("warlock", "demonology"), ("warlock", "destruction"),
    ("priest", "discipline"), ("priest", "holy"), ("priest", "shadow"),
    ("paladin", "holy"),
    ("shaman", "elemental"), ("shaman", "restoration"),
    ("druid", "balance"), ("druid", "restoration"),
}

# ItemModType of ForeverCore spell power: grants damage and healing, unlike the
# damage-only spell-damage stat (42), so it scores under the better weight.
FOREVER_SPELL_POWER = 45

# The scale stat family rule of ai/playerbot/RandomItemWeights.cpp GetSpecStatFamily.
SPELL_STATS = {
    "int", "spi", "splpwr", "splheal", "manargn", "arcsplpwr", "frosplpwr",
    "firsplpwr", "shasplpwr", "natsplpwr", "spellhitrtng", "spellcritstrkrtng",
    "spellpenrtng",
}
TANK_STATS = {"defrtng", "dodgertng", "parryrtng", "blockrtng", "block", "armor"}

# ItemModType base stats the module reads through weightStatLink.
BASE_STAT = {3: "agi", 4: "str", 5: "int", 6: "spi", 7: "sta"}

# Inventory subclasses whose damage the code scores as ranged (IsRangedWeapon).
RANGED_SUBCLASS = {2, 3, 16, 18, 19}


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_weights(path=WEIGHTS_SQL):
    """{scale id: {"name": str, "class": int, "stats": {name: weight}}} from the seed SQL."""
    text = read_text(path)
    if "INSERT INTO `ai_playerbot_weightscales`" not in text:
        raise SystemExit(path + ": no weightscales INSERT")
    scale_rows = text.split("INSERT INTO `ai_playerbot_weightscale_data`")[0]
    scales = {}
    for m in re.finditer(r"\((\d+), '([a-z]+)', (\d+)\)", scale_rows):
        scales[int(m.group(1))] = {"name": m.group(2), "class": int(m.group(3)),
                                   "stats": OrderedDict()}
    data_rows = text.split("INSERT INTO `ai_playerbot_weightscale_data`")[1]
    for m in re.finditer(r"\((\d+), '([a-z]+)', (\d+)\)", data_rows):
        sid, name, val = int(m.group(1)), m.group(2), int(m.group(3))
        if sid not in scales:
            raise SystemExit("weight row for unknown scale id %d" % sid)
        scales[sid]["stats"][name] = val
    if not scales:
        raise SystemExit(path + ": parsed no weight scales")
    return scales


def parse_policy(path=POLICY_H):
    """(name by stat id, rating divisor by stat id), read from the C++ switch tables.

    Reads the shipped header instead of copying the maps, so a new ItemModType
    mapping reaches the check without being added twice. Fails when the shape of
    a switch table changes enough that the parse would silently lose rows.
    """
    text = read_text(path)
    names_body = _function_body(text, "ClassicItemStatWeightName")
    divisors_body = _function_body(text, "ClassicItemStatRatingDivisor")
    names = _parse_switch(names_body, r'return "([a-z]+)";')
    divisors = {k: int(v) for k, v in _parse_switch(divisors_body, r"return (\d+);").items()}
    if len(names) < 28 or 45 not in names or 32 not in names:
        raise SystemExit(path + ": parsed %d stat names, expected the Classic map" % len(names))
    if len(divisors) < 8 or divisors.get(32) != 14:
        raise SystemExit(path + ": parsed %d rating divisors, expected the rating table" % len(divisors))
    return names, divisors


def _function_body(text, name):
    m = re.search(r"inline [^\n]*\b" + name + r"\s*\(.*?\n\}", text, re.S)
    if not m:
        raise SystemExit("cannot find %s in the stat policy header" % name)
    return m.group(0)


def _parse_switch(body, return_re):
    """{stat id: return value} for a switch body; fallthrough case labels share the next return."""
    out = {}
    pending = []
    for line in body.splitlines():
        m = re.search(r"case (\d+):", line)
        if m:
            pending.append(int(m.group(1)))
            ret = re.search(return_re, line)
            if ret:
                for c in pending:
                    out[c] = ret.group(1)
                pending = []
            continue
        ret = re.search(return_re, line)
        if ret and pending:
            for c in pending:
                out[c] = ret.group(1)
            pending = []
        elif re.search(r"\bdefault\s*:|\bbreak\s*;|\}", line):
            pending = []
    return out


def parse_priority(path=PRIORITY_CSV):
    """{(class, spec): [(order, stat, weight, tied)]} in file order."""
    out = OrderedDict()
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            key = (r["class"].strip(), r["spec"].strip())
            out.setdefault(key, []).append(
                (int(r["order"]), r["stat"].strip(), int(r["weight"]),
                 r["about_equal_to_previous"].strip().lower() == "yes"))
    return out


def weight_stat_name(cls, spec, stat):
    """Map one aotc stat to the scale name it scores under."""
    if stat == "Crit":
        return "spellcritstrkrtng" if (cls, spec) in CASTER_SPECS else "critstrkrtng"
    if stat == "Hit":
        return "spellhitrtng" if (cls, spec) in CASTER_SPECS else "hitrtng"
    return AOTC_STAT.get(stat, stat.lower())


def effective_weight(stats, name):
    """The per-point weight a name scores with.

    The Spell Power row is ForeverCore stat 45, which grants damage and healing,
    so RandomItemWeights.cpp scores it under the better of splpwr and splheal.
    """
    weight = stats.get(name, 0)
    if name == "splpwr":
        weight = max(weight, stats.get("splheal", 0))
    return weight


def per_point_weight(name, weight, divisors, names):
    """Rating weights are per percent; convert with the stat's own divisor."""
    stat_id = next((k for k, v in names.items() if v == name), None)
    div = divisors.get(stat_id, 1) if stat_id is not None else 1
    return weight / div


def spec_family(stats):
    """The DeriveSpecStatFamily answer for one scale (strict comparisons, as in C++)."""
    spell = sum(v for k, v in stats.items() if k in SPELL_STATS)
    tank = sum(v for k, v in stats.items() if k in TANK_STATS)
    agi = stats.get("agi", 0)
    strength = stats.get("str", 0)
    if tank > 0 and (tank > 20 or tank >= strength):
        return "Tank"
    if spell > 0 and spell > strength and spell > agi:
        return "Spell"
    if agi > strength:
        return "Agility"
    if strength > 0:
        return "Strength"
    return "None"


def score_item(scale, item, names, divisors):
    """The stat part of RandomItemMgr::CalculateStatWeight for one item dict.

    item: {"stats": {stat id: value}, "armor": n, "dmgMin":, "dmgMax":,
    "delay":, "subclass":}.
    """
    stats = scale["stats"]
    score = 0
    for stat_type, value in item.get("stats", {}).items():
        if value == 0:
            continue
        name = BASE_STAT.get(stat_type) or names.get(stat_type)
        if not name:
            continue
        if stat_type == FOREVER_SPELL_POWER:
            name = "splheal" if stats.get("splheal", 0) > stats.get("splpwr", 0) else "splpwr"
        if name not in stats:
            continue
        weighted = stats[name] * value
        if weighted <= 0:
            continue
        div = divisors.get(stat_type, 1)
        if div > 1:
            weighted = max(1, (weighted + div // 2) // div)
        score += weighted
    score += stats.get("armor", 0) * int(item.get("armor", 0))
    if item.get("dmgMax", 0) and item.get("delay", 0):
        dps = int(((item["dmgMin"] + item["dmgMax"]) / (item["delay"] / 1000.0)) / 2)
        name = "rgddps" if item.get("subclass") in RANGED_SUBCLASS else "mledps"
        score += stats.get(name, 0) * dps
    return score


def parse_stats_field(text):
    """'type:value,type:value' -> {type: value}; ignores malformed pairs."""
    stats = {}
    for pair in (text or "").replace(";", ",").split(","):
        if not pair.strip():
            continue
        if ":" not in pair:
            continue
        k, v = pair.split(":", 1)
        try:
            stats[int(k)] = int(float(v))
        except ValueError:
            continue
    return stats


def load_items(path):
    """{item id: item dict} from an armory_item-style dump (CSV or TSV)."""
    with open(path, newline="", encoding="utf-8") as f:
        sample = f.read(4096)
        f.seek(0)
        dialect = csv.excel_tab if sample.count("\t") > sample.count(",") else csv.excel
        reader = csv.DictReader(f, dialect=dialect)
        items = {}
        for row in reader:
            row = {k.strip(): (v or "").strip() for k, v in row.items() if k}
            item_id = None
            for key in ("id", "item_id", "wowhead_id"):
                raw = row.get(key, "")
                m = re.search(r"item=(\d+)", raw) or re.fullmatch(r"\d+", raw or "")
                if m:
                    item_id = int(m.group(1) if m.lastindex else raw)
                    break
            if item_id is None:
                continue

            def num(key):
                try:
                    return float(row.get(key, "") or 0)
                except ValueError:
                    return 0.0

            items[item_id] = {
                "id": item_id,
                "name": row.get("name", ""),
                "stats": parse_stats_field(row.get("stats", "")),
                "armor": num("armor"),
                "dmgMin": num("dmgMin"),
                "dmgMax": num("dmgMax"),
                "delay": num("delay"),
                "subclass": int(num("subclass")),
                "inventoryType": int(num("inventoryType")),
            }
    return items


def load_gear(path=GEAR_CSV):
    """[(class, spec, population, slot, name, item id, pct)] from gear-60.csv."""
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            m = re.search(r"item=(\d+)", r["wowhead_id"] or "")
            if not m:
                continue
            rows.append((r["class"].strip(), r["spec"].strip(), r["population"].strip(),
                         r["slot"].strip(), r["name"].strip(), int(m.group(1)),
                         float(r["pct"] or 0)))
    return rows


def tb_item_ids(path=ITEMS_CSV):
    ids = set()
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            for key in ("tb_item_id", "forever_id"):
                v = (r.get(key) or "").strip()
                if v.isdigit():
                    ids.add(int(v))
    return ids


def compare(out, scales, priority, names, divisors, min_share=0.05, factor=2.0):
    """Print the per-spec weight comparison; return the flagged rows.

    Both sides are shown as each stat's share of the spec's total over the stats
    the priority lists, which is independent of the two scales' sizes and still
    reads the same when a stat is missing from one side. Stats our scale weights
    that the priority does not list (weapon DPS, a crit/hit the site leaves out)
    stay out of the total, because the site gives them no value to compare.
    """
    flagged = []
    for (cls, spec), rows in priority.items():
        sid = SPEC_SCALE.get((cls, spec))
        out.write("\n%s %s (scale %s)\n" % (cls, spec, sid))
        if sid is None:
            out.write("  no scale id mapped for this spec\n")
            flagged.append((cls, spec, "spec", "no scale mapped"))
            continue
        stats = scales[sid]["stats"]
        entries = []
        aotc_total = 0
        for order, stat, weight, tied in rows:
            name = weight_stat_name(cls, spec, stat)
            entries.append((order, stat, weight, tied, name))
            aotc_total += weight
        ours_points = {name: per_point_weight(name, effective_weight(stats, name), divisors, names)
                       for _o, _s, _w, _t, name in entries}
        ours_total = sum(ours_points.values())
        for order, stat, weight, tied, name in entries:
            ours = effective_weight(stats, name)
            a_share = weight / aotc_total if aotc_total else 0
            o_share = ours_points[name] / ours_total if ours_total else 0
            mark = ""
            if ours == 0 and a_share >= min_share:
                mark = "MISSING"
            elif ours > 0 and a_share > 0 and ratio_factor(o_share, a_share) >= factor:
                mark = "x%.1f" % ratio_factor(o_share, a_share)
            if mark:
                flagged.append((cls, spec, name, mark))
            out.write("  %-9s %-12s aotc %4d (%.2f)  ours %3d (%.2f)  %s\n" % (
                "%d%s" % (order, "~" if tied else ""), stat, weight, a_share,
                ours, o_share, mark))
        missing = [n for n in {e[4] for e in entries} if effective_weight(stats, n) <= 0]
        if missing:
            out.write("  no weight at all: %s\n" % ", ".join(sorted(missing)))
    return flagged


def ratio_factor(ours, theirs):
    if ours <= 0 or theirs <= 0:
        return 0.0
    return max(ours / theirs, theirs / ours)


def rank_slots(out, scales, gear, items, names, divisors, top=5):
    """Score the worn items per slot; return [(key, name, id, pct, rank, score)] badly rated."""
    slots = OrderedDict()
    for cls, spec, population, slot, name, item_id, pct in gear:
        slots.setdefault((cls, spec, population, slot), []).append((name, item_id, pct))

    bad = []
    absent = set()
    for key, entries in slots.items():
        cls, spec, population, slot = key
        sid = SPEC_SCALE.get((cls, spec))
        if sid is None:
            continue
        scored = []
        for name, item_id, pct in entries:
            item = items.get(item_id)
            if item is None:
                absent.add(item_id)
                continue
            scored.append((name, item_id, pct, score_item(scales[sid], item, names, divisors)))
        if len(scored) < 2:
            continue
        # The site's order is the file order (share worn), not the order of the
        # items this dump happens to carry; ours is by score.
        by_score = sorted(scored, key=lambda e: (-e[3], -e[2]))
        our_rank = {e[1]: i + 1 for i, e in enumerate(by_score)}
        for name, item_id, pct in entries[:top]:
            if item_id not in our_rank:
                continue
            rank = our_rank[item_id]
            if rank > top:
                score = next(e[3] for e in scored if e[1] == item_id)
                bad.append((key, name, item_id, pct, rank, score))
    return bad, absent


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--items", help="armory_item-style item stat dump (CSV or TSV)")
    ap.add_argument("--weights", default=WEIGHTS_SQL,
                    help="weight scale SQL to audit (default: the shipped seed)")
    ap.add_argument("--gear", default=GEAR_CSV, help="gear-60.csv (default: forever/data/aotc)")
    ap.add_argument("--priority", default=PRIORITY_CSV)
    ap.add_argument("--items-csv", default=ITEMS_CSV,
                    help="the ids TortoiseBots names (default: forever/data/waterys/items.csv)")
    ap.add_argument("--top", type=int, default=5,
                    help="a slot's top N items the scoring should keep near the top (default 5)")
    ap.add_argument("--factor", type=float, default=2.0,
                    help="ratio difference that counts as a mismatch (default 2)")
    args = ap.parse_args(argv)

    scales = parse_weights(args.weights)
    names, divisors = parse_policy()
    priority = parse_priority(args.priority)

    unknown = [k for k in priority if k not in SPEC_SCALE]
    if unknown:
        raise SystemExit("no scale mapped for: %s" % ", ".join("%s/%s" % k for k in unknown))
    for (cls, spec), sid in SPEC_SCALE.items():
        if sid not in scales:
            raise SystemExit("scale id %d (%s/%s) is not in %s" % (sid, cls, spec, args.weights))

    out = sys.stdout
    out.write("scales: %d, weights: %d (from %s)\n" % (
        len(scales), sum(len(s["stats"]) for s in scales.values()), os.path.relpath(args.weights, ROOT)))
    out.write("stat priority: %s\n" % os.path.relpath(args.priority, ROOT))
    families = {sid: spec_family(s["stats"]) for sid, s in scales.items()}
    out.write("\nscale stat family (what loot priority sees):\n")
    for sid in sorted(families):
        out.write("  %2d %-9s class %2d  %s\n" % (sid, scales[sid]["name"], scales[sid]["class"], families[sid]))

    out.write("\nweights vs Forever level-60 stat priority (per point of the item stat):\n")
    flagged = compare(out, scales, priority, names, divisors, factor=args.factor)
    out.write("\n%d flagged rows:\n" % len(flagged))
    for cls, spec, stat, mark in flagged:
        out.write("  %-8s %-14s %-16s %s\n" % (cls, spec, stat, mark))

    out.write("\ngear-60 items (worn most at 60): %s\n" % os.path.relpath(args.gear, ROOT))
    gear = load_gear(args.gear)
    tb_ids = tb_item_ids(args.items_csv)
    gear_ids = sorted({row[5] for row in gear})
    missing_tb = [i for i in gear_ids if i not in tb_ids]
    out.write("  %d rows, %d distinct item ids; %d ids are not in %s\n" % (
        len(gear), len(gear_ids), len(missing_tb), os.path.relpath(args.items_csv, ROOT)))
    forever_range = [i for i in missing_tb if i >= 100000]
    out.write("  %d of those are above the ids that file covers (>= 100000), i.e. Forever-added items\n" % len(forever_range))

    if not args.items:
        out.write("\nNo --items dump: the per-slot ranking needs the item stats the server builds\n"
                  "from ItemSparse + hotfixes. Export them once on the server (see this file's header)\n"
                  "and rerun with --items armory_item.tsv. The weight comparison above needs no server.\n")
        return 0

    items = load_items(args.items)
    out.write("  stats dump: %s (%d items)\n" % (args.items, len(items)))
    bad, absent = rank_slots(out, scales, gear, items, names, divisors, top=args.top)
    out.write("\n  %d worn items have no stats row in the dump\n" % len(absent))
    out.write("  %d worn top-%d items our scoring puts outside its top %d of the slot:\n" % (
        len(bad), args.top, args.top))
    for (cls, spec, population, slot), name, item_id, pct, rank, score in bad:
        out.write("    %-8s %-14s %-10s %-14s %-32s id %-7d worn %5.1f%%  ours #%d (score %d)\n" % (
            cls, spec, population, slot, name, item_id, pct, rank, score))
    return 0


if __name__ == "__main__":
    sys.exit(main())
