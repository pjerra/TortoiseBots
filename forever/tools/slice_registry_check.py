#!/usr/bin/env python3
"""Slice registry check (FOREVER-SLICE1-SPEC section 4, package R2; slice 2: the list is forever/SLICE2.txt and the priest's context counts as registered, R3).

    python3 forever/tools/slice_registry_check.py [--list] [--quiet]

Parses forever/src/slice/SliceAiObjectContext.cpp (what the slice registers) and the full build's registries
(ai/playerbot/strategy/StrategyContext.h, actions/{Action,ChatAction,WorldPacketAction}Context.h,
triggers/{Trigger,ChatTrigger,WorldPacketTrigger}Context.h, values/ValueContext.h, and the warrior context the slice
compiles as it is), then reports:

  (a) slice keys that are not keys of the full build, or whose creator text differs from the full build's.
      Must be empty: the script exits 1 otherwise.
  (b) keys that the slice's own source files (forever/SLICE2.txt, each .cpp and the .h of the same name) reference by
      string and the slice does not register. This is the to-do list for later packages, not a failure.
      Values come with the C++ types they are read as (the missing-value stand-in in SliceMissingKeyGuard.h
      covers the types listed there).

Exit 0 when (a) is empty and everything parsed, 1 on (a), 2 on a parse error. No build, no network.
"""
import os, re, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
STRAT = os.path.join(ROOT, "ai", "playerbot", "strategy")
SLICE_CTX = os.path.join(ROOT, "forever", "src", "slice", "SliceAiObjectContext.cpp")
SLICE_LIST = os.path.join(ROOT, "forever", "SLICE2.txt")
SLICE_GUARD = os.path.join(ROOT, "forever", "src", "slice", "SliceMissingKeyGuard.h")

# Full-build registries, in the order AiObjectContext::AiObjectContext adds them (first context wins a lookup).
FULL = {
    "strategy": ["StrategyContext.h"],
    "action": ["actions/ActionContext.h", "actions/ChatActionContext.h", "actions/WorldPacketActionContext.h"],
    "trigger": ["triggers/TriggerContext.h", "triggers/ChatTriggerContext.h", "triggers/WorldPacketTriggerContext.h"],
    "value": ["values/ValueContext.h"],
}
# The class context the slice compiles unchanged: it registers its own keys on top of the slice's generic ones.
WARRIOR = os.path.join(STRAT, "warrior", "WarriorAiObjectContext.cpp")
PRIEST = os.path.join(STRAT, "priest", "PriestAiObjectContext.cpp")   # slice 2 (R3): the second class context, compiled as it is
CLASS_CONTEXTS = (WARRIOR, PRIEST)
KINDS = ("strategy", "action", "trigger", "value")
CTX_TYPE = {"Strategy": "strategy", "Action": "action", "Trigger": "trigger", "UntypedValue": "value"}


class ParseError(Exception):
    pass


def strip_comments(text):
    """Blank out // and /* */ comments, keeping every offset and line break (so line numbers stay right)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


KEY = re.compile(r'creators\s*\[\s*"((?:[^"\\]|\\.)*)"\s*\]\s*=')
CLASS = re.compile(r'\bclass\s+(\w+)\s*(?:final\s*)?:\s*public\s+(?:ai::)?NamedObjectContext<\s*(?:ai::)?(\w+)\s*>')
CTORFLAGS = re.compile(r'NamedObjectContext<\s*(?:ai::)?\w+\s*>\s*\(\s*(true|false)\s*,\s*(true|false)\s*\)')


def parse_creators(path):
    """Every `creators["key"] = <expr>;` in a file: dicts with key, expr (whitespace-normalised), cls, kind, flags, line."""
    raw = read(path)
    text = strip_comments(raw)
    classes = [(m.start(), m.group(1), m.group(2)) for m in CLASS.finditer(text)]
    found = []
    for m in KEY.finditer(text):
        # the expression ends at the first ';' outside parentheses, braces and string literals
        i, depth, n = m.end(), 0, len(text)
        while i < n:
            c = text[i]
            if c == '"' or c == "'":
                j = i + 1
                while j < n and text[j] != c:
                    j += 2 if text[j] == "\\" else 1
                i = j
            elif c in "({[":
                depth += 1
            elif c in ")}]":
                depth -= 1
            elif c == ";" and depth == 0:
                break
            i += 1
        else:
            raise ParseError("%s: unterminated creators[] entry at line %d" % (path, text.count("\n", 0, m.start()) + 1))
        owner = None
        for pos, name, tname in classes:
            if pos < m.start():
                owner = (name, tname)
        if owner is None:
            raise ParseError("%s: creators[] entry outside a NamedObjectContext class at line %d"
                             % (path, text.count("\n", 0, m.start()) + 1))
        # The constructor flags (shared, supportsSiblings) sit in the constructor initialiser list of the class.
        seg_end = m.start()
        seg_start = [p for p, _, _ in classes if p < m.start()][-1]
        fm = CTORFLAGS.search(text, seg_start, seg_end)
        flags = (fm.group(1), fm.group(2)) if fm else ("false", "false")
        kind = CTX_TYPE.get(owner[1])
        if kind is None:
            raise ParseError("%s: unknown context value type %s" % (path, owner[1]))
        found.append({"key": m.group(1), "expr": " ".join(text[m.end():i].split()), "cls": owner[0], "kind": kind,
                      "flags": flags, "line": text.count("\n", 0, m.start()) + 1, "file": path})
    return found


def full_registry():
    """kind -> {key: entry}. The first context in registration order wins; inside a class the last assignment wins."""
    reg = {k: {} for k in KINDS}
    for kind, files in FULL.items():
        for rel in files:
            path = os.path.join(STRAT, rel)
            entries = parse_creators(path)
            per_class = collections.OrderedDict()
            for e in entries:
                per_class.setdefault(e["cls"], {})[e["key"]] = e
            for cls, d in per_class.items():
                for key, e in d.items():
                    reg[kind].setdefault(key, e)
    return reg


def warrior_keys():
    keys = {k: set() for k in KINDS}
    for ctx in CLASS_CONTEXTS:
        for e in parse_creators(ctx):
            keys[e["kind"]].add(e["key"])
    return keys


def slice_registry():
    reg = {k: {} for k in KINDS}
    for e in parse_creators(SLICE_CTX):
        reg[e["kind"]][e["key"]] = e
    return reg


# ---- (b): what the slice sources reference by string -------------------------------------------------------------

VALUE_MACROS = ("AI_VALUE", "AI_VALUE2", "AI_VALUE_SAFE", "AI_VALUE2_SAFE", "AI_VALUE_LAZY", "AI_VALUE2_LAZY",
                "SET_AI_VALUE", "SET_AI_VALUE2", "RESET_AI_VALUE", "RESET_AI_VALUE2", "AI_VALUE_EXISTS",
                "AI_VALUE2_EXISTS", "PAI_VALUE", "PAI_VALUE2", "SET_PAI_VALUE", "SET_PAI_VALUE2", "MAI_VALUE",
                "MAI_VALUE2", "MEM_AI_VALUE", "LOG_AI_VALUE")
HAS_MACROS = ("HAS_AI_VALUE", "HAS_AI_VALUE2", "PHAS_AI_VALUE", "PHAS_AI_VALUE2")
STR = r'"((?:[^"\\]|\\.)*)"'
RE_MACRO = re.compile(r'\b(%s)\s*\(' % "|".join(VALUE_MACROS))
RE_HAS = re.compile(r'\b(?:%s)\s*\(\s*%s' % ("|".join(HAS_MACROS), STR))
RE_GETVALUE = re.compile(r'\bGetValue\s*<')
RE_UNTYPED = re.compile(r'\b(?:GetUntypedValue|HasValue)\s*\(\s*' + STR)
RE_TRIGGERNODE = re.compile(r'\bTriggerNode\s*\(\s*' + STR)
RE_NEXTACTION = re.compile(r'\b(?:NextAction|ActionNode)\s*\(\s*' + STR)
RE_TWOTRIG = re.compile(r'\bTwoTriggers\s*\(\s*\w+\s*,\s*' + STR + r'\s*,\s*' + STR)
RE_ACTION_CALL = re.compile(r'\b(?:DoSpecificAction|CanDoSpecificAction|GetAction)\s*\(\s*' + STR)
RE_STRAT_CALL = re.compile(r'\b(?:HasStrategy|addStrategy|removeStrategy)\s*\(\s*' + STR)
RE_TRIG_CALL = re.compile(r'\bGetTrigger\s*\(\s*' + STR)


def scan_type(text, i):
    """text[i] is just after '(' or '<': return (type text, index after the first top-level ',' or closing '>')."""
    depth, j, n = 0, i, len(text)
    while j < n:
        c = text[j]
        if c in "<({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
            if depth < 0:
                return None, j
        elif c == ">":
            if depth == 0:
                return text[i:j].strip(), j + 1
            depth -= 1
        elif c == "," and depth == 0:
            return text[i:j].strip(), j + 1
        j += 1
    return None, n


def base(name):
    return name.split("::", 1)[0]


def refs_in(path, text):
    """Yield (kind, name, type or None, has_only) for every string-literal reference in a source text."""
    for m in RE_MACRO.finditer(text):
        typ, j = scan_type(text, m.end())
        if typ is None:
            continue
        s = re.match(r'\s*' + STR, text[j:])
        if s:
            yield "value", s.group(1), " ".join(typ.split()), False
    for m in RE_HAS.finditer(text):
        yield "value", m.group(1), None, True
    for m in RE_GETVALUE.finditer(text):
        typ, j = scan_type(text, m.end())
        if typ is None:
            continue
        s = re.match(r'\s*\(\s*' + STR, text[j:])
        if s:
            yield "value", s.group(1), " ".join(typ.split()), False
    for m in RE_UNTYPED.finditer(text):
        yield "value", m.group(1), None, False
    for m in RE_TRIGGERNODE.finditer(text):
        yield "trigger", m.group(1), None, False
    for m in RE_TWOTRIG.finditer(text):
        yield "trigger", m.group(1), None, False
        yield "trigger", m.group(2), None, False
    for m in RE_TRIG_CALL.finditer(text):
        yield "trigger", m.group(1), None, False
    for m in RE_NEXTACTION.finditer(text):
        yield "action", m.group(1), None, False
    for m in RE_ACTION_CALL.finditer(text):
        yield "action", m.group(1), None, False
    for m in RE_STRAT_CALL.finditer(text):
        yield "strategy", m.group(1), None, False


def slice_sources():
    paths = []
    for line in read(SLICE_LIST).splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            paths.append(line)
    return paths


def referenced():
    """(kind, base name) -> {'files': set, 'types': set, 'has_only': bool}"""
    out = {}
    for rel in slice_sources():
        base_path = os.path.join(ROOT, rel)
        for p in (base_path, os.path.splitext(base_path)[0] + ".h"):
            if not os.path.isfile(p) or os.path.abspath(p) in [os.path.abspath(c) for c in CLASS_CONTEXTS]:
                continue
            text = strip_comments(read(p))
            for kind, name, typ, has_only in refs_in(p, text):
                if "::" in name and name.startswith("::"):
                    continue
                e = out.setdefault((kind, base(name)), {"files": set(), "types": set(), "has_only": True})
                e["files"].add(os.path.relpath(p, ROOT))
                if typ:
                    e["types"].add(typ)
                if not has_only:
                    e["has_only"] = False
    return out


def norm_type(t):
    return re.sub(r"\s+", "", t).replace("ai::", "")


def guard_types():
    """The value types the missing-value stand-in covers: the template arguments of `using SliceInertValue =
    InertValues<...>;` in forever/src/slice/SliceMissingKeyGuard.h, whitespace and `ai::` removed."""
    if not os.path.isfile(SLICE_GUARD):
        return set()
    text = strip_comments(read(SLICE_GUARD))
    m = re.search(r'using\s+SliceInertValue\s*=\s*InertValues\s*<', text)
    if not m:
        raise ParseError("%s: no `using SliceInertValue = InertValues<...>` alias" % SLICE_GUARD)
    types, i = [], m.end()
    while True:
        t, i = scan_type(text, i)
        if t is None:
            raise ParseError("%s: cannot read the InertValues type list" % SLICE_GUARD)
        types.append(norm_type(t))
        # scan_type stopped after a ',' or after the closing '>'
        if text[i - 1] == ">":
            break
    return set(types)


def main(argv):
    quiet = "--quiet" in argv
    try:
        full = full_registry()
        warrior = warrior_keys()
        sl = slice_registry()
        refs = referenced()
        covered = guard_types()
    except (ParseError, OSError) as e:
        print("PARSE ERROR: %s" % e)
        return 2

    counts = {k: len(sl[k]) for k in KINDS}
    PLURAL = {"strategy": "strategies", "action": "actions", "trigger": "triggers", "value": "values"}
    print("slice registers: %d strategies, %d actions, %d triggers, %d values"
          % tuple(counts[k] for k in KINDS))
    print("full build keys: %s" % ", ".join("%d %s" % (len(full[k]), PLURAL[k]) for k in KINDS))

    # (a)
    bad = []
    for kind in KINDS:
        for key, e in sorted(sl[kind].items()):
            f = full[kind].get(key)
            if f is None:
                bad.append("%s '%s' is not a key of the full build (%s:%d)"
                           % (kind, key, os.path.relpath(e["file"], ROOT), e["line"]))
            elif f["expr"] != e["expr"]:
                bad.append("%s '%s' maps to another creator than the full build:\n      slice: %s\n      full:  %s"
                           % (kind, key, e["expr"], f["expr"]))
            elif f["flags"] != e["flags"]:
                bad.append("%s '%s' sits in a context with other (shared, siblings) flags: slice %s, full %s"
                           % (kind, key, e["flags"], f["flags"]))
    print("missing-value stand-in covers %d value types (SliceMissingKeyGuard.h)" % len(covered))
    print("\n(a) slice keys that are not in the full build, or map elsewhere: %d" % len(bad))
    for b in bad:
        print("    " + b)

    # (b)
    todo = {k: [] for k in KINDS}
    for (kind, name), info in refs.items():
        if name in sl[kind] or name in warrior[kind]:
            continue
        todo[kind].append((name, info))
    print("\n(b) keys the slice's sources reference by string that the slice does not register")
    total = 0
    for kind in KINDS:
        items = sorted(todo[kind], key=lambda x: x[0])
        total += len(items)
        in_full = sum(1 for n, _ in items if n in full[kind])
        print("  %s: %d (%d exist in the full build, %d exist nowhere)" % (PLURAL[kind], len(items), in_full, len(items) - in_full))
        if quiet:
            continue
        for name, info in items:
            tag = "" if name in full[kind] else "  [NOT IN FULL BUILD]"
            extra = ""
            if kind == "value":
                types = sorted(info["types"])
                extra = "  types: " + (", ".join(types) if types else "(none: has/untyped only)")
                if info["has_only"]:
                    extra += "  [has-only]"
                unc = [t for t in types if norm_type(t) not in covered]
                if unc:
                    extra += "  [stand-in lacks: %s]" % ", ".join(unc)
            print("    %s%s%s  <- %s" % (name, tag, extra, ", ".join(sorted(info["files"]))[:120]))
    print("\nsummary: (a)=%d  (b)=%d keys to register or stub later" % (len(bad), total))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
