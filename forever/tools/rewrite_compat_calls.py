#!/usr/bin/env python3
"""Rewrite the two mechanical call families of shim round 2, fix round 1, in the shared bot code.

B1: `sObjectMgr.GetItemPrototype(id)` (and `ObjectMgr::GetItemPrototype(id)`) -> `GetItemProtoByIdCompat(id)`.
    On the Turtle core that helper is the same call; on ForeverCore it is the item mirror by entry
    (TbItemPrototype.h). `sItemStorage.LookupEntry<ItemPrototype>(id)` already works through both shims and is not touched.

B2: `X->IsMoving()` -> `IsMovingCompat(X)`. `X` is a core Unit/Player/Creature at every site (the receiver types were
    checked by hand; no bot class has an IsMoving member). On the Turtle core the helper calls IsMoving(); on
    ForeverCore isMoving().

    python3 forever/tools/rewrite_compat_calls.py [--check] [--b1|--b2] [paths...]

Default paths: ai/ runtime/ commands/ (the shared bot code) and forever/src/ (the module's own sources). The shims keep
their own bodies: they are skipped. --check prints what would change and writes nothing.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_PATHS = ["ai", "runtime", "commands", "forever/src"]
# The shims define these helpers themselves, so their bodies spell the core call directly.
SKIP_FILES = {
    "ai/cmangos-compat-shim.h",
    "forever/src/compat/forever-compat-shim.h",
}
B1_CALL = "GetItemProtoByIdCompat"
B2_CALL = "IsMovingCompat"
B1_PATTERNS = ["sObjectMgr.GetItemPrototype(", "ObjectMgr::GetItemPrototype("]


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


def in_span(spans, pos):
    for start, end in spans:
        if start <= pos < end:
            return True
        if start > pos:
            break
    return False


def is_ident_char(ch):
    return ch.isalnum() or ch == "_"


def receiver_start(text, arrow):
    """Start of the expression whose member is fetched, given the position of the `->` of `->IsMoving()`."""
    i = arrow - 1
    while i >= 0:
        while i >= 0 and text[i] in " \t\n":
            i -= 1
        if i < 0:
            return None
        ch = text[i]
        if ch in ")]":
            close = ch
            open_ = "(" if ch == ")" else "["
            depth = 0
            while i >= 0:
                if text[i] == close:
                    depth += 1
                elif text[i] == open_:
                    depth -= 1
                    if depth == 0:
                        break
                i -= 1
            if i < 0:
                return None
            group_start = i
            # `f(...)` and `a[...]` continue with the name/object in front; `(*p)` and `(a ? b : c)` are the expression
            j = group_start - 1
            while j >= 0 and text[j] in " \t":
                j -= 1
            if j >= 0 and is_ident_char(text[j]):
                i = j
                continue
            if j >= 1 and text[j] == ">" and text[j - 1] == "-":
                i = j - 2
                continue
            if j >= 0 and text[j] == ".":
                i = j - 1
                continue
            return group_start
        if is_ident_char(ch):
            while i >= 0 and is_ident_char(text[i]):
                i -= 1
            # `bucket->item`, `holder.item`: the object in front belongs to the expression
            j = i
            while j >= 0 and text[j] in " \t":
                j -= 1
            if j >= 1 and text[j] == ">" and text[j - 1] == "-":
                i = j - 2
                continue
            if j >= 0 and text[j] == ".":
                i = j - 1
                continue
            return i + 1
        return None
    return None


def rewrite_b1(text):
    """Returns (new_text, count). The argument list is kept as it is, so nesting does not matter."""
    spans = comment_spans(text)
    out = []
    pos = 0
    count = 0
    pattern = re.compile("|".join(re.escape(p) for p in B1_PATTERNS))
    for match in pattern.finditer(text):
        if in_span(spans, match.start()):
            continue
        out.append(text[pos:match.start()])
        out.append(B1_CALL + "(")
        pos = match.end()
        count += 1
    out.append(text[pos:])
    return "".join(out), count


def rewrite_b2(text):
    """Returns (new_text, count)."""
    spans = comment_spans(text)
    out = []
    pos = 0
    count = 0
    for match in re.finditer(r"->IsMoving\(\)", text):
        arrow = match.start()
        if in_span(spans, arrow):
            continue
        start = receiver_start(text, arrow)
        if start is None:
            continue
        receiver = text[start:arrow].rstrip()
        out.append(text[pos:start])
        out.append("%s(%s)" % (B2_CALL, receiver))
        pos = match.end()
        count += 1
    out.append(text[pos:])
    return "".join(out), count


def sources(paths):
    for path in paths:
        full = os.path.join(ROOT, path)
        if os.path.isfile(full):
            yield full
            continue
        for directory, _, files in os.walk(full):
            for name in sorted(files):
                if name.endswith((".cpp", ".h")):
                    yield os.path.join(directory, name)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv[1:]
    do_b1 = "--b2" not in sys.argv[1:]
    do_b2 = "--b1" not in sys.argv[1:]
    paths = args or DEFAULT_PATHS
    totals = [0, 0]
    for path in sources(paths):
        rel = os.path.relpath(path, ROOT)
        if rel in SKIP_FILES:
            continue
        with open(path, encoding="utf-8", errors="surrogateescape") as handle:
            text = handle.read()
        new_text = text
        counts = [0, 0]
        if do_b1 and "GetItemPrototype(" in text:
            new_text, counts[0] = rewrite_b1(new_text)
        if do_b2 and "->IsMoving()" in text:
            new_text, counts[1] = rewrite_b2(new_text)
        if counts[0] or counts[1]:
            print("%-72s %3d %3d" % (rel, counts[0], counts[1]))
            totals[0] += counts[0]
            totals[1] += counts[1]
            if not check:
                with open(path, "w", encoding="utf-8", errors="surrogateescape") as handle:
                    handle.write(new_text)
    print("item-by-id sites rewritten: %d, IsMoving sites rewritten: %d" % (totals[0], totals[1]))


if __name__ == "__main__":
    main()
