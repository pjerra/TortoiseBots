#!/usr/bin/env python3
"""Wide pass 2 (branch port/wide-pass-2): the call-site rewrites of families 2, 3 and 4. Family 1 (names) is
forever/tools/w2_names.py. Files in forever/tools/wide2-exclude.txt, the compat headers and everything under forever/ are never
touched. Every rewrite keeps one call, the same arguments, evaluated once, and never lands inside a comment or a string.

    python3 forever/tools/rewrite_wide_pass2.py --family config [--check]
    python3 forever/tools/rewrite_wide_pass2.py --family groupid [--check]     (reads the keep list in this file)
    python3 forever/tools/rewrite_wide_pass2.py --family position [--check]
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w2_names
from rewrite_wide_pass import comment_spans

ROOT = w2_names.ROOT
HELPER = {"UINT32": "WorldU32ConfigW2", "INT32": "WorldI32ConfigW2", "BOOL": "WorldBoolConfigW2", "FLOAT": "WorldFloatConfigW2"}
CONFIG = re.compile(r"\bsWorld\.getConfig\(\s*(CONFIG_(UINT32|INT32|BOOL|FLOAT)_[A-Z0-9_]+)\s*\)")


def files():
    ex = w2_names.excluded()
    for top in ("ai", "commands", "runtime", "host", "behavior"):
        for dp, _, fn in os.walk(os.path.join(ROOT, top)):
            for f in fn:
                if f.endswith((".cpp", ".h", ".hpp")):
                    rel = os.path.relpath(os.path.join(dp, f), ROOT)
                    if rel not in ex and not rel.startswith("ai/cmangos-compat"):
                        yield rel


def rewrite(rel, pattern, repl, check, keep=lambda ln, text: False):
    path = os.path.join(ROOT, rel)
    lines = open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
    n = 0
    for i, line in enumerate(lines):
        spans = comment_spans(line)
        out, pos = [], 0
        for m in pattern.finditer(line):
            if any(a <= m.start() < b for a, b in spans) or keep(i + 1, line):
                continue
            out.append(line[pos:m.start()])
            out.append(repl(m))
            pos = m.end()
            n += 1
        if out:
            out.append(line[pos:])
            new = "".join(out)
            if check:
                print("%s:%d\n  - %s\n  + %s" % (rel, i + 1, line.strip(), new.strip()))
            lines[i] = new
    if n and not check:
        open(path, "w", encoding="utf-8", errors="surrogateescape").write("\n".join(lines))
        w2_names.add_include(rel)
    return n


def main():
    args = sys.argv[1:]
    check = "--check" in args
    fam = args[args.index("--family") + 1]
    total, touched = 0, 0
    for rel in sorted(files()):
        if fam == "config":
            n = rewrite(rel, CONFIG, lambda m: HELPER[m.group(2)] + "(" + m.group(1) + ")", check)
        else:
            sys.exit("unknown family " + fam)
        total += n
        touched += 1 if n else 0
    print("family %s: %d replacements in %d files" % (fam, total, touched))


if __name__ == "__main__":
    main()
