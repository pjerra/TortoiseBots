# Races and class combos (Forever vs TB)

Core wins. Source: world DB `playercreateinfo` rows in `TDB_full_forever_2026_10_14.7z`.

## Forever's allowed race/class pairs (playercreateinfo)

Race ids: 1 Human, 2 Orc, 3 Dwarf, 4 NightElf, 5 Undead, 6 Tauren, 7 Gnome,
8 Troll, 9 Goblin, 10 BloodElf, 11 Draenei (+ 22, 24-32, 34-37, 52, 70, 84-86, 91, 95, 96 = modern allied races).

- Human (1): Warrior, Paladin, Hunter, Rogue, Priest, DK, Mage, Warlock, Monk
- Orc (2): Warrior, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk
- Dwarf (3): Warrior, Paladin, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk
- NightElf (4): Warrior, Hunter, Rogue, Priest, DK, Mage, Warlock, Monk, Druid, DH
- Undead (5): Warrior, Paladin, Hunter, Rogue, Priest, DK, Mage, Warlock, Monk
- Tauren (6): Warrior, Paladin, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk, Druid
- Gnome (7): Warrior, Hunter, Rogue, Priest, DK, Mage, Warlock, Monk
- Troll (8): Warrior, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk, Druid
- Goblin (9): Warrior, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk
- BloodElf (10): Warrior, Paladin, Hunter, Rogue, Priest, DK, Mage, Warlock, Monk, DH
- Draenei (11): Warrior, Paladin, Hunter, Rogue, Priest, Shaman, DK, Mage, Warlock, Monk

No High Elf (race 10 on Turtle = BloodElf on Forever). Goblin exists on both (race 9).

## What TB does with High Elf and Goblin today (switch off / rewire)

| File:line | What | Action |
|---|---|---|
| ai/playerbot/TravelNode.cpp:2470-2471 | startNames[RACE_GOBLIN/HIGH_ELF] | race 9 = Goblin on both, keep; race 10 label "High Elf" is WRONG on Forever (10 = BloodElf): rewire to BloodElf or drop |
| ai/playerbot/strategy/actions/ReleaseSpiritAction.h:295 | homebind override for GOBLIN/HIGH_ELF (custom player-only start zones) | Turtle-only spawn setup; switch off on Forever (both races have normal starts) |
| runtime/BotManager.cpp:461 | same homebind override, second site | same: switch off |
| runtime/HireProvisionService.cpp:513,617 | hire race pool + class mask loops `RACE_HUMAN..RACE_HIGH_ELF` (1..10) | on Forever 1..10 = Human..BloodElf (no High Elf, plus Goblin/BloodElf included): keep range but the "High Elf" assumption is gone; verify BloodElf/Goblin hire gear sets exist |
| commands/Hire.cpp:295 | hire race parse range HUMAN..HIGH_ELF | same range issue; rewire label 10 |
| host/HireRecruiterScript.cpp:117-118 | race names Goblin/High Elf | rewire 10 to BloodElf |
| policy/travel/MountShopPolicy.h:68-182 | Goblin mounts (80137/80961 vendors, 80460-62/81190-91 items), High Elf = Human horses (case 10) | Goblin rows: verify Forever vendors/items; High Elf case 10 now means BloodElf (hawkstriders, not horses): rewire |
| runtime/AhBotSellerAccount.cpp:40 | static_assert GOBLIN==9 HIGH_ELF==10 | will FAIL on Forever (HIGH_ELF undefined / 10 = BloodElf): remove or rewire |
| ai/playerbot/PlayerbotAIConfig.cpp:1231, PlayerbotAI.cpp:4681, ChatHelper.cpp:195 | comments: Goblin/High Elf rows | comments only; update |
| ai/playerbot/PlayerbotAIConfig.h:423 | comment: goblins to Valley of Trials | comment only; verify |

Goblin (9) is safe to keep everywhere; every `RACE_HIGH_ELF` (10) reference must be rewired to BloodElf or removed. No `RACE_BLOODELF` (also 10 on Trinity) handling exists in TB - add it where race 10 is matched.
