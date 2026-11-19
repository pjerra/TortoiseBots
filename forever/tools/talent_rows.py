#!/usr/bin/env python3
"""The talent loader of the live setup (G8, item 6): turns a talents CSV (forever/data/waterys/rfc/talents-level18.csv) into the `.tb learn` spell list per role.

    python3 forever/tools/talent_rows.py <csv> [--role "<role>"]        # print the plan and the report
    python3 forever/tools/talent_rows.py --selftest

One CSV row is one talent POINT (columns role, order, talent_id, spell_id, rank, ...). A talent with N points needs N DIFFERENT spell ids, one per rank (a rank
is its own spell, and a known spell cannot be learned twice). The loader therefore reads a per-rank id when the data has one: the rows of one talent_id carry
distinct spell_ids, or a `rank` column above 1 with its own spell_id. When the data repeats ONE spell id on every row of a talent (the case of
talents-level18.csv: rank 1 on every row), only the first point is learnable and the other rows are reported as UNLEARNABLE (the data owner adds the per-rank ids;
Talent.db2 has them in SpellRank[]). Nothing is invented: no id is derived from another.
Exit 0 always for a report; --selftest exits non-zero on a failed check.
"""
import csv, sys, collections

def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))

def plan(rows):
    """-> {role: {"learn": [(talent_id, rank, spell_id)], "unlearnable": [(order, talent_id, spell_id, reason)]}}"""
    out = {}
    per = collections.OrderedDict()
    for r in rows:
        per.setdefault((r["role"], r["talent_id"]), []).append(r)
    for (role, tid), pts in per.items():
        o = out.setdefault(role, {"learn": [], "unlearnable": []})
        seen = set()
        for i, r in enumerate(sorted(pts, key=lambda x: int(x["order"]))):
            sid, rank = int(r["spell_id"]), int(r["rank"] or 1)
            if sid in seen:
                o["unlearnable"].append((int(r["order"]), int(tid), sid,
                                         "point %d of talent %s repeats spell %d (no per-rank id in the data)" % (i + 1, tid, sid)))
                continue
            seen.add(sid)
            o["learn"].append((int(tid), max(rank, i + 1) if rank > 1 else rank, sid))
    return out

def report(p, only=None):
    lines = []
    total_bad = 0
    for role, d in p.items():
        if only and role != only:
            continue
        lines.append("%s: learn %d spell ids, %d point(s) UNLEARNABLE" % (role, len(d["learn"]), len(d["unlearnable"])))
        lines.append("  .tb learn ids: " + " ".join(str(s) for _, _, s in d["learn"]))
        for order, tid, sid, why in d["unlearnable"]:
            lines.append("  UNLEARNABLE order %d talent %d: %s" % (order, tid, why))
        total_bad += len(d["unlearnable"])
    lines.append("total unlearnable points: %d" % total_bad)
    return "\n".join(lines)

def selftest():
    fails = 0
    def check(c, m):
        nonlocal fails
        if not c:
            fails += 1
            print("FAIL", m)
    base = [dict(role="r", order=str(i + 1), talent_id="5", spell_id="100", rank="1") for i in range(3)]
    p = plan(base)["r"]
    check(len(p["learn"]) == 1 and len(p["unlearnable"]) == 2, "one repeated id: first point learnable, two reported")
    per_rank = [dict(role="r", order=str(i + 1), talent_id="5", spell_id=str(100 + i), rank=str(i + 1)) for i in range(3)]
    p = plan(per_rank)["r"]
    check([s for _, _, s in p["learn"]] == [100, 101, 102] and not p["unlearnable"], "per-rank ids are all learnable")
    check([k for _, k, _ in p["learn"]] == [1, 2, 3], "ranks follow the points")
    p = plan(base + [dict(role="r", order="4", talent_id="6", spell_id="200", rank="1")])["r"]
    check(len(p["learn"]) == 2, "a second talent is its own group")
    print("talent_rows selftest: %s" % ("ok" if not fails else "%d failed" % fails))
    return fails

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(1 if selftest() else 0)
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    only = sys.argv[sys.argv.index("--role") + 1] if "--role" in sys.argv else None
    print(report(plan(load(sys.argv[1])), only))
