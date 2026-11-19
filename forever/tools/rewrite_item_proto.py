#!/usr/bin/env python3
"""Rewrite `X->GetProto()` on a core Item to `GetItemProtoCompat(X)`, in the shared bot code.

cmangos gave Item, Creature, GameObject and Pet a `GetProto()`; ForeverCore names each getter after its template
(`Item::GetTemplate()`), and the bot brain wants its ItemPrototype mirror back, so an item's template is reached through
one helper that exists on both cores (ai/cmangos-compat-shim.h and forever/src/compat/forever-compat-shim.h).

Only core Items are rewritten. `GetProto()` on the bot's own classes (ItemQualifier, the loot/spell qualifiers) stays:
those are looked up by the receiver name below and reported instead, so the decision is visible.

    python3 forever/tools/rewrite_item_proto.py [--check] [paths...]

Default paths: ai/ runtime/ commands/. --check prints what would change and writes nothing.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_PATHS = ["ai", "runtime", "commands"]
SKIP_RECEIVERS = {"itemQualifier", "qualifier", "itemQualifier2"}   # the bot's own GetProto()
# The shims define GetItemProtoCompat, so the Turtle half must keep its `item->GetProto()` body.
SKIP_FILES = {"ai/cmangos-compat-shim.h"}
CALL = "GetItemProtoCompat"


def comment_spans(text):
    """Character ranges that are comments, so a rewrite never lands inside one."""
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
    """Start of the expression whose member is fetched, given the position of the `->` of `->GetProto()`."""
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


def rewrite(text):
    """Returns (new_text, rewritten_count, skipped_receivers)."""
    spans = comment_spans(text)
    out = []
    pos = 0
    count = 0
    skipped = []
    for match in re.finditer(r"->GetProto\(\)", text):
        arrow = match.start()
        if in_span(spans, arrow):
            continue
        start = receiver_start(text, arrow)
        if start is None:
            continue
        receiver = text[start:arrow].rstrip()
        tail = re.search(r"([A-Za-z_]\w*)$", receiver)
        if tail and tail.group(1) in SKIP_RECEIVERS:
            skipped.append(receiver)
            continue
        out.append(text[pos:start])
        out.append("%s(%s)" % (CALL, receiver))
        pos = match.end()
        count += 1
    out.append(text[pos:])
    return "".join(out), count, skipped


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
    paths = args or DEFAULT_PATHS
    total = 0
    skipped_total = 0
    for path in sources(paths):
        with open(path, encoding="utf-8", errors="surrogateescape") as handle:
            text = handle.read()
        if "GetProto()" not in text or os.path.relpath(path, ROOT) in SKIP_FILES:
            continue
        new_text, count, skipped = rewrite(text)
        skipped_total += len(skipped)
        if not count:
            continue
        total += count
        rel = os.path.relpath(path, ROOT)
        print("%-72s %3d" % (rel, count))
        if not check:
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as handle:
                handle.write(new_text)
    print("item GetProto sites rewritten: %d, bot-class GetProto kept: %d" % (total, skipped_total))


if __name__ == "__main__":
    main()
