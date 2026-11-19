#!/usr/bin/env python3
"""Write the table of forever/docs/RUN-S5-FACTS-RFC.md from the output of forever/tools/facts_rfc.sh.

    python3 forever/tools/facts_rfc_report.py <outdir> [--md OUT.md] [--tsv OUT.tsv]

Two sources per field of `struct Facts` (runtime/DungeonRunPolicy.h):
  dynamic  the access tracer's log: the field was read (a load of its bytes) while DungeonRun::Tick (and the glue calls
           after it) ran on the plan's rows, under the scenarios of facts_trace.cpp; the read site is the first return-chain
           frame inside the policy headers, as file:line; the step kind is the kind of the step the tick began on.
  static   every `X.field` in the policy headers where X is a parameter or local of type Facts (function by function),
           reads and writes told apart: the upper bound, what any step kind could reach.
"""
import collections, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import facts_fields  # noqa: E402

KINDS = ["None", "Move", "MoveDirect", "Pull", "Boss", "Loot", "Open", "UseItem", "WaitObject", "WaitClear", "Rest", "Ritual",
         "Hold", "Gossip", "UseOn"]

# field -> group (first matching rule wins); the groups are the glue's work packages (F6 of slice S6)
GROUPS = [
    ("config copy (aiplayerbot.conf key read once per tick)", r"^(joinerCeiling|aggroWalkRing|packClearMarginYd|packClearRepulls|stragglerPull|pullStand|standClearance|standDiscipline|standPullPreCc|autoCc|autoCcRisky|autoCcAbove|totemFocus|auraGatedTargets|packFocusSparesTacticUnits|respawnNest|walkBackAfterWonFight|retreatAt2Dead|walkBackRescue|dryWalk|teleportReturnYd|reviveHoldYd|fightForward|lightRest|botKeysForPlayers|linkedDeath|nearWipeStop\w*|lootFfaOn|directJoinSupported|bossTacticNoCc|\w*Config\w*)$"),
    ("party roster and lead state", r"^(total|alive|dead|deadNames|members|leadX|leadY|leadZ|leadFloorZ|leadAlive|leadIsTank|leadStaying|leadResting|leadStayWalkResting|leadInCombat|anyInCombat|staleCombat|minHealthPct|minManaPct|maxMemberYd|eating|rezzer|carrier|team|leadOffFloor|leadOffMap\w*|leadInWater|leadOnSurface|leadLeadsGroup|dpsAlive|leadShotYd|leadTargetGuid)$"),
    ("fight reads (engaged units, attackers, lead hit)", r"^(engaged\w*|ownFight\w*|leadAttack\w*|leadHit|leadHasAttacker|partyAttackers|killFirst|ccHeld\w*|combatDrops?\w*|unattackable\w*|nest\w*|fightUnits|hostilesNear\w*|guard\w*)$"),
    ("step read: units, pack, approach, path, floor", r"^(seen|units|hopUnits|packResidue|packNearestLeft\w*|patrollers|standPatrols|approach\w*|leadPath\w*|anchor\w*|directPath\w*|forward\w*|shortcut\w*|pointLiquid|way\w*|stall\w*|island\w*|resumeWay\w*|walkBack\w*)$"),
    ("boss read", r"^boss\w*$|^seek\w*$"),
    ("players in the group (player-led / mixed runs)", r"^(players|itemPlayer|lootTurnPlayer|player\w*)$"),
    ("doors, objects, loot, items, gossip, tasks", r"^(door\w*|legNeed\w*|object\w*|loot\w*|corpse\w*|carrier\w*|task\w*|item\w*|gossip\w*|use\w*)$"),
    ("crowd control (cc)", r"^(cc\w*|autoCc\w*|icon\w*|raidIcons|mark\w*)$"),
    ("raid", r"^(raid\w*|tank\w*|healer\w*)$"),
    ("rescue, fetch, return, rejoin, summon", r"^(rescue\w*|fetch\w*|return\w*|rejoin\w*|summon\w*|direct\w*|stranded\w*)$"),
    ("firing stand, shot, stand-pull", r"^(shot\w*|stand\w*|holdAhead\w*)$"),
    ("hold, ritual, event", r"^(hold\w*|event\w*|clear\w*)$"),
    ("rez hold, resurrection", r"^(rez\w*|revive\w*)$"),
]


# Function name (of a read site) -> (feature, RFC verdict). The first rule that matches a function decides that site; a field takes the
# "yes" verdict when any of its functions says yes, "no" when every one says no, else "check".
FEATURES = [
    (r"^(Door\w*|LegNeed\w*|LeadInOtherPart)$", "doors and leg needs (the RFC plan has 0 such rows)", "no"),
    (r"^(Gossip\w*|Loot\w*|Open\w*|Ritual\w*|UseOn\w*|Task\w*|WaitObject\w*|\w*Object\w*|Hold(Step|DoneNote|Stands\w*|EngageTarget)|CountEvent|RearmEventOnWipe|UpdateHold|\w*Event\w*|NextLootBot|SummonSeen|Proof\w*)$", "row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows)", "no"),
    (r"^(HoldForShot|\w*StandPull\w*|PatrolStandPullGate|StandPullAwayWait|ScopeStandPullFacts|\w*Stand\w*Reach\w*|MoveStep)$", "firing stand / stand-pull (config keys off; 0 authored stand-pull rows)", "no"),
    (r"^(RunAuto\w*|RunCc\w*|\w*Cc\w*)$", "crowd control (a warrior/priest party has none; cc keys off)", "no"),
    (r"^(Raid\w*|\w*Raid\w*)$", "raid", "no"),
    (r"^(Rescue\w*|Fetch\w*|FarReturn\w*|Stranded\w*|Return\w*)$", "rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0)", "no"),
    (r"^(StragglerPull\w*|StragglerNever)$", "straggler pull (key off by default)", "no"),
    (r"^(AheadNoteText|\w*Ahead\w*)$", "aggro estimate notes (key off by default)", "no"),
    (r"^(GatherAnchorInNest|NestLeavable|\w*Nest\w*)$", "respawn nest (key off by default)", "no"),
    (r"^(\w*Rez\w*)$", "rez hold (key off by default)", "no"),
    (r"^(DryWalk\w*|LootFfa\w*)$", "dry walk / free-for-all loot (keys off or forced off)", "no"),
    (r"^(HoldForDirectJoin|NotWithLead|DirectJoin\w*)$", "direct-join step (move_direct rows only: the RFC export has none)", "no"),
    (r"^(Boss\w*|FightStep)$", "boss rows (4 in the plan)", "yes"),
    (r"^(NearWipe\w*)$", "near-wipe stop (key default 2)", "yes"),
    (r"^(PackResidue\w*|PassedResidueNote|NearestLeftNote|CheckAnchor|\w*Anchor\w*)$", "pack-cleared gate / anchor (margin default 10)", "yes"),
    (r"^(FightForward\w*|WalkBack|\w*Shortcut\w*|ApproachHop|ApproachStand\w*)$", "fight forward / walk back / approach hops (key default on)", "yes"),
    (r"^(CheckGuard|EngageTarget|GuardPick|ForgetUnitsThatCame|HealerGuard\w*|Guard\w*|ownFight\w*)$", "fight guard (the tank engages what hits the lead)", "yes"),
    (r"^(Stall\w*|Stuck\w*|RestWalk\w*|Island\w*|SpeedLeash\w*|ExactWalkTo|EscapeWait|NoteLeadWay)$", "stall / stuck / island rules (a walk that fails)", "check"),
    (r"^(TickBody)$", "tick body (run line, rest, hp notes)", "check"),
]


def verdict(funcs):
    seen = []
    for f in funcs:
        for rx, label, v in FEATURES:
            if re.match(rx, f):
                seen.append((label, v))
                break
        else:
            seen.append(("unclassified", "check"))
    vs = {v for _, v in seen}
    labels = sorted({l for l, _ in seen})
    v = "yes" if "yes" in vs else ("check" if "check" in vs else "no")
    return v, "; ".join(labels)[:140]


def group_of(name):
    for g, rx in GROUPS:
        if re.match(rx, name):
            return g
    return "other"


def policy_files():
    # Only DungeonRunPolicy.h reads the run's Facts in Tick (grep "DungeonRun::Facts": the other users are PlayerLedPolicy.h (player-led
    # runs), the decision-capture recorder (off by default) and RunItemNeedFacts.h (item legs): none is part of a bot-led RFC tick).
    return [os.path.join(ROOT, "runtime", "DungeonRunPolicy.h")]


def coverage(outdir):
    """(file basename, line) -> executed? from the gcov files of the scenarios (res/cov/*.gcov)."""
    cov = {}
    for path in glob.glob(os.path.join(outdir, "cov", "*.h.gcov")):
        base = os.path.basename(path)[:-5]
        for line in open(path, encoding="utf-8", errors="replace"):
            parts = line.split(":", 2)
            if len(parts) < 3:
                continue
            count, no = parts[0].strip(), parts[1].strip()
            if not no.isdigit() or no == "0":
                continue
            if count == "-":
                continue
            cov[(base, int(no))] = count not in ("#####", "=====")
    return cov


def static_scan(names):
    """field -> {'read': [(file, line, fn)], 'write': [...]} over the policy headers."""
    sites = {n: {"read": [], "write": []} for n in names}
    alias_rx = re.compile(r"(?<![A-Za-z_])(?:const\s+)?Facts\s*(?:const\s*)?&?\s+(\w+)\b")
    for path in policy_files():
        rel = os.path.relpath(path, ROOT)
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        i = 0
        while i < len(lines):
            # a span starts at a column-0 declaration and ends at the next column-0 `}`
            if re.match(r"^(inline|static|template|struct|namespace|constexpr|class)\b", lines[i]):
                j = i + 1
                if not lines[i].startswith("namespace"):
                    while j < len(lines) and not lines[j].startswith("}"):
                        j += 1
                span = lines[i:j + 1]
                text = "\n".join(span)
                aliases = set(m.group(1) for m in alias_rx.finditer(text)) - {"const"}
                fn = re.sub(r"\s+", " ", lines[i])[:90]
                if aliases:
                    rx = re.compile(r"\b(" + "|".join(re.escape(a) for a in aliases) + r")\s*\.\s*(\w+)\b(\s*(?:\+\+|--|[-+*|&]?=(?!=)|\.\s*(?:push_back|clear|emplace_back|assign|erase|insert|resize)\())?")
                    for k, l in enumerate(span):
                        code = re.sub(r"//.*", "", l)
                        for m in rx.finditer(code):
                            if m.group(2) in sites:
                                kind = "write" if m.group(3) else "read"
                                sites[m.group(2)][kind].append((rel, i + k + 1, fn, i + 1, j + 1))
                i = j + 1 if not lines[i].startswith("namespace") else i + 1
            else:
                i += 1
    return sites


def dynamic(outdir):
    syms = {}
    for line in open(os.path.join(outdir, "syms.tsv")):
        a, fn, loc = line.rstrip("\n").split("\t")
        syms[int(a, 16)] = (fn, loc)
    pol = tuple(os.sep + os.path.join("runtime", n) for n in ("DungeonRunPolicy.h", "RemoteCommanderPolicy.h", "SystemRunPolicy.h",
                                                            "RezHoldPolicy.h", "RemoteCommanderTasks.h", "BoundFightPolicy.h")) + \
        (os.sep + os.path.join("playerbot", "DungeonPlanPolicy.h"), os.sep + os.path.join("playerbot", "BossTacticsPolicy.h"))
    seen = collections.defaultdict(lambda: {"scn": set(), "kinds": collections.Counter(), "lines": collections.Counter(), "hits": 0, "phases": set()})
    other = collections.Counter()
    for fn in sorted(glob.glob(os.path.join(outdir, "out-*.tsv"))):
        scn = os.path.basename(fn)[4:-4]
        for line in open(fn):
            if line.startswith("#"):
                continue
            c = line.rstrip("\n").split("\t")
            field, kind, phase, count = c[0], int(c[2]), int(c[3]), int(c[5])
            frames = [int(x, 16) for x in c[6:12] if x != "0"]
            site = None
            for a in frames:
                loc = syms.get(a, ("?", "?"))[1]
                if loc.split(":")[0].endswith(pol):
                    site = os.path.relpath(loc.split(":")[0].split("wt" + os.sep, 1)[-1] if "wt" + os.sep in loc else loc, "/") + ":" + loc.split(":")[1]
                    site = loc.split("wt/", 1)[-1]
                    break
            if site is None:
                other[(field, syms.get(frames[0], ("?", "?"))[1] if frames else "?")] += 1
                site = "(outside the policy headers)"
            d = seen[field]
            d["scn"].add(scn)
            d["kinds"][KINDS[kind] if 0 <= kind < len(KINDS) else "-"] += 1
            d["lines"][site] += count
            d["hits"] += count
    return seen, other


def main():
    outdir = sys.argv[1]
    fields = facts_fields.fields()
    names = [f[0] for f in fields]
    stat = static_scan(names)
    dyn, other = dynamic(outdir)
    cov = coverage(outdir)
    rows = []
    base = "DungeonRunPolicy.h"

    def ran(site):  # (a) the line itself ran, (b) its function ran at all
        line_ran = cov.get((base, site[1]), False)
        span_ran = any(cov.get((base, n), False) for n in range(site[3], site[4] + 1))
        return line_ran, span_ran

    for name, line, typ in fields:
        s_ = stat[name]
        d = dyn.get(name)
        lr = sum(1 for x in s_["read"] if ran(x)[0])
        fr = sum(1 for x in s_["read"] if ran(x)[1])
        rows.append({"name": name, "decl": line, "type": typ, "group": group_of(name), "dyn": d, "sread": s_["read"],
                     "swrite": s_["write"], "line_ran": lr, "fn_ran": fr})
    tot = len(rows)
    A = [r for r in rows if r["dyn"]]
    # (a read site on a line that ran, in an `a && f.x` / `a || f.x` chain whose left side decided, is not a read: those fields land in C1)
    B = [r for r in rows if not r["dyn"] and r["line_ran"]]
    C1 = [r for r in rows if not r["dyn"] and r["sread"] and r["fn_ran"]]
    C2 = [r for r in rows if not r["dyn"] and r["sread"] and not r["fn_ran"]]
    D = [r for r in rows if not r["dyn"] and not r["sread"]]
    nd, ns, nn = len(A), len(C1) + len(C2), len(D)
    bygroup = collections.OrderedDict()
    for r in rows:
        g = bygroup.setdefault(r["group"], {"all": 0, "A": 0, "C1": 0, "C2": 0, "D": 0})
        g["all"] += 1
        g["A"] += r in A
        g["C1"] += r in C1
        g["C2"] += r in C2
        g["D"] += r in D
    md = []
    md.append(f"Counts: {tot} fields. A read in the simulated RFC run: {len(A)}. "
              f"C1 read in a function the run executed, in a branch or short-circuit operand it did not take ({len(B)} of them on a line that ran): {len(C1)}. C2 read only in functions the run never "
              f"called: {len(C2)}. D no read site in DungeonRunPolicy.h: {len(D)}.\n")
    md.append("| group | fields | A run | C1 gated branch | C2 uncalled function | D never read |\n|---|---|---|---|---|---|")
    for g, v in bygroup.items():
        md.append(f"| {g} | {v['all']} | {v['A']} | {v['C1']} | {v['C2']} | {v['D']} |")
    md.append("")
    md.append("### A: read in the simulated RFC run (field, declared line, step kinds, scenarios, first read sites)\n")
    md.append("| field | decl | kinds (reads) | scenarios | read sites (hits) |\n|---|---|---|---|---|")
    for r in sorted(A, key=lambda r: (r["group"], r["decl"])):
        d = r["dyn"]
        kinds = ", ".join(f"{k} {v}" for k, v in sorted(d["kinds"].items()))
        sites = "; ".join(f"{l} ({n})" for l, n in d["lines"].most_common(3))
        md.append(f"| {r['name']} | {r['decl']} | {kinds} | {','.join(sorted(d['scn']))} | {sites} |")
    md.append("")
    for title, lst in (("C1: read in an executed function, branch not taken in the simulation", C1),
                       ("C2: read only in functions the simulation never called", C2)):
        md.append(f"### {title}\n")
        md.append("| field | decl | read sites (count; first) | in functions | RFC verdict: feature |\n|---|---|---|---|---|")
        for r in sorted(lst, key=lambda r: (r["group"], r["decl"])):
            fns = sorted(set(re.sub(r"\(.*", "", f[2]).split()[-1] for f in r["sread"]))
            v, why = verdict(fns)
            r["verdict"] = v
            md.append(f"| {r['name']} | {r['decl']} | {len(r['sread'])}; {r['sread'][0][0]}:{r['sread'][0][1]} | {', '.join(fns[:4])}{' ...' if len(fns) > 4 else ''} | {v}: {why} |")
        md.append("")
    vc = collections.Counter(r.get("verdict", "?") for r in C1 + C2)
    md.insert(1, f"Verdict of the {len(C1) + len(C2)} not-traced fields by the feature of the function that reads them: yes (RFC rows can reach it) {vc['yes']}, check (stall and tick-body rules, depends on the run) {vc['check']}, no (a feature the RFC plan or the config does not use) {vc['no']}.\n")
    md.append("### D: never read in DungeonRunPolicy.h\n")
    md.append(", ".join(r["name"] for r in D) or "none")
    out = "\n".join(md)
    if "--md" in sys.argv:
        open(sys.argv[sys.argv.index("--md") + 1], "w").write(out + "\n")
    else:
        print(out)
    if "--tsv" in sys.argv:
        with open(sys.argv[sys.argv.index("--tsv") + 1], "w") as o:
            o.write("field\tdecl\tgroup\tdynamic\tstatic_reads\tstatic_writes\tkinds\tfirst_site\n")
            for r in rows:
                d = r["dyn"]
                o.write("\t".join([r["name"], str(r["decl"]), r["group"], "yes" if d else "no", str(len(r["sread"])), str(len(r["swrite"])),
                                   ",".join(sorted(d["kinds"])) if d else "", (d["lines"].most_common(1)[0][0] if d else (f"{r['sread'][0][0]}:{r['sread'][0][1]}" if r["sread"] else ""))]) + "\n")
    print(f"fields {tot}: A {len(A)}, B {len(B)}, C1 {len(C1)}, C2 {len(C2)}, D {len(D)}; reads outside policy headers: {sum(other.values())}", file=sys.stderr)


if __name__ == "__main__":
    main()
