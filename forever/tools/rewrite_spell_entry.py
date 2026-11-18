#!/usr/bin/env python3
"""Rewrite the spell FETCH sites of shim round 3 in the shared bot code (SHIM-R3-NOTES.md, "Call sites").

The bot brain reads spell data as the old fields (`spellInfo->Effect[i]`, `pSpellInfo->Reagent[k]`). On ForeverCore that is our
SpellEntry mirror of the core's SpellInfo (forever/src/compat/TbSpellEntry.h), so every place that FETCHES a spell has to hand
back the mirror. The helpers exist on both cores (ai/cmangos-compat-shim.h for the Turtle core, where each is an identity or
the same call), so the rewritten line reads the same on both:

  A  sSpellMgr.GetSpellEntry(id)                    ->  GetSpellEntryCompat(id)
  B  sSpellTemplate.LookupEntry<SpellEntry>(id)     ->  GetSpellEntryCompat(id)
  C  <receiver>->m_spellInfo                        ->  GetSpellEntryCompat(<receiver>->m_spellInfo)
  D  <receiver>->GetSpellProto()                    ->  GetSpellEntryCompat(<receiver>->GetSpellProto())

C and D are NOT applied where the value goes back into a core member or a core call that wants the core's SpellInfo; those
sites are listed in SKIP_LINES with the reason, so the split is visible instead of accidental.

    python3 forever/tools/rewrite_spell_entry.py [--check] [--a|--b|--c|--d] [paths...]

Default paths: ai/ runtime/ commands/. --check prints what would change and writes nothing. The shims define the helpers, so
they are skipped.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_PATHS = ["ai", "runtime", "commands"]
SKIP_FILES = {
    "ai/cmangos-compat-shim.h",
    "forever/src/compat/forever-compat-shim.h",
}
# The SpellRange row's two numbers: the old struct spells them minRange/maxRange, ForeverCore RangeMin[i]/RangeMax[i], so the
# read goes through a helper that exists on both cores (the lookup itself compiles on both).
E_SITES = [
    ("ai/playerbot/PlayerbotAISpells.cpp", "spellRangeEntry->minRange", "SpellRangeMin(spellRangeEntry)"),
    ("ai/playerbot/PlayerbotAISpells.cpp", "spellRangeEntry->maxRange", "SpellRangeMax(spellRangeEntry)"),
    ("ai/playerbot/strategy/actions/EnemyAreaSpell.h", "rangeEntry->maxRange", "SpellRangeMax(rangeEntry)"),
    ("ai/playerbot/strategy/triggers/GenericTriggers.cpp", "rangeEntry->maxRange", "SpellRangeMax(rangeEntry)"),
]
# Site -> why the core's SpellInfo has to stay there (a core member or a core call, not a field read).
SKIP_SITES = {
    ("ai/playerbot/strategy/actions/UseItemAction.cpp", "m_triggeredByAuraSpell = triggeredByAura->GetSpellProto()"),
}
A_PATTERNS = [
    ("sSpellMgr.GetSpellEntry(", "GetSpellEntryCompat("),
    ("sSpellTemplate.LookupEntry<SpellEntry>(", "GetSpellEntryCompat("),
]
C_TARGET = "->m_spellInfo"
D_TARGET = "->GetSpellProto()"


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


def match_back(text, close, open_, start):
    depth = 0
    k = start
    while k >= 0:
        if text[k] == close:
            depth += 1
        elif text[k] == open_:
            depth -= 1
            if depth == 0:
                return k
        k -= 1
    return -1


def _is_word(c):
    return c.isalnum() or c == "_"


def postfix_start(text, end):
    """Start of the postfix expression that ends just before `end` (a call `f(x)`, an index `a[i]`, a group `(x)`, a name)."""
    while True:
        j = end
        while j > 0 and text[j - 1] in " \t":
            j -= 1
        if j == 0:
            return None
        c = text[j - 1]
        if c in ")]":
            close, open_ = (")", "(") if c == ")" else ("]", "[")
            k = match_back(text, close, open_, j - 1)
            if k < 0:
                return None
            # `f(x)` / `a[i]`: the name (or call) before the bracket is part of the same expression, so keep going from there.
            m = k
            while m > 0 and text[m - 1] in " \t":
                m -= 1
            if m > 0 and (_is_word(text[m - 1]) or text[m - 1] in ")]"):
                end = k
                continue
            return k
        if _is_word(c):
            k = j - 1
            while k > 0 and _is_word(text[k - 1]):
                k -= 1
            return k
        return None


def receiver_start(text, arrow):
    """Index where the postfix expression whose member access starts at `arrow` begins, or None."""
    i = postfix_start(text, arrow)
    while i is not None:
        m = i
        while m > 0 and text[m - 1] in " \t":
            m -= 1
        if m >= 2 and text[m - 2:m] in ("->", "::"):
            nxt = postfix_start(text, m - 2)
            if nxt is None:
                return i
            i = nxt
            continue
        if m >= 1 and text[m - 1] == ".":
            nxt = postfix_start(text, m - 1)
            if nxt is None:
                return i
            i = nxt
            continue
        return i
    return None


def wrap(text, target, spans, wrapped):
    """Wrap every <receiver><target> in the file. `wrapped` collects (line, text) for the report.

    `pos` is the end of the text copied into the output so far; a hit that is skipped (a comment, a receiver the parser
    cannot follow, or something already wrapped) only moves the scan position, never the copy position, so no text is lost."""
    out = []
    pos = 0
    scan = 0
    changed = 0
    while True:
        hit = text.find(target, scan)
        if hit < 0:
            break
        end = hit + len(target)
        if in_spans(hit, spans):
            scan = end
            continue
        start = receiver_start(text, hit)
        if start is None or start < pos:
            scan = end
            continue
        before = text[:start].rstrip()
        if before.endswith("GetSpellEntryCompat(") or before.endswith("SpellRangeMin(") or before.endswith("SpellRangeMax("):
            scan = end
            continue
        out.append(text[pos:start])
        out.append("GetSpellEntryCompat(" + text[start:end] + ")")
        line = text.count("\n", 0, start) + 1
        wrapped.append((line, text[start:end]))
        changed += 1
        pos = end
        scan = end
    out.append(text[pos:])
    return "".join(out), changed


def main():
    args = [a for a in sys.argv[1:]]
    check = "--check" in args
    only = {a[2:] for a in args if re.fullmatch(r"--[abcd]", a)}
    paths = [a for a in args if not a.startswith("--")] or DEFAULT_PATHS

    files = []
    for base in paths:
        full = os.path.join(ROOT, base)
        if os.path.isfile(full):
            files.append(base)
            continue
        for root, _, names in os.walk(full):
            for n in names:
                if n.endswith((".cpp", ".h", ".hpp")):
                    files.append(os.path.relpath(os.path.join(root, n), ROOT))

    total = {}
    for rel in sorted(files):
        if rel in SKIP_FILES:
            continue
        path = os.path.join(ROOT, rel)
        text = open(path, encoding="utf-8").read()
        spans = comment_spans(text)
        changed = {}

        if not only or "a" in only:
            for old, new in A_PATTERNS:
                n = text.count(old)
                if n:
                    text = text.replace(old, new)
                    changed[old] = n

        if not only or "b" in only:
            old = "sSpellTemplate.LookupEntry<SpellEntry>"
            if old in text and text.count(old + "(") != text.count(old):
                print(f"{rel}: {old} without ( - check by hand")
            n = text.count(old + "(")
            if n:
                text = text.replace(old + "(", "GetSpellEntryCompat(")
                changed[old] = n

        for key, target in (("c", C_TARGET), ("d", D_TARGET)):
            if only and key not in only:
                continue
            if target not in text:
                continue
            lines = text.split("\n")
            skipped = [l for l in lines if any(s in l and rel == f for (f, s) in SKIP_SITES)]
            if skipped:
                # park the skipped lines as strings-free sentinels so wrap() never sees them
                for idx, l in enumerate(lines):
                    if any(s in l and rel == f for (f, s) in SKIP_SITES):
                        lines[idx] = l.replace(target, target.replace("->", "\x01"))
                text = "\n".join(lines)
            wrapped = []
            text, n = wrap(text, target, comment_spans(text), wrapped)
            if n:
                changed[target] = n
                for line, frag in wrapped:
                    print(f"  {rel}:{line}: {frag}")
            if skipped:
                text = text.replace(target.replace("->", "\x01"), target)

        for f, old, new in E_SITES:
            if f == rel and old in text:
                text = text.replace(old, new)
                changed[old] = text.count(new)

        if changed:
            total[rel] = changed
            if not check:
                open(path, "w", encoding="utf-8").write(text)

    print()
    grand = sum(sum(c.values()) for c in total.values())
    for rel, changed in sorted(total.items()):
        print(f"{rel}: " + ", ".join(f"{k} x{v}" for k, v in changed.items()))
    print(f"{'would change' if check else 'changed'} {grand} site(s) in {len(total)} file(s)")


if __name__ == "__main__":
    main()
