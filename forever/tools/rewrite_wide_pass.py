#!/usr/bin/env python3
"""Rewrite the mechanical call families of the wide pass (WIDE-PASS-R1) onto helpers that already exist.

Every helper used here has an identity body on the Turtle core (ai/cmangos-compat-r3.h / -r4*.h), so the line means the same
call as before there; on ForeverCore it is the call that core needs.

  --player   sObjectMgr.GetPlayer(guid)            -> GetPlayerCompat(guid)
  --guidkey  <receiver>.GetRawValue()              -> GuidKeyCompat(<receiver>)   (a guid used as a 64-bit key)

`--guidkey` skips a place when the value cannot be a key on ForeverCore: a stream or packet write (`<<`, `WorldPacket`,
`Enqueue(`, `Write(`, ...) or a place that turns the number back into a guid (`ObjectGuid(`, `GuidPosition(`,
`GuidFromRawCompat(`). Those are listed in forever/docs/WIDE-PASS-R1-NOTES.md, not rewritten.

    python3 forever/tools/rewrite_wide_pass.py [--check] [--player|--guidkey] [paths...]

Default paths: every .cpp under the repository outside forever/ and tools/, minus forever/tools/wide-pass-exclude.txt (the
three slice lists and their same-base headers). --check prints what would change and writes nothing.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXCLUDE_FILE = os.path.join(ROOT, "forever", "tools", "wide-pass-exclude.txt")
DEFAULT_PATHS = [""]
SKIP_PREFIXES = ("forever/", "tools/", ".git/")
SKIP_FILES = {"ai/cmangos-compat-shim.h", "ai/cmangos-compat-r3.h", "ai/cmangos-compat-r4a.h", "ai/cmangos-compat-r4c.h",
              "ai/cmangos-compat-r4p.h", "ai/cmangos-compat-r4v.h", "ai/cmangos-compat-unported.h"}

# The guid overload only: `GetPlayer(name)` is a name lookup on Turtle and has no ForeverCore counterpart in this pass.
PLAYER_PATTERN = re.compile(r"\bsObjectMgr\.GetPlayer\((?![^)]*(?:\.c_str\(\)|\"))")
# A guid used as a 64-bit key: `<simple receiver>.GetRawValue()`. The receiver is a chain of identifiers and zero-argument
# calls (`guid`, `bot->GetObjectGuid()`, `itr->first`), which is what every key site in the tree is; anything else (a
# comparison, a ternary, a dereference) is left alone and listed.
GUIDKEY_PATTERN = re.compile(r"(?<![\w.):>])([A-Za-z_]\w*(?:(?:->|\.)[A-Za-z_]\w*(?:\(\))?)*)\.GetRawValue\(\)")
# A value that is not a key: it is written to a wire or a stream, turned back into a guid, or built by a ternary.
NONKEY_MARKERS = (re.compile(r"<<"),
                  re.compile(r"\bWorldPacket\b|QueuePacket|\bEnqueue\(|\bWrite\(|AppendPackedGuid|appendPackGUID|\bByteBuffer\b"),
                  re.compile(r"(?<!get)(?<!Get)ObjectGuid\(|GuidPosition\(|GuidFromRawCompat\(|guidStr|printf|SetFacingGUID\("),
                  re.compile(r" \? "))

def comment_spans(text):
    """Character ranges that are comments or string literals, so a rewrite never lands inside one."""
    spans = []
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            end = text.find("\n", i)
            end = n if end < 0 else end
            spans.append((i, end))
            i = end
        elif two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            spans.append((i, end))
            i = end
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                j += 1
            spans.append((i, j))
            i = j
        else:
            i += 1
    return spans


def in_spans(pos, spans):
    return any(a <= pos < b for a, b in spans)


def receiver_start(text, end):
    """(start, end) of the receiver expression that ends before the `G` of GetRawValue; (end, end) when it is not one."""
    j = end
    if text[j - 2:j] in ("->", "::"):
        j -= 2
    elif j > 0 and text[j - 1] == ".":
        j -= 1
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.>-:()")
    i = j
    depth = 0
    while i > 0 and text[i - 1] in allowed:
        c = text[i - 1]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
            if depth < 0:
                return end, end
        i -= 1
    if depth != 0 or i == j:
        return end, end
    receiver = text[i:j]
    if receiver[0] in ".-:)" or receiver[-1] in ".-:(":
        return end, end
    return i, j


# A file that turns a number back into a guid or an online player is a judgement (the key and the guid must agree); the
# whole file is left for the slices, because a value written in one place is read back in another.
RECONSTRUCTS = re.compile(r"(?<!get)(?<!Get)ObjectGuid\(|GuidPosition\(|GuidFromRawCompat\(|FindCharacter\(|QueuePublicNote\(")


def guidkey_edits(text):
    spans = comment_spans(text)
    edits = []
    for m in GUIDKEY_PATTERN.finditer(text):
        if in_spans(m.start(), spans):
            continue
        line_start = text.rfind("\n", 0, m.start()) + 1
        line_end = text.find("\n", m.end())
        line_end = len(text) if line_end < 0 else line_end
        if any(p.search(text[line_start:line_end]) for p in NONKEY_MARKERS):
            continue
        edits.append((m.start(1), m.end(1)))
    return edits


def apply_edits(text, edits, wrap):
    for rstart, rend in sorted(edits, reverse=True):
        text = text[:rstart] + wrap(text[rstart:rend]) + text[rend + len(".GetRawValue()"):]
    return text


def files(paths):
    excluded = set()
    if os.path.exists(EXCLUDE_FILE):
        for line in open(EXCLUDE_FILE):
            line = line.strip()
            if line and not line.startswith("#"):
                excluded.add(os.path.normpath(line))
    found = []
    for p in paths:
        base = os.path.join(ROOT, p) if p else ROOT
        if os.path.isfile(base):
            found.append(os.path.relpath(base, ROOT))
            continue
        for d, dirs, names in os.walk(base):
            rel = os.path.relpath(d, ROOT)
            rel = "" if rel == "." else rel + "/"
            if any((rel + n).startswith(SKIP_PREFIXES) for n in [""]):
                dirs[:] = []
                continue
            for n in names:
                if not n.endswith((".cpp", ".h")):
                    continue
                path = os.path.normpath(os.path.join(rel, n))
                if path.startswith(SKIP_PREFIXES) or path in excluded or path in SKIP_FILES:
                    continue
                found.append(path)
    return sorted(set(found))


def main():
    args = [a for a in sys.argv[1:]]
    check = "--check" in args
    families = [a[2:] for a in args if a in ("--player", "--guidkey")] or ["player", "guidkey"]
    paths = [a for a in args if not a.startswith("--")] or DEFAULT_PATHS
    changed_files = 0
    total = 0
    for path in files(paths):
        full = os.path.join(ROOT, path)
        try:
            text = open(full, encoding="utf-8").read()
        except UnicodeDecodeError:
            continue
        new = text
        hits = 0
        if "player" in families:
            new, n = PLAYER_PATTERN.subn("GetPlayerCompat(", new)
            hits += n
        if "guidkey" in families:
            edits = [] if RECONSTRUCTS.search(new) else guidkey_edits(new)
            if edits:
                for rstart, rend in sorted(edits, reverse=True):
                    line_start = new.rfind("\n", 0, rstart) + 1
                    line_end = new.find("\n", rend)
                    line_end = len(new) if line_end < 0 else line_end
                    print(f"{path}: {new[line_start:line_end].strip()}")
                hits += len(edits)
                new = apply_edits(new, edits, lambda r: "GuidKeyCompat(" + r + ")")
        if hits:
            total += hits
            changed_files += 1
            print(f"--- {path}: {hits} place(s)", file=sys.stderr)
            if not check:
                open(full, "w", encoding="utf-8").write(new)
    print(f"{'would change' if check else 'changed'}: {total} place(s) in {changed_files} file(s)")


if __name__ == "__main__":
    main()
