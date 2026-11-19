#!/usr/bin/env python3
"""Wide pass (WIDE-PASS-R1): put the existing both-core helpers on the same errors the slice already solved, in the files
outside the slices.

The places come from the whole-tree census (`forever/tools/vm-census.sh "" 10` writes errors.tsv: one `file:line<TAB>message`
row per error). A family takes the rows its predicate names and rewrites only those lines. Every helper used here has an
identity body on the Turtle core (ai/cmangos-compat-r3.h and the -r4*.h files are read for each family in the notes), so
the line makes the same call as before there; on ForeverCore it is the call that core needs. Nothing is edited in a file
named by forever/tools/wide-pass-exclude.txt (the three slice lists and their same-base headers) or in a compat header.

    python3 forever/tools/rewrite_wide_pass.py --errors <errors.tsv> [--check] [--family NAME ...]

Families: cstr player guidkey unitflags playerflags height angle los path headless transport zoneid bgmap summon
questlevel. Default: all of them. --check prints every line it would change and writes nothing.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXCLUDE_FILE = os.path.join(ROOT, "forever", "tools", "wide-pass-exclude.txt")
SKIP_PREFIXES = ("forever/", "tools/", ".git/")
SKIP_FILES = {"ai/cmangos-compat-shim.h", "ai/cmangos-compat-r3.h", "ai/cmangos-compat-r4a.h", "ai/cmangos-compat-r4c.h",
              "ai/cmangos-compat-r4p.h", "ai/cmangos-compat-r4v.h", "ai/cmangos-compat-unported.h"}

# A receiver: a chain of identifiers, zero-argument calls and subscripts, joined by -> or . (`bot`, `bot->GetMap()`,
# `creatures[mob]`, `found.kt`). Anchored so that the chain starts at a token, not in the middle of one.
RECV = r"(?<![A-Za-z0-9_.>])([A-Za-z_]\w*(?:\(\))?(?:\[[^\]]*\])?(?:(?:->|\.)[A-Za-z_]\w*(?:\(\))?(?:\[[^\]]*\])?)*)"


def comment_spans(line):
    """(start, end) of every comment and string literal on the line, so a rewrite never lands inside one."""
    spans = []
    i, n = 0, len(line)
    while i < n:
        two = line[i:i + 2]
        if two == "//":
            spans.append((i, n))
            break
        if two == "/*":
            end = line.find("*/", i + 2)
            end = n if end < 0 else end + 2
            spans.append((i, end))
            i = end
        elif line[i] in "\"'":
            quote = line[i]
            j = i + 1
            while j < n:
                if line[j] == "\\":
                    j += 2
                    continue
                if line[j] == quote:
                    j += 1
                    break
                j += 1
            spans.append((i, j))
            i = j
        else:
            i += 1
    return spans


def sub_all(pattern, repl, line):
    """re.subn over the code outside comments and string literals only."""
    spans = comment_spans(line)
    out, pos, n = [], 0, 0
    for m in re.finditer(pattern, line):
        if any(a <= m.start() < b for a, b in spans):
            continue
        out.append(line[pos:m.start()])
        out.append(repl(m) if callable(repl) else m.expand(repl))
        pos = m.end()
        n += 1
    out.append(line[pos:])
    return "".join(out), n


def wrap_call(pattern, repl, line, repetitions=1):
    total = 0
    for _ in range(repetitions):
        line, n = re.subn(pattern, repl, line)
        total += n
        if not n:
            break
    return line, total


def transform_cstr(line):
    """`sLog.outDetail("%s", bot->GetName())`: the Turtle core's GetName() is a char*, ForeverCore's a std::string."""
    total = 0
    # `GetName()` is a char* on Turtle, a std::string on ForeverCore: the helper covers it. `Name1` and `SpellName[0]`
    # are std::string on BOTH cores, and the Turtle-side helper has no std::string overload, so they are left.
    for pattern in (RECV + r"->GetName\(\)",):
        rx = re.compile(pattern)
        spans = comment_spans(line)
        for m in reversed(list(rx.finditer(line))):
            if line[max(0, m.start() - 11):m.start()] == "CStrCompat(":
                continue
            if any(a <= m.start() < b for a, b in spans):
                continue
            line = line[:m.start()] + "CStrCompat(" + m.group(0) + ")" + line[m.end():]
            total += 1
    return line, total


def transform_player(line):
    """`sObjectMgr.GetPlayer(guid)` -> `GetPlayerCompat(guid)`; a name lookup is left (it needs a name helper)."""
    total = 0
    start = 0
    spans = comment_spans(line)
    while True:
        pos = line.find("sObjectMgr.GetPlayer(", start)
        if pos < 0 or any(a <= pos < b for a, b in spans):
            break
        open_paren = pos + len("sObjectMgr.GetPlayer(")
        depth, i = 1, open_paren
        while i < len(line) and depth:
            if line[i] == "(":
                depth += 1
            elif line[i] == ")":
                depth -= 1
            i += 1
        argument = line[open_paren:i - 1] if depth == 0 else ""
        if ".c_str()" in argument or '"' in argument:
            start = i
            continue
        line = line[:pos] + "GetPlayerCompat(" + line[open_paren:]
        total += 1
        start = pos + len("GetPlayerCompat(")
    return line, total


GUIDKEY = re.compile(r"(?<![\w.):>])([A-Za-z_]\w*(?:(?:->|\.)[A-Za-z_]\w*(?:\(\))?)*)\.GetRawValue\(\)")
# A number that is not a key: written to a wire or a stream, turned back into a guid, or a forced-facing packet field.
NONKEY = (re.compile(r"<<"),
          re.compile(r"\bWorldPacket\b|QueuePacket|\bEnqueue\(|\bWrite\(|AppendPackedGuid|appendPackGUID|\bByteBuffer\b"),
          re.compile(r"(?<!get)(?<!Get)ObjectGuid\(|GuidPosition\(|GuidFromRawCompat\(|guidStr|printf|SetFacingGUID\("),
          re.compile(r"\bPQuery\(|\bPExecute\("))
# A key whose reader this pass leaves raw, so the writer must stay raw too: (file, line, why). The reader is in a file the
# guid model above cannot be applied to, or in one outside this pass.
GUIDKEY_KEEP_RAW = {
    ("ai/playerbot/strategy/actions/ResetAiAction.cpp", "37"): "ai_playerbot_db_store guid column, shared with the other table writers",
    ("ai/playerbot/strategy/actions/ResetAiAction.cpp", "115"): "ai_playerbot_db_store guid column, shared with the other table writers",
    ("ai/playerbot/strategy/actions/ServiceNearbyNpcAction.cpp", "51"): "NearbyServiceFailParks is read with GetRawValue in MaintenanceValues.cpp",
    ("ai/playerbot/strategy/actions/VictimEscapeActions.cpp", "160"): "SeekMeetHealPuller is produced in BossTacticsActions.cpp, left raw",
    ("host/RangedShotAdapter.cpp", "25"): "RangedShotLedger::Count is read in commands/Commander.cpp, left raw",
}


def transform_guidkey(line):
    if any(p.search(line) for p in NONKEY):
        return line, 0
    return sub_all(GUIDKEY.pattern, lambda m: "GuidKeyCompat(" + m.group(1) + ")", line)


def transform_flag(field, helper):
    def run(line):
        total = 0
        spans = comment_spans(line)
        for m in reversed(list(re.finditer(RECV + r"->HasFlag\(\s*" + field + r"\s*,\s*", line))):
            if any(a <= m.start() < b for a, b in spans):
                continue
            separator = ", " if line[m.end():].strip() else ","
            line = line[:m.start()] + helper + "(" + m.group(1) + separator + line[m.end():]
            total += 1
        return line, total
    return run


def transform_height(line):
    return sub_all(RECV + r"->GetHeight\((?!\))", lambda m: "GetHeightCompat(" + m.group(1) + ", ", line)


def transform_angle(line):
    return sub_all(RECV + r"->GetAngle\(", lambda m: "GetAngleCompat(" + m.group(1) + ", ", line)


def args_of(line, open_paren):
    depth, i, commas = 1, open_paren, 0
    while i < len(line) and depth:
        c = line[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 1:
            commas += 1
        i += 1
    return commas + 1 if depth == 0 else -1


def transform_los(line):
    total = 0
    line, n = sub_all(RECV + r"->IsWithinLOSInMap\(", lambda m: "IsWithinLOSInMapCompat(" + m.group(1) + ", ", line)
    total += n
    spans = comment_spans(line)
    for m in reversed(list(re.finditer(RECV + r"->IsWithinLOS\(", line))):
        if any(a <= m.start() < b for a, b in spans):
            continue
        if args_of(line, m.end()) != 4:
            continue  # the helper has no fifth argument (the Turtle core's optional extra distance)
        line = line[:m.start()] + "IsWithinLOSCompat(" + m.group(1) + ", " + line[m.end():]
        total += 1
    return line, total


def transform_path(line):
    total = 0
    for pattern, repl in (
            (RECV + r"->calculate\(", lambda m: "PathCalculateCompat(*" + m.group(1) + ", "),
            (RECV + r"\.calculate\(", lambda m: "PathCalculateCompat(" + m.group(1) + ", "),
            (RECV + r"->getActualEndPosition\(\)", lambda m: "PathActualEndCompat(*" + m.group(1) + ")"),
            (RECV + r"\.getActualEndPosition\(\)", lambda m: "PathActualEndCompat(" + m.group(1) + ")"),
            (RECV + r"->getPathType\(\)", lambda m: "PathTypeCompat(*" + m.group(1) + ")"),
            (RECV + r"\.getPathType\(\)", lambda m: "PathTypeCompat(" + m.group(1) + ")"),
            (RECV + r"->getPath\(\)", lambda m: "PathPointsCompat(*" + m.group(1) + ")"),
            (RECV + r"\.getPath\(\)", lambda m: "PathPointsCompat(" + m.group(1) + ")"),
            (RECV + r"->Length\(\)", lambda m: "PathLengthCompat(*" + m.group(1) + ")"),
            (RECV + r"\.Length\(\)", lambda m: "PathLengthCompat(" + m.group(1) + ")")):
        line, n = sub_all(pattern, repl, line)
        total += n
    if re.search(r"\bPathType\s+(?:const\s+)?\w+\s*=", line):
        line, n = sub_all(r"(PathType\s+(?:const\s+)?\w+\s*=\s*)PathTypeCompat\(", r"\1PathType(PathTypeCompat(", line)
        if n:
            line, _ = sub_all(r"(PathType\s+(?:const\s+)?\w+\s*=\s*PathType\(PathTypeCompat\([^;]*?)\s*;", r"\1);", line)
            total += n
    return line, total


def transform_headless(line):
    return sub_all(RECV + r"->IsHeadless\(\)", lambda m: "IsHeadlessCompat(" + m.group(1) + ")", line)


def transform_transport(line):
    return sub_all(RECV + r"->HasNetworkTransport\(\)", lambda m: "HasNetworkTransportCompat(" + m.group(1) + ")", line)


def transform_zoneid(line):
    return sub_all(RECV + r"->ZoneId\b(?!\s*\()", lambda m: "AreaZoneIdCompat(" + m.group(1) + ")", line)


def transform_bgmap(line):
    return sub_all(RECV + r"->IsBattleGround\(\)", lambda m: "MapIsBattlegroundCompat(" + m.group(1) + ")", line)


def transform_summon(line):
    return sub_all(RECV + r"->SummonCreature\(",
                   lambda m: "SummonCreatureCompat(" + m.group(1) + (", " if line[m.end():].strip() else ","), line)


def transform_accountid(line):
    return sub_all(r"\bsObjectMgr\.GetPlayerAccountIdByGUID\(", lambda m: "AccountIdOfCharacterCompat(", line)


def transform_playername(line):
    return sub_all(r"\bsObjectMgr\.GetPlayerNameByGUID\(", lambda m: "PlayerNameByGuidCompat(", line)


def transform_playerguid(line):
    return sub_all(r"\bsObjectMgr\.GetPlayerGuidByName\(", lambda m: "PlayerGuidByNameCompat(", line)


def transform_underwater(line):
    return sub_all(RECV + r"->IsUnderwater\(\)", lambda m: "IsUnderwaterCompat(" + m.group(1) + ")", line)


def transform_respawn(line):
    return sub_all(RECV + r"->GetRespawnCoord\(", lambda m: "GetRespawnCoordCompat(" + m.group(1) + ", ", line)


def transform_holder(line):
    return sub_all(RECV + r"->GetHolder\(\)", lambda m: "AuraHolderCompat(" + m.group(1) + ")", line)


def transform_questlevel(line):
    return sub_all(RECV + r"->GetQuestLevel\(\)", lambda m: "QuestLevelCompat(" + m.group(1) + ")", line)


# name -> (row predicate on (message, line), line transform, needs no guid reconstruction in the file)
FAMILIES = {
    "cstr": (lambda msg, line: "to variadic" in msg, transform_cstr, False),
    "player": (lambda msg, line: "no member named 'GetPlayer'" in msg, transform_player, False),
    "guidkey": (lambda msg, line: "GetRawValue()" in line, transform_guidkey, True),
    "unitflags": (lambda msg, line: "no member named 'HasFlag'" in msg and "UNIT_FIELD_FLAGS" in line,
                  transform_flag("UNIT_FIELD_FLAGS", "HasUnitFlagCompat"), False),
    "playerflags": (lambda msg, line: "no member named 'HasFlag'" in msg and "PLAYER_FLAGS" in line,
                    transform_flag("PLAYER_FLAGS", "HasPlayerFlagCompat"), False),
    "height": (lambda msg, line: "call to 'GetHeight'" in msg, transform_height, False),
    "angle": (lambda msg, line: "no member named 'GetAngle'" in msg, transform_angle, False),
    "los": (lambda msg, line: "LineOfSightChecks" in msg, transform_los, False),
    "path": (lambda msg, line: "in 'PathGenerator'" in msg, transform_path, False),
    "headless": (lambda msg, line: "'IsHeadless'" in msg, transform_headless, False),
    "transport": (lambda msg, line: "'HasNetworkTransport'" in msg, transform_transport, False),
    "zoneid": (lambda msg, line: "'ZoneId' in 'AreaTableEntry'" in msg, transform_zoneid, False),
    "bgmap": (lambda msg, line: "'IsBattleGround' in 'Map'" in msg, transform_bgmap, False),
    "summon": (lambda msg, line: "call to 'SummonCreature'" in msg, transform_summon, False),
    "questlevel": (lambda msg, line: "'GetQuestLevel' in 'Quest'" in msg, transform_questlevel, False),
    "accountid": (lambda msg, line: "'GetPlayerAccountIdByGUID'" in msg, transform_accountid, False),
    "playername": (lambda msg, line: "'GetPlayerNameByGUID'" in msg, transform_playername, False),
    "playerguid": (lambda msg, line: "'GetPlayerGuidByName'" in msg, transform_playerguid, False),
    "underwater": (lambda msg, line: "'IsUnderwater'" in msg, transform_underwater, False),
    "respawn": (lambda msg, line: "'GetRespawnCoord'" in msg, transform_respawn, False),
    "holder": (lambda msg, line: "'GetHolder'" in msg, transform_holder, False),
}

# A file that turns a number back into a guid or an online player: the key and the object must agree, and a hash cannot be
# turned back; that is a decision, so the whole file is left for the slices.
RECONSTRUCTS = re.compile(r"(?<!get)(?<!Get)ObjectGuid\(|GuidPosition\(|GuidFromRawCompat\(|FindCharacter\(|QueuePublicNote\(")


def excluded():
    names = set()
    if os.path.exists(EXCLUDE_FILE):
        for line in open(EXCLUDE_FILE):
            line = line.strip()
            if line and not line.startswith("#"):
                names.add(os.path.normpath(line))
    return names


def main():
    args = sys.argv[1:]
    check = "--check" in args
    errors_path = ""
    families = []
    i = 0
    while i < len(args):
        if args[i] == "--errors":
            errors_path = args[i + 1]
            i += 2
            continue
        if args[i] == "--family":
            families.append(args[i + 1])
            i += 2
            continue
        if not args[i].startswith("--"):
            sys.exit("unexpected argument: " + args[i])
        i += 1
    if not errors_path:
        sys.exit("--errors <errors.tsv> is required (forever/tools/vm-census.sh writes it in the census directory)")
    families = families or list(FAMILIES)
    unknown = [f for f in families if f not in FAMILIES]
    if unknown:
        sys.exit("unknown family: " + ", ".join(unknown))

    skip = excluded()
    by_file = {}
    for row in open(errors_path):
        loc, msg = row.rstrip("\n").split("\t", 1)
        if ":" not in loc:
            continue
        path, line_no = loc.rsplit(":", 1)
        if not line_no.isdigit():
            continue
        if not path.startswith(("ai/", "commands/", "runtime/", "host/", "behavior/", "src/")):
            continue
        by_file.setdefault(path, []).append((int(line_no), msg))

    totals = {f: 0 for f in families}
    left = {f: 0 for f in families}
    changed = 0
    for path, rows in sorted(by_file.items()):
        if path.startswith(SKIP_PREFIXES) or path in skip or path in SKIP_FILES:
            continue
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            continue
        lines = open(full, encoding="utf-8").read().split("\n")
        before = list(lines)
        touched = 0
        raw_lines = sorted({i + 1 for i, text in enumerate(lines) if "GetRawValue()" in text})
        for line_no, msg in rows:
            line = lines[line_no - 1]
            for family in families:
                predicate, transform, guid_safe = FAMILIES[family]
                if not predicate(msg, line):
                    continue
                if family == "guidkey" and RECONSTRUCTS.search("\n".join(lines)):
                    left[family] += 1
                    continue
                if family == "guidkey" and (path, str(line_no)) in GUIDKEY_KEEP_RAW:
                    left[family] += 1
                    continue
                new, n = transform(line)
                if n:
                    lines[line_no - 1] = new
                    line = new
                    totals[family] += n
                    touched += 1
                    if check:
                        print(f"{path}:{line_no}: {new.strip()[:160]}")
                else:
                    left[family] += 1
        if "guidkey" in families and not RECONSTRUCTS.search("\n".join(lines)):
            for line_no in raw_lines:
                if (path, str(line_no)) in GUIDKEY_KEEP_RAW:
                    continue
                line = lines[line_no - 1]
                new, n = transform_guidkey(line)
                if n and new != line:
                    lines[line_no - 1] = new
                    totals["guidkey"] += n
                    touched += 1
                    if check:
                        print(f"{path}:{line_no}: {new.strip()[:160]}")
        if lines != before:
            changed += 1
            if not check:
                open(full, "w", encoding="utf-8").write("\n".join(lines))
    for family in families:
        print(f"{family}: {totals[family]} place(s) rewritten, {left[family]} place(s) left", file=sys.stderr)
    print(f"{'would change' if check else 'changed'}: {changed} file(s)")


if __name__ == "__main__":
    main()
