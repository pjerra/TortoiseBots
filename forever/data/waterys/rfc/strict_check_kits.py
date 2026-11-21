import csv, pickle, struct, re, sys
import os
# Usage: ITEM_DB2=<70959 Item.db2> ARMORY_PKL=<armory item pickle> python3 strict_check_kits.py
W = os.path.dirname(os.path.abspath(__file__))
arm = pickle.load(open(os.environ.get("ARMORY_PKL", "r11_armory.pkl"), "rb"))
item_bin = open(os.environ.get("ITEM_DB2", "Item.db2"), "rb").read()
import csv as _csv
GQ = {}
for _r in _csv.DictReader(open(os.path.join(W, "gear-level18.csv"))):
    if _r["source_type"] == "quest":
        try: GQ[_r["item_id"]] = int(_r["source_level"])
        except ValueError: pass
QRE = re.compile(r"quest \((\d+)")
BADNAME = re.compile(r"\btest\b|epic|90 |naxx|trial|deprecated|unused|\bQA\b|\bGM\b|Mock\b", re.I)
WEAR = {"mage": [(4, 1)], "rogue": [(4, 2)], "priest": [(4, 1)],
        "protection warrior": [(4, 3)], "arms warrior": [(4, 3)]}
INV = {"head": [1], "neck": [2], "shoulder": [3], "back": [16], "chest": [5, 20],
       "wrist": [9], "hands": [10], "waist": [6], "legs": [7], "feet": [8],
       "finger": [11], "finger2": [11], "trinket": [12], "trinket2": [12],
       "main_hand": [21, 13], "off_hand": [22, 23, 13, 14, 17],
       "two_hand": [17], "ranged": [25, 16, 2, 8, 19, 26, 15]}
MELEE_OK = {"rogue": [15, 7, 13, 4, 16, 2, 3, 18, 19], "mage": [15, 7, 10, 4, 19, 21],
            "priest": [15, 10, 4, 19], "protection warrior": [15, 7, 10, 4, 13, 21, 14, 17, 22, 23, 16, 2, 3, 1, 5, 6, 8],
            "arms warrior": [15, 7, 10, 4, 13, 21, 14, 17, 22, 23, 16, 2, 3, 6, 8, 1, 5]}
fails = []
for fn, lvl, cap in [("kits-level18.csv", 18, 25), ("kits-level22.csv", 22, 32)]:
    rows = list(csv.DictReader(open(W + "/" + fn)))
    for r in rows:
        if not r["item_id"] or r["slot"] in ("food", "drink", "bandage"):
            continue
        tag = "%s %s/%s %s" % (fn, r["role"], r["slot"], r["item_id"])
        try:
            iid = int(r["item_id"])
        except (ValueError, TypeError):
            fails.append((tag, "bad item id")); continue
        a = arm.get(iid)
        if a is None:
            fails.append((tag, "not in armory")); continue
        nm = a["name"] or ""
        if BADNAME.search(nm) or BADNAME.search(r.get("name", "")):
            fails.append((tag, "bad name %r" % nm[:35]))
        if a["q"] not in (2, 3) or (a["q"] == 3 and "quest" not in (r.get("source","")+r.get("reason","")).lower()):
            # green always ok; blue only when the row is quest-sourced (warrior/priest kits)
            fails.append((tag, "quality %d not green/quest-blue" % a["q"]))
        if not (0 <= a["req"] <= lvl):
            fails.append((tag, "req %d not in 1..%d" % (a["req"], lvl)))
        if a["ilvl"] > cap:
            fails.append((tag, "ilvl %d over cap %d" % (a["ilvl"], cap)))
        slot = r["slot"]
        if a["slot"] not in INV[slot]:
            fails.append((tag, "invType %d not slot %s" % (a["slot"], slot)))
        if a["cls"] == 2 and slot in ("main_hand", "off_hand", "two_hand", "ranged"):
            if a["sub"] not in MELEE_OK[r["role"]] and a["slot"] not in (2, 16, 11, 12):
                fails.append((tag, "weapon sub %d not for %s" % (a["sub"], r["role"])))
        elif a["cls"] == 4:
            if slot in ("back",):  # cloaks: cloth-textured, wearable by all
                pass
            elif a["sub"] != 0 and (a["cls"], a["sub"]) not in WEAR[r["role"]]:
                fails.append((tag, "armor (%d,%d) not for %s" % (a["cls"], a["sub"], r["role"])))
        if struct.pack("<I", iid) not in item_bin:
            fails.append((tag, "absent from 70959 Item.db2"))
        m = QRE.search(r.get("source","") + " " + r.get("reason",""))
        if m and r["item_id"] in GQ and GQ[r["item_id"]] > lvl + 2:
            fails.append((tag, "quest source level %d > kit %d + 2" % (GQ[r["item_id"]], lvl)))
print("failures:", len(fails))
for t, w in fails:
    print(" ", t, "=>", w)
