#!/usr/bin/env python3
"""Turn the SQL files of one dungeon plan into the tab-separated rows forever/tools/facts_trace.cpp reads.

    python3 forever/tools/facts_rfc_rows.py <plan> <out.tsv> [sql file ...]

Default files: data/sql/world/20261113203000_world.sql (the plan, exported by turtle-agent plan-export: 9 legs, 84 steps for
ragefire_chasm) and data/sql/world/20261021114931_world.sql (the ai_dungeon_boss rows). Only INSERT/REPLACE rows of the
ai_dungeon_plan / ai_dungeon_leg / ai_dungeon_step / ai_dungeon_boss tables are read (the plan filter applies to the first three).
"""
import os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT = ["data/sql/world/20261113203000_world.sql", "data/sql/world/20261021114931_world.sql"]


def tuples(text):
    """The parenthesised value tuples of one INSERT ... VALUES statement, each as a list of python values."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text[i] != "(":
            i += 1
            continue
        i += 1
        row, tok = [], ""
        while i < n:
            c = text[i]
            if c == "'":
                s, i = "", i + 1
                while i < n:
                    if text[i] == "\\" and i + 1 < n:
                        s += text[i + 1]; i += 2; continue
                    if text[i] == "'":
                        if i + 1 < n and text[i + 1] == "'":
                            s += "'"; i += 2; continue
                        break
                    s += text[i]; i += 1
                i += 1
                row.append(s)
                tok = ""
            elif c in ",)":
                if tok.strip():
                    v = tok.strip()
                    row.append(None if v.upper() == "NULL" else v)
                tok = ""
                i += 1
                if c == ")":
                    break
            else:
                tok += c; i += 1
        out.append(row)
    return out


def main():
    plan, out = sys.argv[1], sys.argv[2]
    files = sys.argv[3:] or DEFAULT
    rows = []
    for fn in files:
        path = fn if os.path.isabs(fn) else os.path.join(ROOT, fn)
        text = open(path, encoding="utf-8", errors="replace").read()
        text = re.sub(r"--[^\n]*", "", text)
        for m in re.finditer(r"(?:INSERT|REPLACE)\s+INTO\s+`(\w+)`\s*\(([^)]*)\)\s*VALUES(.*?);", text, re.S | re.I):
            table = m.group(1)
            cols = [c.strip(" `") for c in m.group(2).split(",")]
            for t in tuples(m.group(3)):
                d = dict(zip(cols, t))
                if table == "ai_dungeon_plan" and d["plan"] == plan:
                    rows.append(["plan", d["plan"], d["map"], d.get("cc_priority") or "", d.get("cc_always") or "",
                                 d.get("kill_first") or "", d.get("cc_never") or ""])
                elif table == "ai_dungeon_leg" and d["plan"] == plan:
                    rows.append(["leg", d["plan"], d["leg"], d["name"]])
                elif table == "ai_dungeon_step" and d["plan"] == plan:
                    rows.append(["step", d["plan"], d["leg"], d["seq"], d["kind"], d["entry"], d["x"], d["y"], d["z"],
                                 d["radius"], d["item"], d["param"], d["flags"], d["timeout_s"], d.get("entries") or "",
                                 d.get("comment") or ""])
                elif table == "ai_dungeon_boss":
                    pmap = next((r[2] for r in rows if r[0] == "plan"), None)
                    rows.append(["boss", d["map"], d["entry"]])
    pmap = next(r[2] for r in rows if r[0] == "plan")
    rows = [r for r in rows if r[0] != "boss" or r[1] == pmap]
    with open(out, "w") as o:
        for r in rows:
            o.write("\t".join(str(x) for x in r) + "\n")
    kinds = {}
    for r in rows:
        kinds[r[0]] = kinds.get(r[0], 0) + 1
    print(" ".join(f"{k}={v}" for k, v in kinds.items()), file=sys.stderr)


if __name__ == "__main__":
    main()
