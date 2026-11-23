#!/usr/bin/env python3
"""Slice 11 wiring test (hunter, warlock, pets): checks that the pieces of the slice agree with each other. No build, no network, no core.

    python3 forever/tools/test_slice11_wiring.py

What it proves (each is a bug the build alone does not catch):
  * SLICE11.txt: every path exists, no duplicates, every path of SLICE5.txt is in it, the 22 hunter and warlock .cpp are in it.
  * the three both-core compat pairs (K11h, K11w, P11): the ForeverCore header and the Turtle header define the same helper names (the Turtle K11h header does not
    repeat IsImmobilizedStateCompat: Turtle has it in ai/cmangos-compat-shim.h), and each wrapper in ai/playerbot/ names both headers.
  * the four pet helpers that used to answer "not ported" are gone from both Unported headers (a duplicate definition would not compile).
  * the five PlayerbotFactory members are no longer link stand-ins and are defined in TbHostPetFactory.cpp.
  * the call sites include the wrapper of the helper they call, and no call site still names the original core call.
  * the starter pet table of TbHostPetFactory.cpp is the CSV of forever/data/slice11 (race, entry).
  * the slice factory carries the kits of the spec (FOREVER-SLICE11-PETS-SPEC section 2).
  * the registry generator is up to date and the registry check has no (a) line.
"""
import csv, os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
failures = []


def check(cond, text):
    if not cond:
        failures.append(text)


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def lines_of(rel):
    return [l.strip() for l in read(rel).splitlines() if l.strip() and not l.startswith("#")]


def code_only(rel):
    return re.sub(r"//[^\n]*", "", read(rel))


# ---- the list ----
s11 = lines_of("forever/SLICE11.txt")
s5 = lines_of("forever/SLICE5.txt")
check(len(s11) == len(set(s11)), "SLICE11.txt has duplicate paths")
for p in s11:
    check(os.path.isfile(os.path.join(ROOT, p)), "SLICE11.txt names a missing file: " + p)
check(set(s5) <= set(s11), "SLICE11.txt lacks paths of SLICE5.txt: " + ", ".join(sorted(set(s5) - set(s11))[:5]))
klass = [p for p in s11 if "/strategy/hunter/" in p or "/strategy/warlock/" in p]
check(len(klass) == 22, "SLICE11.txt should hold 22 hunter and warlock .cpp, has %d" % len(klass))
# Integration batch 2: SLICE11 is the newest list, so it holds every path of the earlier slice lists too (slices 8 and 10 merged in).
s8 = lines_of("forever/SLICE8.txt")
s10 = lines_of("forever/SLICE10.txt")
check(set(s8) <= set(s11), "SLICE11.txt lacks paths of SLICE8.txt: " + ", ".join(sorted(set(s8) - set(s11))[:5]))
check(set(s10) <= set(s11), "SLICE11.txt lacks paths of SLICE10.txt: " + ", ".join(sorted(set(s10) - set(s11))[:5]))
for extra in ("ai/playerbot/strategy/actions/CancelChannelAction.cpp", "ai/playerbot/strategy/actions/CombatActions.cpp",
              "ai/playerbot/strategy/values/ActiveSpellValue.cpp"):
    check(extra in s11, "SLICE11.txt lacks " + extra)
# W6a compiles EquipAction.cpp (forever/SLICE9.txt): the warlock stones call the real EquipItem, so no StandIn is left.
check("void ai::EquipAction::EquipItem(PlayerbotAI*, Player*, Item*, bool)" not in read("forever/src/slice/SliceLinkStubs.cpp"), "SliceLinkStubs.cpp still defines an EquipAction::EquipItem StandIn next to the compiled EquipAction.cpp")

# ---- compat pairs ----
HELPER = re.compile(r"\binline\s+[\w:<>&\*\s]+?\b(\w+Compat)\s*\(")


def helpers(rel):
    return set(HELPER.findall(code_only(rel)))


PAIRS = [
    ("K11h", "forever/src/compat/TbCompat_K11h.h", "ai/cmangos-compat-k11h.h", "ai/playerbot/TbCompatK11h.h", {"IsImmobilizedStateCompat"}),
    ("K11w", "forever/src/compat/TbCompat_K11w.h", "ai/cmangos-compat-k11w.h", "ai/playerbot/TbCompatK11w.h", set()),
    ("P11", "forever/src/compat/TbCompat_P11.h", "ai/cmangos-compat-p11.h", "ai/playerbot/TbCompatP11.h", set()),
]
for name, f_rel, t_rel, w_rel, only_f in PAIRS:
    f, t = helpers(f_rel), helpers(t_rel)
    check(len(f) >= 2, "%s: the ForeverCore header defines too few helpers (%s)" % (name, sorted(f)))
    check(f - only_f == t, "%s: helper names differ: ForeverCore %s, Turtle %s (F-only allowed: %s)" % (name, sorted(f - only_f), sorted(t), sorted(only_f)))
    check(only_f <= f, "%s: %s should be defined on the ForeverCore side" % (name, sorted(only_f)))
    w = read(w_rel)
    base_f, base_t = os.path.basename(f_rel), os.path.basename(t_rel)
    check(base_f in w and base_t in w and "__has_include" in w, "%s: wrapper %s must pick %s or %s" % (name, w_rel, base_f, base_t))
    check("#pragma once" in read(f_rel) and "#pragma once" in read(t_rel), "%s: both headers need #pragma once" % name)

# ---- the moved helpers ----
MOVED = ("PetLearnSpellCompat", "PetToggleAutocastCompat", "PetDismissCommandCompat", "ForAllGuardiansCompat")
for rel in ("forever/src/compat/TbCompat_Unported.h", "ai/cmangos-compat-unported.h"):
    text = code_only(rel)
    for h in MOVED:
        check(not re.search(r"\b%s\s*\(" % h, text), "%s still defines %s (moved to the P11 pair)" % (rel, h))
for h in MOVED + ("PetDeadCompat", "HasStoredPetCompat"):
    for rel in ("forever/src/compat/TbCompat_P11.h", "ai/cmangos-compat-p11.h"):
        check(re.search(r"\b%s\s*\(" % h, read(rel)) is not None, "%s lacks %s" % (rel, h))

# ---- the factory stand-ins ----
stubs = read("forever/src/slice/SliceLinkStubs.cpp")
fact = read("forever/src/host/TbHostPetFactory.cpp")
for member in ("InitAmmo()", "InitPet(bool", "InitPetSpells()", "PetAutoTrainAllowed(Player*", "TrainHunterPet(Player*"):
    check("PlayerbotFactory::" + member not in stubs, "SliceLinkStubs.cpp still defines PlayerbotFactory::" + member)
    check("PlayerbotFactory::" + member in fact, "TbHostPetFactory.cpp does not define PlayerbotFactory::" + member)

# ---- call sites ----
SITES = {
    "ai/playerbot/strategy/actions/GenericActions.cpp": "TbCompatP11.h",
    "ai/playerbot/PlayerbotAI.cpp": "TbCompatP11.h",
    "ai/playerbot/strategy/values/StatsValues.cpp": "TbCompatP11.h",
    "ai/playerbot/strategy/hunter/HunterActions.cpp": "TbCompatK11h.h",
    "ai/playerbot/strategy/hunter/HunterActions.h": "TbCompatK11h.h",
    "ai/playerbot/strategy/hunter/HunterTriggers.h": "TbCompatK11h.h",
    "ai/playerbot/strategy/actions/HunterRangedTelemetry.h": "TbCompatK11h.h",
    "ai/playerbot/strategy/actions/CombatActions.cpp": "TbCompatK11h.h",
    "ai/playerbot/strategy/warlock/WarlockActions.h": "TbCompatK11w.h",
    "ai/playerbot/strategy/warlock/WarlockSpreadTargeting.h": "TbCompatK11w.h",
}
for rel, inc in SITES.items():
    check(('"playerbot/%s"' % inc) in read(rel), "%s does not include %s" % (rel, inc))
# a call site that still names the original core call would not compile on ForeverCore
FORBIDDEN = {
    "ai/playerbot/strategy/hunter/HunterActions.cpp": ("PLAYER_AMMO_ID", "getThreatList", "!= HAPPY"),
    "ai/playerbot/strategy/hunter/HunterActions.h": ("->isTameable(",),
    "ai/playerbot/strategy/hunter/HunterTriggers.h": ("PLAYER_AMMO_ID", "->GetDistance(target, SizeFactor", "->GetCombatDistance("),
    "ai/playerbot/strategy/warlock/WarlockActions.cpp": ("StoreNewItemInInventorySlot",),
    "ai/playerbot/strategy/warlock/WarlockActions.h": ("->IsImmuneToSpellEffect(", "GetRawValue()"),
    "ai/playerbot/strategy/warlock/WarlockSpreadTargeting.h": ("->HasExtraFlag(",),
    "ai/playerbot/strategy/warlock/WarlockTriggers.cpp": ("Spell::CalculatePowerCost",),
    "ai/playerbot/strategy/actions/CombatActions.cpp": ("->AttackStop(true)",),
    "ai/playerbot/strategy/values/StatsValues.cpp": ("GetPetGuid()",),
    "ai/playerbot/strategy/actions/GenericActions.cpp": ("UnportedBlockCompat(tbcompat::Feature::Pets", "FROM character_pet"),
}
for rel, names in FORBIDDEN.items():
    text = code_only(rel)
    for n in names:
        check(n not in text, "%s still contains %s" % (rel, n))

# ---- starter pets ----
RACE_IDS = {"RACE_HUMAN": 1, "RACE_ORC": 2, "RACE_DWARF": 3, "RACE_NIGHTELF": 4, "RACE_UNDEAD_PLAYER": 5, "RACE_TAUREN": 6, "RACE_GNOME": 7, "RACE_TROLL": 8}
code = {}
for m in re.finditer(r"\{\s*(RACE_\w+)\s*,\s*(\d+)\s*\}", fact):
    code[RACE_IDS[m.group(1)]] = int(m.group(2))
rows = list(csv.DictReader(open(os.path.join(ROOT, "forever/data/slice11/hunter-starter-pets.csv"), newline="")))
csv_races = {int(r["race_id"]): int(r["entry"]) for r in rows if r["race_id"].isdigit()}
check(code == csv_races, "the starter pet table of TbHostPetFactory.cpp %s is not the CSV %s" % (code, csv_races))
fb = {r["race_id"]: int(r["entry"]) for r in rows if not r["race_id"].isdigit()}
check(re.search(r"kAllianceFallback\s*=\s*%d" % fb.get("fallback-alliance", -1), fact) is not None, "alliance fallback differs from the CSV")
check(re.search(r"kHordeFallback\s*=\s*%d" % fb.get("fallback-horde", -1), fact) is not None, "horde fallback differs from the CSV")

# ---- the kits ----
fac = read("forever/src/slice/SliceAiFactory.cpp")
KITS = [
    'combatEngine->addStrategies(tab == 0 ? "beast mastery" : (tab == 2 ? "survival" : "marksmanship"), "dps assist", "ranged", "flee", "aspect", "sting", "pet", "buff",',
    'combatEngine->addStrategies(tab == 1 ? "demonology" : (tab == 2 ? "destruction" : "affliction"), "dps assist", "ranged", "flee", "pet", "curse", "buff",',
    'nonCombatEngine->addStrategies(tab == 0 ? "beast mastery" : (tab == 2 ? "survival" : "marksmanship"), "dps assist", "aspect", "pet", "buff", "follow", "food", "loot", "roll", "default",',
    'nonCombatEngine->addStrategies(tab == 1 ? "demonology" : (tab == 2 ? "destruction" : "affliction"), "dps assist", "pet", "pet auto", "buff", "follow", "food", "loot", "roll", "default",',
    "GuardedContext<HunterAiObjectContext>",
    "GuardedContext<WarlockAiObjectContext>",
]
for k in KITS:
    check(k in fac, "SliceAiFactory.cpp lacks: " + k)
pol = read("forever/src/slice/SliceSpecPolicy.h")
check(re.search(r"case ClassHunter:\s*\n\s*return 1;", pol) is not None, "a hunter without talents must default to spec tab 1 (marksmanship)")


# ---- the registry ----
def run(*cmd):
    return subprocess.run([sys.executable, *cmd], cwd=ROOT, capture_output=True, text=True)


r = run("forever/tools/slice_gen_context.py", "--check")
check(r.returncode == 0, "slice_gen_context.py --check: " + (r.stdout + r.stderr).strip()[:200])
r = run("forever/tools/slice_registry_check.py", "--quiet")
check(r.returncode == 0 and "(a)=0" in r.stdout, "slice_registry_check.py: " + (r.stdout + r.stderr).strip()[-300:])
ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
for key in ("initialize pet", "toggle pet spell", "set pet stance", "pet attack", "no pet", "has pet", "new pet", "pet dead", "pet happy", "active spell", "cancel channel", "pet"):
    check('creators["%s"]' % key in ctx, "the slice registry lacks the key " + key)
for key in ("flee with pet",):
    check('creators["%s"]' % key not in ctx, "the slice registry must NOT hold " + key)
check('creators["manual saved string"]' in ctx,
    "piece c-store registers manual saved string (its wiring pins the row)")

if failures:
    print("FAIL: %d problem(s)" % len(failures))
    for f in failures:
        print("  - " + f)
    sys.exit(1)
print("OK: slice 11 wiring (%d paths in SLICE11.txt, %d class files, 3 compat pairs, %d starter races)" % (len(s11), len(klass), len(code)))
