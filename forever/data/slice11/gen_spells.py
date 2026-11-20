#!/usr/bin/env python3
"""Slice 11, package D11: the hunter and warlock spell lists 1-22 (hunter-spells-1-22.csv, warlock-spells-1-22.csv).

    python3 gen_spells.py <dir with trainer.tsv> <out dir>

hotfix-names.tsv (optional) is `select ID, Name from hotfixes.spell_name where ID in (...)`. trainer.tsv is a SELECT on the world database of the forever test server (read only, TDB 2026-10-14): `select TrainerId,SpellId,ReqLevel,MoneyCost from trainer_spell
where TrainerId in (20,14,40) and ReqLevel<=22` (20 = the hunter trainers, 14 = the warlock trainers, 40 = the hunter trainer that no creature spawns from, the only row of Tame
Beast 1515). The other rows are the starting spells (`playercreateinfo_action` of dwarf hunter 3/3 and gnome warlock 7/9) and the pet-class spells FOREVER-SLICE11-PETS-SPEC
section 6 lists outside a trainer (quest-given and `spell_learn_spell`-given), each with its source. Names are the collaborator's table forever/data/waterys/forever-spell-ids.csv
(tb name, all rank ids); an id it does not name carries "(name: .tb spells)": the live `.tb spells <bot>` (gate a) prints the core's own name. No rank is written: the
table does not order the ranks. Format of forever/data/waterys/rfc/mage-spells-1-18.csv.
"""
import csv, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
IDS = os.path.join(HERE, "..", "waterys", "forever-spell-ids.csv")
BUILD = "client DB2 70938 (2026-11-17); world TDB 2026-10-14"

STARTING = {   # class -> [(spell id, how)]
    "hunter": [(75, "playercreateinfo_action dwarf hunter"), (2973, "playercreateinfo_action dwarf hunter")],
    "warlock": [(686, "playercreateinfo_action dwarf/gnome warlock"), (687, "playercreateinfo_action gnome warlock")],
}
# Not on a spawned trainer (spec section 6): (spell id, level, how, who)
OUTSIDE = {
    "hunter": [
        (1515, 13, "trainer", "trainer 40 (0 spawns, @13); quests 6082/6085/6088/6102/94013/94864 'Taming the Beast' @10"),
        (982, 10, "spell_learn_spell", "learned with 883 Call Pet or 1515 Tame Beast (Revive Pet)"),
        (2641, 10, "spell_learn_spell", "learned with 883 or 1515 (Dismiss Pet)"),
        (5149, 10, "spell_learn_spell", "learned with 883 or 1515 (Beast Training)"),
        (6991, 10, "spell_learn_spell", "learned with 883 or 1515 (Feed Pet)"),
    ],
    "warlock": [
        (688, 3, "quest", "Summon Imp: quests 1598/1599/1470/1485 @3-4"),
        (697, 10, "quest", "Summon Voidwalker: 'The Binding' 1471/1504/1689 @10-11, 9619"),
    ],
}
TRAINER = {"hunter": (20, 40), "warlock": (14,)}


def names(src):
    by_id = {}
    # hotfixes.spell_name rows (SELECT ID, Name ... for the ids below): a name the core's hotfix table holds beats the collaborator's table (U3: 980 and 1014 are renamed there).
    hf = os.path.join(src, "hotfix-names.tsv")
    hotfix = {}
    if os.path.exists(hf):
        for r in csv.reader(open(hf), delimiter="\t"):
            if len(r) >= 2 and r[0].isdigit():
                hotfix[int(r[0])] = r[1]
    for r in csv.DictReader(open(IDS, newline="")):
        if r["class"] not in ("hunter", "warlock"):
            continue
        for sid in (r["forever_all_rank_ids"] or "").split(";"):
            if sid.isdigit():
                by_id.setdefault(int(sid), set()).add(r["tb_spell_name"])
    for sid, n in hotfix.items():
        by_id[sid] = {n + " (hotfixes.spell_name)"}
    return by_id


def main():
    src, out = sys.argv[1], sys.argv[2]
    nm = names(src)
    trainer = [r for r in csv.reader(open(os.path.join(src, "trainer.tsv")), delimiter="\t")]
    for cls in ("hunter", "warlock"):
        rows, seen = [], set()
        def add(sid, level, how, who):
            if sid in seen:
                return
            seen.add(sid)
            n = "; ".join(sorted(nm.get(sid, []))) or "(name: .tb spells)"
            rows.append([sid, n, "", level, how, who, "%s; %s" % (how, "forever-spell-ids.csv" if sid in nm else "no name in the collaborator's table"), BUILD])
        for sid, how in STARTING[cls]:
            add(sid, 1, "starting", how)
        for tid, sid, lvl, cost in ((int(a), int(b), int(c), int(d)) for a, b, c, d in trainer):
            if tid in TRAINER[cls] and lvl <= 22 and not (tid == 40 and sid != 1515):
                add(sid, lvl, "trainer", "trainer %d (%d copper)" % (tid, cost))
        for sid, lvl, how, who in OUTSIDE[cls]:
            add(sid, lvl, how, who)
        rows.sort(key=lambda r: (int(r[3]), int(r[0])))
        with open(os.path.join(out, "%s-spells-1-22.csv" % cls), "w", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(["spell_id", "name", "rank", "level_learned", "how_learned", "trainer_or_quest", "source", "client_build"])
            w.writerows(rows)


if __name__ == "__main__":
    main()
