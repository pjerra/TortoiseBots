#!/usr/bin/env python3
"""Slice 11, package D11: kits for the hunter (marksmanship, bow) and warlock (affliction, staff and wand) roles at level 18 and 22.
(Adapted from forever/data/slice10/gen_kits.py of package D10: same inputs, same rules, new roles.)

    python3 gen_kits.py <dir with armory.tsv vendor.tsv quest.tsv loot.tsv names.tsv> <out dir>

The four inputs are SELECTs on the world database of the forever test server (read only, TDB 2026-10-14 + the client's DB2):
  armory.tsv  armory_item rows (class 2 weapons / class 4 armor, quality 1-3, requiredLevel <= 22, itemLevel <= 34, no test names):
              id name quality itemLevel requiredLevel class subclass inventoryType armor dmgMin dmgMax delay stats allowableClass requiredSkill flags
  vendor.tsv  npc_vendor: item, lowest vendor entry, vendor count (ExtendedCost 0)
  quest.tsv   quest_template: ID, LogTitle, AllowableRaces, RewardChoiceItemID1-6, RewardItem1-4
  loot.tsv    creature_loot_template (plain items, chance >= 0.5, not quest-required): item, lowest creature entry, count, max chance
  names.tsv   entry name   (creature names for the vendor and creature entries the rows above may cite)
No item id is written here: every id is picked from those tables by the weights below, then each row is checked again by
`.tb itemscan` / `.tb equip` on a live server (M10). Output format = forever/data/waterys/rfc/kits-level18.csv.
"""
import csv, os, sys

FOODWATER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "waterys", "rfc", "food-water-5-25.csv")
CLASS_BIT = {"hunter": 1 << 2, "warlock": 1 << 8}
RACE_BIT = {"hunter": 4, "warlock": 64}      # dwarf hunter (race 3), gnome warlock (race 7): FOREVER-SLICE11-PETS-SPEC section 1

# Stat codes of the armory table: 3 agility 4 strength 5 intellect 6 spirit 7 stamina 12 defense 38 attack power 41 healing
# 42 spell damage 43 mana per 5 s 45 spell power 50 bonus armor.
W = {
    "hunter":  {"3": 1.0, "38": 0.5, "7": 0.5, "5": 0.4, "4": 0.2, "6": 0.1, "armor": 0.02},
    "warlock": {"45": 1.0, "42": 1.0, "5": 0.9, "7": 0.7, "6": 0.3, "43": 0.4, "armor": 0.0},
    "armoronly": {"armor": 1.0},
}
# role -> (class, weights, armor subclasses best first, weapon subclasses, off-hand kind)
# A hunter wears leather (mail is a level-40 skill), a warlock cloth. Hunter: a two-hander (staff 10, polearm 6, two-hand sword 8, two-hand axe 1; no mace) and a bow
# (arrows 2512 are the kit's ammo, the spec's setup line); a dwarf could shoot a gun (Light Shot 2516): recorded in the notes, not the default. Warlock: a staff (10) and a wand.
ROLES = [
    ("marksmanship hunter", "hunter",  "hunter",  [2, 1], [10, 6, 8, 1], "twohand"),
    ("affliction warlock",  "warlock", "warlock", [1],    [10],         "twohand"),
]
# slot name -> inventoryTypes
BODY = {"head": [1], "shoulder": [3], "back": [16], "chest": [5, 20], "wrist": [9], "hands": [10], "waist": [6], "legs": [7], "feet": [8]}
JEWEL = {"neck": [2], "finger": [11], "trinket": [12]}
SLOTS = ["head", "neck", "shoulder", "back", "chest", "wrist", "hands", "waist", "legs", "feet", "finger", "finger2",
         "trinket", "trinket2", "main_hand", "off_hand", "ranged"]
BAD_NAME = ("test", "deprecated", " old", "unused", "[ph]", "monster", "(old)", "qa ")


def rows(path):
    with open(path, newline="") as f:
        return list(csv.reader(f, delimiter="\t"))


def stats(text):
    out = {}
    for part in text.split(","):
        if ":" in part:
            k, v = part.split(":")
            out[k] = out.get(k, 0) + int(v)
    return out


def score(item, weights):
    s = stats(item["stats"])
    total = sum(weights.get(k, 0.0) * v for k, v in s.items())
    total += weights.get("armor", 0.0) * item["armor"]
    return total


def main():
    src, out = sys.argv[1], sys.argv[2]
    names = {r[0]: r[1] for r in rows(os.path.join(src, "names.tsv"))}
    items = {}
    for r in rows(os.path.join(src, "armory.tsv")):
        i = dict(id=int(r[0]), name=r[1], quality=int(r[2]), ilvl=int(r[3]), req=int(r[4]), cls=int(r[5]), sub=int(r[6]),
                 inv=int(r[7]), armor=int(r[8]), dmin=float(r[9]), dmax=float(r[10]), delay=int(r[11]), stats=r[12],
                 allow=int(r[13]), skill=int(r[14]))
        if any(b in i["name"].lower() for b in BAD_NAME):
            continue
        items[i["id"]] = i
    vendor = {int(r[0]): (int(r[1]), int(r[2])) for r in rows(os.path.join(src, "vendor.tsv"))}
    loot = {int(r[0]): (int(r[1]), int(r[2])) for r in rows(os.path.join(src, "loot.tsv"))}
    quest = {}   # item -> list of (race mask, quest id, title)
    for r in rows(os.path.join(src, "quest.tsv")):
        for col in r[3:]:
            if col and col != "0":
                quest.setdefault(int(col), []).append((int(r[2] or 0), int(r[0]), r[1]))

    def source(item, role_class):
        """(rank, text): quest 0, vendor 1, drop 2, none 3."""
        race = RACE_BIT[role_class]
        qs = [q for q in quest.get(item["id"], []) if q[0] in (0, -1) or q[0] & race]
        if qs:
            q = sorted(qs, key=lambda x: x[1])[0]
            return 0, "quest (%d %s)" % (q[1], q[2])
        if item["id"] in vendor:
            e, n = vendor[item["id"]]
            return 1, "vendor (%s%s)" % (e, (" " + names[str(e)]) if str(e) in names else "") + (", %d vendors" % n if n > 1 else "")
        if item["id"] in loot:
            e, n = loot[item["id"]]
            return 2, "drop (%d %s)" % (e, names.get(str(e), "creature"))
        return 3, "none"

    def pick(role, cls, wkey, level, slot_types, classes, subs, taken, need_weights=True):
        """The best candidate for a slot: allowed by class bit, armor type or weapon type, level, with a source."""
        weights = W[wkey]
        best = None
        for i in items.values():
            if i["inv"] not in slot_types or i["cls"] not in classes or i["sub"] not in subs or i["id"] in taken:
                continue
            if i["req"] > level or i["ilvl"] > level + 9 or i["ilvl"] < level - 8 or i["skill"]:
                continue
            if i["allow"] != -1 and not (i["allow"] & CLASS_BIT[cls]):
                continue
            rank, text = source(i, cls)
            if rank == 3:
                continue
            sc = score(i, weights)
            # quest/vendor preferred (as the warrior kit): a drop only wins by a clear margin, so the console-handable item comes first
            key = (sc - 2.0 * rank, i["ilvl"], -i["id"])
            if sc <= 0 and need_weights:
                continue
            if best is None or key > best[0]:
                best = (key, i, text, rank)
        return best

    for level in (18, 22):
        fw = list(csv.DictReader(open(FOODWATER)))
        lines = [["role", "slot", "item_id", "name", "source", "reason"]]
        for role, cls, wkey, armor_pref, weapon_subs, offkind in ROLES:
            taken = set()
            for slot in SLOTS:
                got = None
                if slot in BODY:
                    for sub in armor_pref:          # best armor type first; a cloak is always cloth (subclass 1)
                        classes_sub = [1] if slot == "back" else [sub]
                        got = pick(role, cls, wkey, level, BODY[slot], [4], classes_sub, taken)
                        if got:
                            break
                    if not got:     # nothing with a useful stat: the piece with the most armor in the best armor type
                        for sub in armor_pref:
                            classes_sub = [1] if slot == "back" else [sub]
                            got = pick(role, cls, "armoronly", level, BODY[slot], [4], classes_sub, taken, need_weights=False)
                            if got:
                                break
                elif slot in ("neck", "finger", "finger2"):
                    got = pick(role, cls, wkey, level, JEWEL[slot.rstrip("2")], [4], [0], taken)
                elif slot in ("trinket", "trinket2"):
                    got = pick(role, cls, wkey, level, JEWEL["trinket"], [4], [0], taken)
                elif slot == "main_hand":
                    if offkind == "twohand":
                        got = pick(role, cls, wkey, level, [17], [2], weapon_subs, taken, need_weights=False)
                    else:
                        got = pick(role, cls, wkey, level, [13, 21], [2], weapon_subs, taken, need_weights=False)
                elif slot == "off_hand":
                    if offkind == "shield":
                        got = pick(role, cls, wkey, level, [14], [4], [6], taken)
                    elif offkind == "held":
                        got = pick(role, cls, wkey, level, [23], [4], [0], taken)
                elif slot == "ranged":
                    # a hunter's bow (class 2 subclass 2, inventory type 15), a warlock's wand (subclass 19, inventory type 26)
                    got = pick(role, cls, wkey, level, [15] if cls == "hunter" else [26], [2], [2] if cls == "hunter" else [19], taken, need_weights=False)
                if got:
                    _, i, text, rank = got
                    taken.add(i["id"])
                    reason = ("quest/vendor preferred, console-handable" if rank < 2 else "drop; no quest or vendor source at <=%d in the world DB" % level)
                    lines.append([role, slot, i["id"], i["name"], text, reason + "; ilvl %d req %d" % (i["ilvl"], i["req"])])
                else:
                    why = {"off_hand": "two-hander in the main hand" if offkind == "twohand" else "no wearable %s at <=%d with a source" % (offkind, level),
                           "ranged": "no %s with a source at <=%d" % ("bow" if cls == "hunter" else "wand", level)}.get(slot, "no wearable %s for %s at <=%d with a source" % (slot, role, level))
                    lines.append([role, slot, "", "", "none", "EMPTY: " + why])
            # food and drink: the highest-level bread/juice with many alliance vendors (food-water-5-25.csv)
            for kind in ("food", "drink"):
                cand = [r for r in fw if r["kind"] == kind and int(r["required_level"]) <= level]
                cand.sort(key=lambda r: (int(r["required_level"]) >= 15, int(r["alliance_vendor_count"]), int(r["vendor_count"])), reverse=True)
                r = cand[0]
                lines.append([role, kind, r["item_id"], r["name"], "vendor (%s alliance vendors)" % r["alliance_vendor_count"],
                              "widely stocked alliance vendor food/water (req %s)" % r["required_level"]])
        with open(os.path.join(out, "kits-level%d.csv" % level), "w", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows(lines)


if __name__ == "__main__":
    main()
