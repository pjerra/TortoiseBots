# Mounts, hunter pets, warlock pets (Forever)

Sources: TB `MountShopPolicy.h` vendor/item lists; world DB `npc_vendor` + `creature_template` (TDB 2026-10-14);
DB2 `Item.db2` 70888. Files: `mounts.csv`, `hunter-pets.csv`, `warlock-pets.csv`.

## Riding skill

TB rule (Turtle): speed scales purely with riding skill - 75 (level 40) -> 60 % mounts,
150 (level 60) -> 100 % mounts. Turtle quest mount (spell 30174, item 100038?) excluded from shops.
Forever: riding is skill 762 in the DB2s; the exported trainer rows for it are custom staging
entries (TrainerId 1003xxx). Whether Forever gates mounts at vanilla 75/150 could not be verified
from the DBs alone - verify on live with a level-40 bot at a racial vendor.

## Mounts

Classic racial vendors + items all present on Forever (50/56 rows). Missing (6, all Turtle customs):
Goblin Rocket Cars 80460-80462 + Shredders 81190-81191 (items absent from Item.db2+armory),
Turtle riding spell 30174 (not a shop mount anyway). Goblin vendors 80137/80961 exist as
creature_template rows but have no npc_vendor rows - Goblin bots have no vendor mounts on Forever.
Human/High Elf row covers Humans; race 10 on Forever is Blood Elf (hawkstriders, not in TB's list -
add before port; see races.md).

## Hunter pets

Tameable families from `creature_template.family` x `creature_classic_level` (77 sample rows:
family, entry, level range, 10-level bracket). Covered families: Wolf, Cat, Spider, Bear, Boar,
Carrion Bird, Crab, Gorilla, Raptor, Moth. Only 10 of ~46 families have classic-level rows, so this
is a starter list, not a full tameable table - extend from live Tame Beast results.
Taming itself is spell 1515 (OK, in SpellName.db2 + trainer data).

## Warlock pets

Imp 688 / Voidwalker 697 / Succubus 712 / Felhunter 691 - all present (OwnerPicks + APL + DB2).
Infernal (1122) and Doomguard rituals resolve at runtime like other crafts; not enumerated here.
