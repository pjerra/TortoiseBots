#!/usr/bin/env python3
"""Lists every `File.ext:LINE[-LINE]` cite in forever/ (code comments; docs with --docs) that points into a ForeverCore file
which changed between two core commits, and says where the cited line went.

  forever/tools/check_core_cites.py [--core ~/ForeverCore] [--old 6517af0] [--new 25fc7ca] [--docs] [--apply]

A cite names a core file by its base name. Turtle-core cites (`PathFinder.h:146`, T files) and cites into files the move did
not touch are not listed. For each cite into a changed file: the line at the OLD commit (what the comment meant), the line
number that text has at the NEW commit (from `git diff -U0` arithmetic), and the text at the new number. `MOVED` = the same
line is somewhere else now; `GONE` = the old line was edited or deleted (read it by hand); `same` = unchanged.
--apply rewrites the MOVED cites in place (a range moves as a whole only when both ends moved by the same amount).
A cite whose nearest preceding bare `T` or `F` marker on its line is T is a Turtle-core cite: skipped (the comments
in the compat headers write `T Unit.cpp:11035 ... F Unit.cpp:749`). A cite with no marker on its line is read as ForeverCore
and listed with `?` so a reviewer can look at it (multi-line comments carry the marker on an earlier line).
A basename that exists more than once in the core is reported AMBIGUOUS and never rewritten. The script reads the core
through `git show`, so it works on a sparse checkout; it changes no core file.
"""
import argparse, os, re, subprocess, sys

MARK = re.compile(r'(?<![A-Za-z0-9_`])([TF])(?![A-Za-z0-9_`])')
CITE = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*\.(?:cpp|h|hpp|inc)):(\d+)(?:-(\d+))?')


def git(core, *a):
    return subprocess.run(['git', '-C', core, *a], capture_output=True, text=True, errors='replace').stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--core', default=os.path.expanduser('~/ForeverCore'))
    ap.add_argument('--old', default='6517af0')
    ap.add_argument('--new', default='25fc7ca')
    ap.add_argument('--docs', action='store_true', help='also scan forever/docs/PORT-PLAN.md')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

    tree = git(args.core, 'ls-tree', '-r', '--name-only', args.new, '--', 'src').split('\n')
    by_base = {}
    for p in tree:
        if p:
            by_base.setdefault(os.path.basename(p), []).append(p)
    changed = [p for p in git(args.core, 'diff', '--name-only', args.old, args.new, '--', 'src').split('\n') if p]

    # old line -> new line maps (None = edited or deleted) for every changed file, from -U0 hunks
    maps, oldtext, newtext = {}, {}, {}
    for p in changed:
        hunks = []
        for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', git(args.core, 'diff', '-U0', args.old, args.new, '--', p), re.M):
            oa, oc, na, nc = int(m[1]), int(m[2] if m[2] is not None else 1), int(m[3]), int(m[4] if m[4] is not None else 1)
            hunks.append((oa, oc, na, nc))
        maps[p] = hunks
        oldtext[p] = git(args.core, 'show', f'{args.old}:{p}').split('\n')
        newtext[p] = git(args.core, 'show', f'{args.new}:{p}').split('\n')

    def to_new(p, n):
        shift = 0
        for oa, oc, na, nc in maps[p]:
            # a pure insertion (oc == 0) after old line oa
            if oc == 0:
                if oa < n:
                    shift += nc
                continue
            if n < oa:
                break
            if n < oa + oc:
                return None
            shift += nc - oc
        return n + shift

    files = []
    for dp, dn, fn in os.walk(os.path.join(root)):
        rel = os.path.relpath(dp, root)
        if rel.startswith('docs') and not args.docs:
            continue
        for f in fn:
            full = os.path.join(dp, f)
            r = os.path.relpath(full, root)
            if r.startswith('docs'):
                if not (args.docs and r == 'docs/PORT-PLAN.md'):
                    continue
            if f.endswith(('.h', '.cpp', '.py', '.cmake', '.sh', '.md', '.txt', '.in')) and f != 'check_core_cites.py':
                files.append(full)

    rows = []
    for full in sorted(files):
        try:
            lines = open(full, encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            continue
        for i, ln in enumerate(lines, 1):
            for m in CITE.finditer(ln):
                base = m[1]
                marks = MARK.findall(ln[:m.start()])
                if marks and marks[-1] == 'T':
                    continue
                unmarked = not marks
                cands = [p for p in by_base.get(base, []) if p in maps]
                allc = by_base.get(base, [])
                if not cands:
                    continue
                if len(allc) > 1:
                    rows.append((full, i, m[0], 'AMBIGUOUS', f'{len(allc)} files named {base}', '', None))
                    continue
                p = cands[0]
                a = int(m[2]); b = int(m[3]) if m[3] else None
                na = to_new(p, a)
                nb = to_new(p, b) if b else None
                ot = oldtext[p][a - 1].strip() if a <= len(oldtext[p]) else ''
                if na is None or (b and nb is None):
                    status = 'GONE'
                elif na == a and (not b or nb == b):
                    status = 'same'
                elif b and (nb - na) != (b - a):
                    status = 'GONE'
                else:
                    status = 'MOVED'
                nt = newtext[p][na - 1].strip() if na and na <= len(newtext[p]) else ''
                new_cite = None
                if status == 'MOVED':
                    new_cite = f'{base}:{na}' + (f'-{nb}' if b else '')
                if unmarked and status != 'same' and ln.lstrip().startswith(('//', '*', '#')) and not os.path.basename(full).endswith('.md'):
                    status += '?'
                rows.append((full, i, m[0], status, ot[:90], f'-> {na}' + (f'-{nb}' if b else '') + f': {nt[:90]}' if na else '', new_cite))

    for full, i, cite, status, ot, nt, nc in rows:
        if status != 'same':
            print(f'{status:9} {os.path.relpath(full, root)}:{i}  {cite}  old: {ot}  {nt}')
    print(f'-- {len(rows)} cites into {len(changed)} changed core files: ' +
          ', '.join(f'{s} {sum(1 for r in rows if r[3] == s)}' for s in ('same', 'MOVED', 'MOVED?', 'GONE', 'GONE?', 'AMBIGUOUS')))

    if args.apply:
        edits = {}
        for full, i, cite, status, ot, nt, nc in rows:
            if status in ('MOVED', 'MOVED?') and nc:
                edits.setdefault(full, []).append((i, cite, nc))
        for full, es in edits.items():
            lines = open(full, encoding='utf-8', errors='replace').read().split('\n')
            for i, cite, nc in es:
                lines[i - 1] = lines[i - 1].replace(cite, nc, 1)
            open(full, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
            print(f'rewrote {len(es)} cite(s) in {os.path.relpath(full, root)}')


if __name__ == '__main__':
    main()
