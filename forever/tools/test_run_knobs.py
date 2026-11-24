#!/usr/bin/env python3
"""Checks forever/data/run-knobs-139.tsv against the source (issue #139): every AiPlayerbot.DungeonRun* / BossStand* key has exactly one row,
a "read" row names real lines of TBF-built files that name the config member, a not-read / hard-coded / ported-other row has no reader
in any TBF-built file (so a port that forgets the table fails here), and the shipped conf.dist carries the table's status.

    python3 forever/tools/test_run_knobs.py          # check
    python3 forever/tools/test_run_knobs.py --fix    # move the file:line of every "read" / "ported-now" row to the line that names the member now
"""
import glob, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
os.chdir(ROOT)
STATUSES = {"read", "ported-now", "ported-other", "hard-coded", "not-read"}
failures = []
FIX = "--fix" in sys.argv

# The statements a hard-coded / ported-other row makes about the run glue, as code that must still be there (a line number would drift).
CODE_CHECKS = {
    "DungeonRunRaid": ("forever/src/host/run/TbRun.cpp", 'refuse("group-raid"'),
    "DungeonRunRaidBossRules": ("forever/src/host/run/TbRun.cpp", 'refuse("group-raid"'),
    "DungeonRunRaidHealerGuard": ("forever/src/host/run/TbRun.cpp", 'refuse("group-raid"'),
    "DungeonRunGuardLeavesAssignedTank": ("forever/src/host/run/TbRun.cpp", 'refuse("group-raid"'),
    "DungeonRunGuardClockHittable": ("forever/src/host/run/TbRun.cpp", 'refuse("group-raid"'),
    "DungeonRunRezHold": ("forever/src/host/run/TbRunFacts.cpp", "f.rezHold = true;"),
    "DungeonRunWalkBackRescue": ("forever/src/host/run/TbRunFactsPure.h", "f.walkBackRescue = false;"),
    "DungeonRunDryWalk": ("forever/src/host/run/TbRunFactsPure.h", "f.dryWalk = false;"),
    "DungeonRunBotKeysForPlayers": ("forever/src/host/run/TbRunFactsPure.h", "f.botKeysForPlayers = false;"),
    "DungeonRunFactJudgeCharmedLead": ("forever/src/host/run/TbRunFacts.cpp", "if (lead->IsAlive() && !lead->IsCharmed())"),
    "DungeonRunLootFfa": ("forever/src/host/run/TbRunFactsPure.h", "f.lootFfaOn = false;"),
}
CODE_CHECKS_EXTRA = {
    "DungeonRunLootFfa": [("forever/src/host/run/TbRun.cpp", 'refuse("loot-method"'), ("forever/src/host/run/TbRun.cpp", 'PauseWith(rec, "loot-method");')],
}
# The not-read rows that say "ApplyConfig forces it off": the flag is forced off in the pure half.
FORCED_OFF = {
    "DungeonRunPullStand": "f.pullStand = false;", "DungeonRunStandClearance": "f.standClearance = false;", "DungeonRunStandPullPreCc": "f.standPullPreCc = false;",
    "DungeonRunStandDiscipline": "f.standDiscipline = false;", "DungeonRunAggroEstimate": "f.joinerCeiling = 0;", "DungeonRunAggroCeiling": "f.joinerCeiling = 0;",
    "DungeonRunAggroWalkRing": "f.aggroWalkRing = false;", "DungeonRunStragglerPull": "f.stragglerPull = false;", "DungeonRunRespawnNest": "f.respawnNest = false;",
    "DungeonRunTotemFocus": "f.totemFocus = false;", "DungeonRunPackFocusSparesTacticUnits": "f.packFocusSparesTacticUnits = false;",
    "DungeonRunAutoCcRisky": "f.autoCcRisky = false;",
}


def fail(msg):
    failures.append(msg)
    print("FAIL: " + msg)


def source_keys():
    text = open("ai/playerbot/PlayerbotAIConfig.cpp", encoding="utf-8", errors="replace").read()
    keys = {}
    for m in re.finditer(r'config\.Get(Bool|Int|Float|String)Default\(\s*"AiPlayerbot\.((?:DungeonRun|BossStand)\w*)"', text):
        keys.setdefault(m.group(2), text.count("\n", 0, m.start()) + 1)
    return keys


def member_of(key):
    return key[0].lower() + key[1:]


def built_files():
    """The sources a TBF build compiles: forever/src (any), the slice list, the combat monitor TU."""
    files = set()
    for line in open("forever/SLICE-W4.txt"):
        line = line.strip()
        if line and not line.startswith("#"):
            files.add(line)
    for pat in ("forever/src/**/*.cpp", "forever/src/**/*.h"):
        files.update(glob.glob(pat, recursive=True))
    files.add("runtime/CombatMonitorService.cpp")
    return files


def read_lines(path, cache={}):
    if path not in cache:
        cache[path] = open(path, encoding="utf-8", errors="replace").read().split("\n")
    return cache[path]


def load_table():
    rows = {}
    for raw in open("forever/data/run-knobs-139.tsv", encoding="utf-8"):
        if raw.startswith("#") or not raw.strip():
            continue
        cols = raw.rstrip("\n").split("\t")
        if len(cols) != 5:
            fail("row has %d columns, want 5: %s" % (len(cols), raw[:60]))
            continue
        if cols[0] in rows:
            fail("key twice: " + cols[0])
        rows[cols[0]] = cols
    return rows


def has_member(path, member):
    rx = re.compile(r"\b" + re.escape(member) + r"\b")
    return any(rx.search(l) for l in read_lines(path))


fixes = {}


def check_code(k, path, snippet):
    if not any(snippet in l for l in read_lines(path)):
        fail("%s: %s no longer holds `%s`, the row's statement is stale" % (k, path, snippet))


def main():
    keys = source_keys()
    rows = load_table()
    built = built_files()
    for k in sorted(set(keys) - set(rows)):
        fail("key has no row in the table: " + k)
    for k in sorted(set(rows) - set(keys)):
        fail("row for a key the source does not read: " + k)
    counts = {}
    for k, (key, dflt, status, where, note) in rows.items():
        if k not in keys:
            continue
        counts[status] = counts.get(status, 0) + 1
        member = member_of(k)
        if status not in STATUSES:
            fail("%s: unknown status %s" % (k, status))
            continue
        if status in ("read", "ported-now"):
            cites = [c.strip() for c in where.split(";") if c.strip()]
            if not cites:
                fail("%s: %s with no file:line" % (k, status))
            for cite in cites:
                m = re.fullmatch(r"(.+):(\d+)", cite)
                if not m or not os.path.exists(m.group(1)):
                    fail("%s: bad citation %r" % (k, cite))
                    continue
                path, line = m.group(1), int(m.group(2))
                if path not in built:
                    fail("%s: %s is not a TBF-built file" % (k, path))
                lines = read_lines(path)
                if line > len(lines) or not re.search(r"\b" + member + r"\b", lines[line - 1]):
                    near = [n for n, l in enumerate(lines, 1) if re.search(r"\b" + member + r"\b", l)]
                    if FIX and near:
                        fixes[(k, cite)] = "%s:%d" % (path, min(near, key=lambda n: abs(n - line)))
                    else:
                        fail("%s: %s does not name %s (run with --fix)" % (k, cite, member))
        else:
            for path in sorted(built):
                if path.endswith("PlayerbotAIConfig.cpp") or path.endswith("PlayerbotAIConfig.h"):
                    continue
                if os.path.exists(path) and has_member(path, member):
                    fail("%s is %s but %s reads it: update the table" % (k, status, path))
            # a file:line named in the note (or where) must exist, so a moved file or a deleted line is noticed
            for path, line in re.findall(r"((?:forever|ai|commands|runtime|host)/[\w/.-]+\.(?:cpp|h)):(\d+)", note + " " + where):
                if not os.path.exists(path) or int(line) > len(read_lines(path)):
                    fail("%s: citation %s:%s does not exist" % (k, path, line))
            if k in CODE_CHECKS and status in ("hard-coded", "ported-other"):
                check_code(k, *CODE_CHECKS[k])
                for extra in CODE_CHECKS_EXTRA.get(k, []):
                    check_code(k, *extra)
            elif status in ("hard-coded", "ported-other"):
                fail("%s: no CODE_CHECKS entry for its %s statement" % (k, status))
            if k in FORCED_OFF and status == "not-read":
                check_code(k, "forever/src/host/run/TbRunFactsPure.h", FORCED_OFF[k])
            if status == "not-read":
                m = re.fullmatch(r"TB reader: (.+):(\d+)", where)
                if m:
                    lines = read_lines(m.group(1))
                    if not re.search(r"\b" + member + r"\b", lines[int(m.group(2)) - 1]):
                        fail("%s: TB reader citation %s:%s does not name %s" % (k, m.group(1), m.group(2), member))
    if FIX and fixes:
        text = open("forever/data/run-knobs-139.tsv", encoding="utf-8").read()
        for (k, old), new in fixes.items():
            text = re.sub(r"^(%s\t[^\n]*?)%s" % (re.escape(k), re.escape(old)), lambda m: m.group(1) + new, text, count=1, flags=re.M)
        open("forever/data/run-knobs-139.tsv", "w", encoding="utf-8", newline="\n").write(text)
        print("moved %d citation(s): run the test again" % len(fixes))
        return 0
    gen = subprocess.run([sys.executable, "forever/tools/gen_aiplayerbot_conf.py", "--check"], capture_output=True, text=True)
    if gen.returncode != 0:
        fail("conf.dist is not what gen_aiplayerbot_conf.py writes: " + gen.stdout.strip())
    conf = open("forever/conf/aiplayerbot.conf.dist", encoding="utf-8").read()
    for k, (key, dflt, status, where, note) in rows.items():
        marker = "# AiPlayerbot.%s  (" % k
        at = conf.find(marker)
        # #144 (chore/config-keys-144, merged in #242) ships only keys a compiled file reads and consumes: an
        # unread knob whose member nothing consumes is left out (docs/server/config-keys.md still lists it), while
        # one that is shipped must say so in a TBF note. A read key ships without a TBF note.
        wants_note = status in ("not-read", "hard-coded", "ported-other")
        if at < 0:
            if wants_note:
                continue
            fail("conf.dist has no entry for " + k)
            continue
        entry = conf[at:conf.find("\n\n", at)]
        if wants_note != ("#   TBF:" in entry):
            fail("conf.dist entry of %s %s a TBF note" % (k, "lacks" if wants_note else "has"))
    print("keys: %d  %s" % (len(rows), "  ".join("%s=%d" % kv for kv in sorted(counts.items()))))
    if failures:
        print("%d failure(s)" % len(failures))
        return 1
    print("run knobs table: OK")
    return 0


sys.exit(main())
