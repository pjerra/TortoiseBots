# Hunter and warlock first pets on Forever

Data: TDB 2026-10-14 quest/creature tables + client DB2 70959 (`SpellName`, `SkillLineAbility`),
plus Audrey's in-game check on Nov 21 (repack 0.1.37.4, client 71037).

## Hunter — first pet at level 10

- Spells: **Tame Beast 1515**, **Call Pet 883**.
- The class quest line is **"Taming the Beast"** (classic quests 6061–6102; one per race, level 10).
  The turn-in grants Tame Beast; the hunter then uses the supplied Taming Rod on the named beast and
  turns the tamed beast in. Call Pet brings it out.
- Classic giver examples (all spawned in TDB):
  - 6082 Thotar, entry 3171, map 1 `275.341 -4704 11.712` (Orc/Troll)
  - 6085 Grif Wildheart, entry 1231, map 0 `-5618.54 -454.071 407.842` (Dwarf)
  - 6088 Yaw Sharpmane, entry 3065, map 1 `-2180.22 -408.816 -4.518` (Tauren)
  - 6102 Dazalar, entry 3601, map 1 `9812.33 928.806 1308.19` (Night Elf)
- Forever also has custom **94013 "Taming the Beast"**: giver **Quel'ana Quickgale 252389**
  (map 2991, `2262.72 917.641 675.346`), `StartItem 264163`, `PrevQuestID 94979`,
  `RewardSpell 1579` (Tame Beast) / display 1515. Use this for Zephras Isle characters.
- Flags: `trainer_spell` has Tame Beast 1515 at **ReqLevel 13**, while the class quests are level 10;
  **Call Pet 883 has no trainer_spell or quest-reward row**. Check a fresh hunter: if 883 is not on the
  bar/spellbook after the Taming quest, that is a data gap.
- Test: `.go xyz 275.341 -4704 11.712 1` (Thotar) or the matching race giver; `.quest add 6082` (or the
  race's quest); `.additem 15920` if the Taming Rod is missing; complete and check Tame Beast/Call Pet.
  For Zephras: `.go xyz 2262.72 917.641 675.346 2991`, `.quest add 94013`.

## Warlock — Imp and Voidwalker

- **Imp 688** is the first pet, but the data has **no quest reward, no trainer_spell row, and no
  playercreateinfo_action entry** (starting actions for class 9 are Shadow Bolt 686 and Demon Skin 687).
  `SkillLineAbility` 6391 classifies 688 as Learned (AcquireMethod 0), so a trainer row should exist;
  it does not. This looks **missing** unless the core grants 688 as a hard-coded default.
- **Voidwalker 697** is the level-10 class quest **"The Binding"**:
  - 1471: Carendin Halgar, entry 5675, map 0 `1711.59 57.167 -62.206` (Undercity)
  - 1504: Gan'rul Bloodeye, entry 5875, map 1 `1850.31 -4357.3 -14.798` (Orgrimmar)
  - 1689: Gakin the Darkbinder, entry 6122, map 0 `-8983.33 1041.52 101.487` (Stormwind)
  - `RewardSpell 11520` = Teach Summon Voidwalker, display **697**; `StartItem` 6284/7464/6928;
    `PrevQuestID` 1473/1501/1688. All three givers are spawned in TDB.
- Later ranks are data-present: 1474/1513/1739 (level 20 Succubus 712), 1795 (level 30 Felhunter 691).
- Test: `.go xyz 1711.59 57.167 -62.206 0` (Carendin), `.quest add 1471`, `.additem 6284`,
  `.quest complete 1471`; check Summon Voidwalker 697. For Imp: `.learn 688` and `.cast 688` on a fresh
  warlock; if 688 is absent from the spellbook/trainer, it is the missing data flagged above.

## In-game check (Nov 21, repack 0.1.37.4, client 71037)

- **Hunter: fully works.** Tame Beast and Call Pet are present and usable; the Taming quest completed,
  the beast was tamed, dismissed and called back with no issue.
- **Warlock Imp 688: confirmed MISSING.** A new level-1 warlock has no Summon Imp in the spellbook,
  matching the data gap (no quest/trainer/action entry). Classic expects it at level 1.
- **Warlock Voidwalker: works.** Level-10 quest 1471 via `.quest add`/`.quest complete` + turn-in taught
  Summon Voidwalker; it was summoned with a Soul Shard (`.additem 6265`).
- So the only first-pet blocker is the missing Imp 688; hunter and Voidwalker are verified.
