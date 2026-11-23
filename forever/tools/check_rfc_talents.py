import csv, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from talent_builds import load_trees, check_build
TREES = load_trees()
R = os.path.join(HERE, "..", "data", "waterys", "rfc") + os.sep
def steps(rows):
    rows = sorted(rows, key=lambda r: int(r["order"]))
    out = []
    for r in rows:
        tid = int(r["talent_id"])
        if out and out[-1][0] == tid:
            out[-1][1] += 1
        else:
            out.append([tid, 1])
    return [(a, b) for a, b in out]
CLASS = {"protection warrior": "warrior", "arms warrior": "warrior", "priest": "priest", "mage": "mage", "rogue": "rogue"}
r18 = list(csv.DictReader(open(R + "talents-level18.csv")))
r22 = list(csv.DictReader(open(R + "talents-level22.csv")))
fails = 0
for role, cls in CLASS.items():
    s18 = steps([r for r in r18 if r["role"] == role])
    sall = steps([r for r in r18 + r22 if r["role"] == role])
    for name, s, pts in [("L18", s18, 9), ("L18+L22", sall, 13)]:
        errs, spent = check_build(TREES[cls], s)
        errs = [e for e in errs if "spends" not in e]
        total = sum(spent.values())
        ok = not errs and total == pts
        if not ok: fails += 1
        print("%-18s %s %s %s" % (role, name, "OK %dpts" % total if ok else "FAIL(%dpts)" % total, "" if ok else errs[:6]))
print("RFC-FAILURES:", fails)
