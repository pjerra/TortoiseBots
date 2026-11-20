#!/usr/bin/env python3
"""Generates forever/src/slice/SliceAiObjectContext.cpp (FOREVER-SLICE1-SPEC section 4, package R2; slice 2 keys added in R3).

    python3 forever/tools/slice_gen_context.py            # rewrite the file
    python3 forever/tools/slice_gen_context.py --check    # exit 1 when the file on disk differs from what would be written
    python3 forever/tools/slice_gen_context.py --report   # also list registered classes whose .cpp is not in SLICE2.txt

The key lists are SLICE_KEYS below (the spec's lists, corrected against the code). For each key the creator text is
copied byte for byte from the full build's context header, inside a context class of the same shape (same
shared/siblings flags). The headers of the classes the creators build are found by scanning ai/playerbot for the class
definition, so the slice file includes only what it registers. slice_registry_check.py proves the copy is faithful.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402

ROOT = chk.ROOT
OUT = chk.SLICE_CTX

# The spec's lists (FOREVER-SLICE1-SPEC section 4). Differences from the spec are recorded in
# forever/docs/SLICE1-R2-NOTES.md; at the time of writing there are none.
# Slice 2 (R3, decision D6): SLICE2_KEYS below are the 69 keys of forever/docs/SLICE2-KEYS.md, added to the slice-1 lists.
SLICE_KEYS = {
    "strategy": "dps assist, close, follow",
    "action": "melee, reach melee, follow, stop follow, dps assist, select new target, set combat state, "
              "set non combat state, set dead state, interrupt current spell, formation station",
    "trigger": "combat start, combat end, death, enemy out of melee, invalid target, in pve, in pvp, in raid fight, "
               "often, very often, random, critical health, almost full health, low health, target critical health, "
               "light rage available, medium rage available, high rage available, melee light aoe, melee medium aoe, "
               "melee high aoe, melee very high aoe, ranged light aoe, ranged medium aoe, not dps target active, "
               "update follow, stop follow, master target active, out of free move range, formation station",
    "value": "action useful, add hazard, and, aoe count, aoe heal, aoe position, attack target, "
             "attacker of non-tank target, attackers, attackers count, attackers targeting me, automated attack target, "
             "bag space, balance, behind, can free move, cc target, closest attacker targeting me, collision, combat, "
             "combat start time, combo, corpse run, current cc target, current target, dead, death count, distance, "
             "dps aoe target, dps target, duel target, durability, durability inventory, enemy healer target, energy, "
             "estimated group dps, explicit attack target, facing, focus heal targets, follow target, formation, "
             "formation position, free move center, free move range, future travel destinations, group, group and, group members, group or, "
             "group ready, has aggro, has area debuff, has attackers, has mana, has strategy, has totem, hazards, "
             "health, ignore rpg target, incoming damage, inside target, interrupt target, invalid target, "
             "inventory items, item count, item for spell, last area trigger, last long move, last movement, "
             "last spell cast, last spell cast time, last taxi, leader travel target, lowest durability, main tank, "
             "mana, mana save level, manual bool, manual follow target, manual int, manual saved int, manual string, "
             "manual time, master target, mounted, move style, moving, my attacker count, my threat, nearest adds, "
             "nearest damaging traps, nearest dynamic objects no los, nearest friendly players, nearest game objects, "
             "nearest game objects no los, nearest npcs, nearest stealthed units, neglect threat, new player nearby, "
             "old target, party member to protect, party member to remove roots, party member without aura, "
             "party member without my aura, party tank without aura, pet target, pos, position, possible adds, "
             "possible attack targets, possible targets, possible targets no los, possible triggers, pull target, rage, "
             "random bot update, range, rpg target, rti, rti cc, rti cc target, rti target, run cc family, self target, "
             "skip spells list, snare target, spell cast useful, spell id, stance, stay time, stored hazards, swimming, "
             "talent spec, tank target, tank threat, tank to remove roots, threat, travel target, trigger active, "
             "trinkets on use, wait for attack time",
}
SLICE2_KEYS = {
    "strategy": "ranged, food, loot, cast time, default, heal interrupt",
    "action": "reach spell, reach party member to heal, flee, shoot, drink, food, use, loot, open loot, move to loot, "
              "add all loot, store loot",
    "trigger": "enemy out of spell, enemy too close for spell, enemy five yards, medium mana, no mana, high mana, "
               "medium health, healer should attack, healer should wand, melee medium aoe and healer should attack, "
               "medium threat, target of attacker, party member critical health, party member low health, "
               "party member medium health, party member almost full health, party member to heal out of spell range, "
               "group heal setting, medium group heal setting, resume drink, resume eat, loot available, can loot, "
               "far from loot target, timer, loot response, heal target full health",
    "value": "party member to heal, party member to dispel, buff targets, patchwerk soaker to shield, drink duration, "
             "eat duration, drink unfinished, rest to full, should drink, should eat, spell ready, custom position, "
             "nearest npcs no los, loot target, available loot, has available loot, can loot, always loot list, "
             "skip loot list, should loot object, stack space for item, force item usage, nearest corpses, "
             "loot strategy, should get money, inventory item ids, incoming heals",
}
# Slice 3 (porting worker, FOREVER-SLICE3-SPEC section 3, listed with creator file:line in forever/docs/SLICE3-KEYS.md): the tank, the pull, the stay
# and wait strategies and the order actions. Keys the spec deliberately leaves out: `should pull`, `pull nearest target`, `chat`, `pull back`.
SLICE3_KEYS = {
    "strategy": "tank assist, threat, stay, free, passive, tank face, wait for attack",
    "action": "tank assist, stay, return to stay position, pull my target, end pull, pull start, pull action, pull end, reach pull, "
              "release pull hold, return to pull position, wait for attack keep safe distance, tank face away, set facing, "
              "successor white cap noop, threat reset white stop noop, attack my target, attack rti target, follow chat shortcut, "
              "stay chat shortcut, follow target",
    "trigger": "tank assist, lose aggro, has aggro, party member attacked, protect party member, tank face needed, high threat, "
               "return to stay position, pull start, pull end, pull hold expired, pull anchor done, return to pull position, "
               "wait for attack safe distance, successor white cap, threat reset white stop",
    "value": "master position",
}
# Slice 4 (package P4, FOREVER-SLICE4-SPEC section 3, listed with creator file:line in forever/docs/SLICE4-P-NOTES.md section 4): death and resurrection. Left out on
# purpose (each one does less): repop, self resurrect, spirit healer, should spirit healer, find corpse, revive from corpse, corpse near, dead (trigger),
# falling far, move long stuck, bg reset objective force, corpse run, resurrect request (the host calls the action), area trigger, reach area trigger,
# within area trigger, use meeting stone, summon.
SLICE4_KEYS = {
    "strategy": "dead",
    "action": "auto release, accept resurrect, release, reach party member to resurrect",
    "trigger": "party member dead, resurrect",
    "value": "party member to resurrect",
}
# `behind` (the rogue kit's strategy, SetBehindCombatStrategy) and the three keys it fires (`set behind`, `rear flank`, `rear flank needed`) are NOT in the spec's table
# (its closure read only the mage and rogue folders, not the kit): H8 adds them because the spec's rogue kit names `behind` (section 2) and section 5 relies on it.
# Slice 8 (package H8, FOREVER-SLICE-MAGE-ROGUE-SPEC section 3 "Registry keys", forever/docs/SLICE8-K-NOTES.md "Needs from H8"; listed with creator file:line in
# forever/docs/SLICE8-H-NOTES.md): the keys the compiled mage and rogue contexts and their kits name that the slice did not register yet. The give-item values
# (`party member without item/food/water`) are not in the spec's table but `give food`/`give water` read them (K8 notes). Left unregistered on purpose (each one does
# less): apply oil, throw grenade, oil of immolation, thistle tea (item casts); enemy flagcarrier near, player has flag, enemy player target, has enemy player targets
# (PvP); grind target (the stealth trigger then reads `dps target` only); manual saved string; no blink instance; hemorrhage back/front (no context defines them).
SLICE8_KEYS = {
    "strategy": "hold position, behind",
    "action": "give food, give water, cancel channel, set behind, rear flank, reach cc",
    "trigger": "give food, give water, low mana, enemy ten yards, enemy is close, behind target, not behind target, high energy available, "
               "no energy available, has aggro in raid group, rear flank needed",
    "value": "party member without item, party member without food, party member without water",
}
# Slice 10 (package H10, FOREVER-SLICE10-HYBRIDS-SPEC section 3 "Registry keys", forever/docs/SLICE10-K-NOTES.md section 4; listed with creator file:line in
# forever/docs/SLICE10-H-NOTES.md): the keys the compiled druid, paladin and shaman contexts and their kits name that the slice did not register yet. `low mana` and
# `enemy is close` are not in the spec's table but the restoration shaman (`low mana`) and the holy paladin (`enemy is close`) kits fire them (K10 notes: also on
# slice 8's list, listed once). Left unregistered on purpose (each one does less): manual saved string (D5: totem mode stays auto), travel target traveling, enemy player
# target, has enemy player targets, grind target; enemy flagcarrier near, player has flag, new pet, has (greater) blessing of salvation, resist aura swap over, not
# sensing undead, kick, aquatic form; apply oil/stone, oil of immolation, throw grenade, mana/stoneshield potion, remove (greater) blessing of salvation, set pet stance,
# sense undead, switch to melee/ranged, cancel channel, powershift, reach party member for totem (no totem order). Cat, balance, enhancement, elemental and the Turtle
# shaman tank kit are out of the slice (D2), so behind target / not behind target (the feral dps) stay out too. `restore spec aura` (the spec's table lists it) is NOT registered:
# its class RestoreSpecAuraAction is defined in strategy/actions/DungeonActions.cpp (1,036 lines of raid and dungeon actions, not in the list: the option-ON link failed with
# `undefined vtable for ai::RestoreSpecAuraAction`), and its only trigger `resist aura swap over` is on the unregistered list, so the action could never fire.
SLICE10_KEYS = {
    "strategy": "",
    "action": "move out of enemy contact",
    "trigger": "enemy too close for melee, not facing target, rooted, party member rooted, tank rooted, enough mana, healer low mana, medium aoe heal, "
               "ranged medium aoe and healer should attack, target of attacker close, loose adds near, low mana, enemy is close",
    "value": "has own totem, have any own totem, possible attack targets count, revive targets, boost targets, healer low mana",
}
# Slice 11 (package H11, FOREVER-SLICE11-PETS-SPEC sections 3 and 10, listed with creator file:line in forever/docs/SLICE11-NOTES.md section 5): the generic pet keys the hunter and
# warlock kits fire, and the hunter's dead-zone and range keys. Left unregistered on purpose (each one makes the bot do less, spec section 3): the chat `tame` action and
# trigger (TameAction.cpp stays out), `flee with pet`, `boss tactics seek pet`, `thaddius *`, `manual saved string` (so `choice pet`, `choice sting`, `choice aspect` and
# `.bot choice` stay off, as S10 D5), `apply oil`, `apply stone`, `equip upgrades`, `dispel enrage` (Tranquilizing Shot is level 60), `player has flag`, `nearest stealthed unit`,
# `warlock spread attacker my aura` (the raid DoT spread), `aoe fear`. `pet target` is registered since slice 1.
SLICE11_KEYS = {
    "strategy": "",
    "action": "initialize pet, toggle pet spell, set pet stance, pet attack, pet, cancel channel, switch to melee, switch to ranged",
    "trigger": "no pet, has pet, new pet, pet attack, pet, enemy ten yards, enemy fifteen yards, low mana, target of attacker close, has aggro in raid group",
    "value": "pet dead, pet happy, active spell",
}
def _join(*ds):
    # One entry per key and kind: a key two slice lists both name (slice 8's and slice 10's `low mana`, `enemy is close`) is
    # registered once, in the order of the first list that names it.
    out = {}
    for k in SLICE_KEYS:
        seen = []
        for d in ds:
            for x in d[k].split(","):
                x = x.strip()
                if x and x not in seen:
                    seen.append(x)
        out[k] = seen
    return out
# Slice 9 (package L9, FOREVER-SLICE9-SPEC section 6, listed with creator file:line in forever/docs/SLICE9-L-NOTES.md): item loot and group loot rolls. The
# `roll` strategy is the one whose `very often` trigger fires `auto loot roll`; `loot start roll` / `loot vote` are the pass-through names of the
# `default` strategy (WorldPacketHandlerStrategy::supported), fed by the poll of TbCompat_S9.h. Left out on purpose (each one does less): `loot roll won`
# and `log roll won` (nothing feeds them; the trigger's second action is `equip upgrades`, so registering it would wire AUTO-EQUIP), `delayed roll`,
# `equip upgrades`, `item push result`; the item usage value's own dependencies (mount list, craft spells, should craft spell, free money for): their
# absence reads as the empty value, so no mount, craft or purchase is planned.
SLICE9_KEYS = {
    "strategy": "roll",
    "action": "loot start roll, loot vote, loot roll, auto loot roll, roll, release loot",
    "trigger": "loot start roll, loot vote, loot roll",
    "value": "active rolls, seen need rolls, item usage, skip go loot list",
}
# W6a (package E6): auto-equip upgrades for free bots (`equipment audit` 300 s + `item push result` bag-generation poll).
# Other keys named by MaintenanceStrategy.cpp (`clean quest log`, `use random recipe`, `open random item`, `disenchant random item`,
# `enchant random item`, `smart destroy item`, `pool gear top up`, `use random quest item`, `auto share quest`, `auto complete quest`)
# stay unregistered and are skipped by the engine.
W6_KEYS = {
    "strategy": "maintenance",
    "action": "equip upgrades",
    "trigger": "equipment audit, item push result",
    "value": "",
}
KEYS = _join(SLICE_KEYS, SLICE2_KEYS, SLICE3_KEYS, SLICE4_KEYS, SLICE8_KEYS, SLICE10_KEYS, SLICE11_KEYS, SLICE9_KEYS, W6_KEYS)

# Local context class names and the full-build context class each one copies the shape of (order = registration order).
KIND_ORDER = ("strategy", "action", "trigger", "value")
BASE_T = {"strategy": "Strategy", "action": "Action", "trigger": "Trigger", "value": "UntypedValue"}
LOCAL = {"StrategyContext": "SliceStrategyContext", "MovementStrategyContext": "SliceMovementStrategyContext",
         "AssistStrategyContext": "SliceAssistStrategyContext", "ActionContext": "SliceActionContext",
         "ChatActionContext": "SliceChatActionContext", "WorldPacketActionContext": "SliceWorldPacketActionContext",
         "TriggerContext": "SliceTriggerContext", "ChatTriggerContext": "SliceChatTriggerContext",
         "WorldPacketTriggerContext": "SliceWorldPacketTriggerContext",
         "ValueContext": "SliceValueContext"}


# Headers nothing includes (forward-ported from the donor module, never wired in): the class they define is the same name as a live one in
# another header, so the class finder must not see them. strategy/triggers/WaitForAttackTriggers.h repeats WaitForAttackSafeDistanceTrigger
# of RangeTriggers.h (grep over ai/: no #include of it; slice 3).
DONOR_ONLY_HEADERS = {"strategy/triggers/WaitForAttackTriggers.h"}


def find_class_headers():
    """class name -> list of headers (paths relative to ai/playerbot) that define it."""
    defs = {}
    pat = re.compile(r'^\s*(?:class|struct)\s+(\w+)\b(?![^{;]*;)[^;{]*\{|^\s*(?:class|struct)\s+(\w+)\s*(?:final\s*)?:[^;{]*$', re.M)
    base = os.path.join(ROOT, "ai", "playerbot")
    for d, _, names in os.walk(base):
        for n in names:
            if not n.endswith(".h"):
                continue
            p = os.path.join(d, n)
            if os.path.relpath(p, base).replace(os.sep, "/") in DONOR_ONLY_HEADERS:
                continue
            text = chk.strip_comments(chk.read(p))
            for m in re.finditer(r'^[ \t]*(?:class|struct)\s+(\w+)\b([^;{]*)(\{|$)', text, re.M):
                if m.group(3) == "{" or ":" in m.group(2):
                    defs.setdefault(m.group(1), set()).add(os.path.relpath(p, base))
    return defs


def classes_of(expr):
    return re.findall(r'\bnew\s+(\w+)', expr)


def main(argv):
    full = chk.full_registry()
    entries = {k: [] for k in KIND_ORDER}
    for kind in KIND_ORDER:
        for key in KEYS[kind]:
            e = full[kind].get(key)
            if e is None:
                sys.exit("slice_gen_context: %s key '%s' is not a key of the full build" % (kind, key))
            entries[kind].append(e)

    defs = find_class_headers()
    headers, problems, used = set(), [], []
    for kind in KIND_ORDER:
        for e in entries[kind]:
            for c in classes_of(e["expr"]):
                used.append((c, kind, e["key"]))
                hs = defs.get(c, set())
                if not hs:
                    problems.append("no header defines class %s (%s '%s')" % (c, kind, e["key"]))
                elif len(hs) > 1:
                    problems.append("class %s is defined in several headers: %s" % (c, ", ".join(sorted(hs))))
                else:
                    headers.add(next(iter(hs)))
    if problems:
        print("\n".join(problems))
        sys.exit("slice_gen_context: cannot place every class")

    # group the entries by their full-build context class, keeping each class's flags
    groups = {}
    order = []
    for kind in KIND_ORDER:
        for e in entries[kind]:
            if e["cls"] not in groups:
                groups[e["cls"]] = (kind, e["flags"], [])
                order.append(e["cls"])
            groups[e["cls"]][2].append(e)
    for cls in order:
        if cls not in LOCAL:
            sys.exit("slice_gen_context: unknown full-build context class %s" % cls)
    # registration order of the full build (AiObjectContext.cpp:21-35): StrategyContext, Movement, Assist, ..., then the rest
    order.sort(key=lambda c: list(LOCAL).index(c))

    L = []
    w = L.append
    w("// GENERATED by forever/tools/slice_gen_context.py: do not edit by hand, change the key lists there and rerun it.")
    w("// forever/tools/slice_registry_check.py proves every entry below is a key of the full build with the same creator.")
    w("//")
    w("// Slice 1 (FOREVER-SLICE1-SPEC section 4): the AiObjectContext constructor of the slice build. The full build's")
    w("// constructor (strategy/AiObjectContextRegistry.cpp, strategy/AiObjectContext.cpp before the R1 split) adds ~1,800")
    w("// creators from eight context headers; the slice registers only the keys the warrior's follow / assist / melee")
    w("// kit uses. Each creator is the text of the full header's, inside a context class of the same shape. The warrior")
    w("// entries are not here: WarriorAiObjectContext (compiled as it is) adds them on top of this constructor.")
    w("#include \"compat/botpch_forever.h\"")
    w("#include \"playerbot/playerbot.h\"")
    w("#include \"SliceLog.h\"")
    w("#include \"playerbot/strategy/AiObjectContext.h\"")
    w("#include \"playerbot/strategy/NamedObjectContext.h\"")
    for h in sorted(headers):
        w("#include \"playerbot/%s\"" % h.replace(os.sep, "/"))
    w("")
    w("using namespace ai;")
    w("")
    w("namespace ai")
    w("{")
    w("namespace slice")
    w("{")
    for cls in order:
        kind, flags, es = groups[cls]
        t = BASE_T[kind]
        w("    // the registry of %s (%d of its keys)" % (cls, len(es)))
        w("    class %s : public NamedObjectContext<%s>" % (LOCAL[cls], t))
        w("    {")
        w("    public:")
        init = " : NamedObjectContext<%s>(%s, %s)" % (t, flags[0], flags[1]) if flags != ("false", "false") else ""
        w("        %s()%s" % (LOCAL[cls], init))
        w("        {")
        for e in es:
            w("            creators[\"%s\"] = %s;" % (e["key"], e["expr"]))
        w("        }")
        w("    };")
        w("")
    w("    // Counted when the constructor runs; SliceRegisteredCounts() reports them for the one-line boot log.")
    w("    constexpr unsigned kStrategies = %d, kActions = %d, kTriggers = %d, kValues = %d;"
      % tuple(len(entries[k]) for k in KIND_ORDER))
    w("}")
    w("}")
    w("")
    w("// The slice's constructor. Same signature as the full build's: only the registry differs.")
    w("AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)")
    w("{")
    for kind in KIND_ORDER:
        member = {"strategy": "strategyContexts", "action": "actionContexts", "trigger": "triggerContexts",
                  "value": "valueContexts"}[kind]
        for cls in order:
            if groups[cls][0] == kind:
                w("    %s.Add(new slice::%s());" % (member, LOCAL[cls]))
    w("")
    w("    static bool logged = false;")
    w("    if (!logged)")
    w("    {")
    w("        logged = true;")
    w("        slice::LogInfo(\"slice: registered \" + std::to_string(slice::kStrategies) + \"/\" + std::to_string(slice::kActions) + \"/\"")
    w("            + std::to_string(slice::kTriggers) + \"/\" + std::to_string(slice::kValues) + \" (strategies/actions/triggers/values)\");")
    w("    }")
    w("}")
    text = "\n".join(L) + "\n"

    if "--report" in argv:
        inslice = set(l.strip() for l in chk.read(chk.SLICE_LIST).splitlines() if l.strip() and not l.startswith("#"))
        cpp_defs = {}
        for d, _, names in os.walk(os.path.join(ROOT, "ai")):
            for n in names:
                if n.endswith(".cpp"):
                    p = os.path.join(d, n)
                    cpp_defs[os.path.relpath(p, ROOT)] = chk.strip_comments(chk.read(p))
        print("registered classes with out-of-line members in a .cpp that is NOT in forever/SLICE1.txt:")
        seen, nout = set(), 0
        for c, kind, key in used:
            if c in seen:
                continue
            seen.add(c)
            where = [f for f, t in cpp_defs.items() if re.search(r'\b%s::~?\w+\s*\(' % re.escape(c), t)]
            miss = [f for f in where if f not in inslice]
            if where and not any(f in inslice for f in where):
                print("  %s (%s '%s') defined in %s" % (c, kind, key, ", ".join(where)))
                nout += 1
        print("  (%d classes checked, %d with no defining .cpp in the slice list)" % (len(seen), nout))
    if "--check" in argv:
        cur = chk.read(OUT) if os.path.exists(OUT) else ""
        if cur != text:
            print("SliceAiObjectContext.cpp is out of date: rerun forever/tools/slice_gen_context.py")
            return 1
        print("SliceAiObjectContext.cpp is up to date")
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s: %s" % (os.path.relpath(OUT, ROOT),
                            ", ".join("%d %s" % (len(entries[k]), {"strategy": "strategies"}.get(k, k + "s")) for k in KIND_ORDER)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
