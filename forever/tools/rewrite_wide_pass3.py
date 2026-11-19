#!/usr/bin/env python3
"""Wide pass 3 (branch port/wide-pass-3): the call-site rewrites of five families, with the helpers of forever/src/compat/TbCompat_W3.h
(ForeverCore) and ai/cmangos-compat-w3.h (Turtle), found through ai/playerbot/TbCompatW3.h. Files in forever/tools/wide3-exclude.txt
(every slice list of the four branches, their .h/.cpp pairs and everything named *Commander*), the compat headers and everything under
forever/ are never touched. Every rewrite keeps one call, the same arguments, evaluated once, and never lands inside a comment or a string.

    python3 forever/tools/rewrite_wide_pass3.py --family spelling|guidstring|db|manager|quest [--check]
"""
import os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
INCLUDE = '#include "playerbot/TbCompatW3.h"'
INCLUDE_UP = '#include "../ai/playerbot/TbCompatW3.h"'   # runtime/, host/, commands/, behavior/ reach ai/ the way their neighbours do


def excluded():
    out = set()
    for l in open(os.path.join(ROOT, "forever/tools/wide3-exclude.txt")):
        l = l.strip()
        if l and not l.startswith("#"):
            out.add(os.path.normpath(l))
    return out


def files():
    ex = excluded()
    for top in ("ai", "commands", "runtime", "host", "behavior"):
        for dp, _, fn in os.walk(os.path.join(ROOT, top)):
            for f in sorted(fn):
                if f.endswith((".cpp", ".h", ".hpp")):
                    rel = os.path.relpath(os.path.join(dp, f), ROOT)
                    if rel not in ex and not rel.startswith("ai/cmangos-compat") and rel != "ai/botpch.h" and not rel.startswith("ai/playerbot/TbCompat"):
                        yield rel


def mask(text):
    """1 for every character inside a comment, a string literal or a character literal."""
    m = bytearray(len(text))
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            m[i:j] = b"\x01" * (j - i)
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            m[i:j] = b"\x01" * (j - i)
            i = j
        elif text[i] in "\"'":
            q = text[i]
            j = i + 1
            while j < n and text[j] != "\n":
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == q:
                    j += 1
                    break
                j += 1
            m[i:j] = b"\x01" * (j - i)
            i = j
        else:
            i += 1
    return m


def balanced_close(text, open_idx):
    """Index of the ')' that closes the '(' at open_idx (strings and comments skipped), or -1."""
    depth, i, n = 0, open_idx, len(text)
    msk = mask(text[open_idx:])
    while i < n:
        if not msk[i - open_idx]:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    return -1


def subst(text, pattern, repl):
    """Apply re.sub-like replacement for matches whose first character is code (not a comment or string). Returns (text, count)."""
    msk = mask(text)
    out, pos, n = [], 0, 0
    for m in pattern.finditer(text):
        if msk[m.start()]:
            continue
        new = repl(m)
        if new is None:
            continue
        out.append(text[pos:m.start()])
        out.append(new)
        pos = m.end()
        n += 1
    out.append(text[pos:])
    return "".join(out), n


# ---- family 1: spellings -------------------------------------------------------------------------------------------------
EVENT_SOURCE = re.compile(r"\b(event|moveEvent|GetLastEvent\(\))\.GetSource\(\)")
ZONE_ID = re.compile(r"->getZoneId\(\)")


def fam_spelling(text):
    text, a = subst(text, EVENT_SOURCE, lambda m: m.group(1) + ".getSource()")
    text, b = subst(text, ZONE_ID, lambda m: "->GetZoneId()")
    return text, a + b


# ---- family 2: a guid as text --------------------------------------------------------------------------------------------
RECV = r"[A-Za-z_]\w*(?:\(\))?(?:(?:\.|->)[A-Za-z_]\w*(?:\(\))?)*"
GUID_STRING = re.compile(r"(?<![\w.>])(" + RECV + r")\.GetString\(\)")
# the lines the census names for "no member named 'GetString' in 'ObjectGuid'" are the only ones rewritten (Field::GetString exists too)
GUID_LINES = {}


def load_lines(path, needle):
    out = {}
    for l in open(path):
        where, msg = l.rstrip("\n").split("\t", 1)
        if needle in msg:
            f, ln = where.rsplit(":", 1)
            out.setdefault(os.path.normpath(f), set()).add(int(ln))
    return out


def fam_guidstring(text, rel=None, lines=None):
    keep = lines.get(rel, set()) if lines is not None else None
    msk = mask(text)
    starts = [0]
    for i, ch in enumerate(text):
        if ch == "\n":
            starts.append(i + 1)
    import bisect
    def line_of(pos):
        return bisect.bisect_right(starts, pos)
    def repl(m):
        if keep is not None and line_of(m.start()) not in keep and line_of(m.end()) not in keep:
            return None
        return "GuidStringW3(" + m.group(1) + ")"
    return subst(text, GUID_STRING, repl)


# ---- family 3: database reads --------------------------------------------------------------------------------------------
DECL = re.compile(r"std::unique_ptr<QueryResult>\s+(\w+)\s*\(\s*(\w+Database)\s*\.\s*(Query|PQuery)\s*\(")
FIELD_TEXT = re.compile(r"(?<![\w.>])([A-Za-z_]\w*(?:\[[^\]\n]*\])?)\.GetCppString\(\)")
FIELD_NULL = re.compile(r"(?<![\w.>])([A-Za-z_]\w*(?:\[[^\]\n]*\])?)\.IsNULL\(\)")
ESCAPE = re.compile(r"\b(\w+Database)\s*\.\s*escape_string\(\s*([A-Za-z_][\w.]*(?:\[[^\]\n]*\])?)\s*\)")


def fam_db(text):
    total = 0
    pos = 0
    while True:
        msk = mask(text)
        m = None
        for cand in DECL.finditer(text, pos):
            if not msk[cand.start()]:
                m = cand
                break
        if not m:
            break
        name, db, call = m.group(1), m.group(2), m.group(3)
        outer_open = text.index("(", m.start() + len("std::unique_ptr<QueryResult>"))   # the '(' after the variable name
        inner_open = m.end() - 1                                                        # the '(' of the Query/PQuery call
        close_inner = balanced_close(text, inner_open)
        close_outer = balanced_close(text, outer_open)
        if close_inner < 0 or close_outer < 0 or text[close_inner + 1:close_outer].strip() != "":
            pos = m.end()   # not the plain `unique_ptr<QueryResult> x(DB.Query(...))` shape: left
            continue
        args = text[inner_open + 1:close_inner]
        if call == "Query":
            inner = db + ".Query(" + args + ")"
        elif db == "CharacterDatabase":
            inner = "CharacterPQueryCompat(" + args + ")"
        else:
            inner = "DbPQueryW3(" + db + ", " + args.lstrip() + ")"
        new = "auto " + name + " = QueryGuardCompat(" + inner + ")"
        text = text[:m.start()] + new + text[close_outer + 1:]
        pos = m.start() + len(new)
        total += 1
    text, a = subst(text, FIELD_TEXT, lambda m: "FieldTextW3(" + m.group(1) + ")")
    text, b = subst(text, FIELD_NULL, lambda m: "FieldIsNullW3(" + m.group(1) + ")")
    text, c = subst(text, ESCAPE, lambda m: "DbEscapeW3(" + m.group(1) + ", " + m.group(2) + ")")
    return text, total + a + b + c


# ---- family 4: the manager singletons ------------------------------------------------------------------------------------
MANAGER = re.compile(r"\b(sAuctionMgr|sAccountMgr|sScriptMgr)\s*\.")
MANAGER_HELPER = {"sAuctionMgr": "AuctionMgrW3", "sAccountMgr": "AccountMgrW3", "sScriptMgr": "ScriptMgrW3"}


def fam_manager(text):
    return subst(text, MANAGER, lambda m: MANAGER_HELPER[m.group(1)] + "().")


# ---- family 5: quest template members ------------------------------------------------------------------------------------
QUEST_RECV = r"([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)"
QUEST_TITLE = re.compile(r"(?<![\w.>])" + QUEST_RECV + r"(\.|->)GetTitle\(\)")
QUEST_ARRAY = re.compile(r"(?<![\w.>])" + QUEST_RECV + r"(\.|->)(RewItemId|RewItemCount|RewChoiceItemId)\b(?=\s*\[)")
QUEST_ARRAY_HELPER = {"RewItemId": "QuestRewItemIdsW3", "RewItemCount": "QuestRewItemCountsW3", "RewChoiceItemId": "QuestRewChoiceItemIdsW3"}
# receivers that are quests (the census names these members only for 'Quest'; other types with a GetTitle are not touched)
QUEST_RECV_NAMES = None


def quest_site_filter(lines, rel):
    return lines.get(rel, set())


def fam_quest(text, rel=None, lines=None):
    import bisect
    starts = [0]
    for i, ch in enumerate(text):
        if ch == "\n":
            starts.append(i + 1)
    keep = lines.get(rel, set()) if lines is not None else None
    def ok(m):
        return keep is None or bisect.bisect_right(starts, m.start()) in keep
    def recv(m):
        return m.group(1) if m.group(2) == "->" else "&" + m.group(1)
    text, a = subst(text, QUEST_TITLE, lambda m: ("QuestTitleW3(" + recv(m) + ")") if ok(m) else None)
    text, b = subst(text, QUEST_ARRAY, lambda m: (QUEST_ARRAY_HELPER[m.group(3)] + "(" + recv(m) + ")") if ok(m) else None)
    return text, a + b


NEEDLES = {"guidstring": "no member named 'GetString' in 'ObjectGuid'"}
QUEST_NEEDLE = re.compile(r"no member named '(GetTitle|RewItemId|RewItemCount|RewChoiceItemId)' in 'Quest'")


def census_lines(errors_tsv, pattern):
    out = {}
    for l in open(errors_tsv):
        where, msg = l.rstrip("\n").split("\t", 1)
        if pattern.search(msg):
            f, ln = where.rsplit(":", 1)
            out.setdefault(os.path.normpath(f), set()).add(int(ln))
    return out


def add_include(rel):
    path = os.path.join(ROOT, rel)
    text = open(path, encoding="utf-8", errors="surrogateescape").read()
    inc = INCLUDE if rel.startswith("ai/") else INCLUDE_UP
    if "TbCompatW3.h" in text:
        return False
    lines = text.split("\n")
    last = -1
    for i, l in enumerate(lines[:80]):
        if re.match(r'\s*#\s*include\b', l):
            last = i
    if last < 0:
        return False
    lines.insert(last + 1, inc)
    open(path, "w", encoding="utf-8", errors="surrogateescape").write("\n".join(lines))
    return True


def main():
    args = sys.argv[1:]
    check = "--check" in args
    fam = args[args.index("--family") + 1]
    errors = None
    if "--errors" in args:
        errors = args[args.index("--errors") + 1]
    lines = None
    if fam == "guidstring":
        lines = census_lines(errors, re.compile(re.escape(NEEDLES["guidstring"])))
    elif fam == "quest":
        lines = census_lines(errors, QUEST_NEEDLE)
    total = 0
    touched = []
    for rel in files():
        path = os.path.join(ROOT, rel)
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        if fam == "spelling":
            new, n = fam_spelling(text)
        elif fam == "guidstring":
            new, n = fam_guidstring(text, rel, lines)
        elif fam == "db":
            new, n = fam_db(text)
        elif fam == "manager":
            new, n = fam_manager(text)
        elif fam == "quest":
            new, n = fam_quest(text, rel, lines)
        else:
            sys.exit("unknown family")
        if not n:
            continue
        total += n
        touched.append(rel)
        if check:
            a, b = text.split("\n"), new.split("\n")
            import difflib
            for d in difflib.unified_diff(a, b, rel, rel, n=0, lineterm=""):
                if d.startswith(("+", "-")) and not d.startswith(("+++", "---")):
                    print(d[:200])
        else:
            open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
            if fam != "spelling":
                add_include(rel)
    print("family %s: %d rewrites in %d files%s" % (fam, total, len(touched), " (check only)" if check else ""))


if __name__ == "__main__":
    main()
