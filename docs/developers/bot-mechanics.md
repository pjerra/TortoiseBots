---
id: developers-bot-mechanics
title: Deep Bot Mechanics, Quirks & Capabilities
category: developers
summary: Target selection, interrupt and crowd-control rules, movement, upkeep, and the guards that are active in the Forever engine build.
tags: [mechanics, quirks, targeting, threat, interrupts, movement, ai]
relates_to:
  - developers-strategy-engine
  - developers-architecture
---

# Deep Bot Mechanics, Quirks & Capabilities

This page describes the engine that the `TORTOISEBOTS_SLICE1` build compiles. A value, trigger or action that the slice registry does not list never runs, however complete its class looks in `ai/playerbot`; see [Strategy Engine & Action Scheduling](strategy-engine.md).

Pure rules live in policy headers next to the values that call them. The value file translates game objects to plain facts; the policy decides. That split is why a rule can be read and tested without a server.

## 1. Target selection

| Intent | Registered key(s) | Rule owner | Main rule |
| :--- | :--- | :--- | :--- |
| Tank | `tank target` | `TankTargetValue.cpp`, `TankAddPickupPolicy.h`, `MainTankPolicy.h` | Bucketed order: loose mobs the tank holds nothing on first, then held mobs in reach, then held mobs out of reach. An explicit main tank with an off-tank beside it holds its mob; off-tanks peel to loose adds. |
| DPS, single target | `dps target` | `DpsTargetValue.cpp`, `policy/combat/DpsTargetPolicy.h`, `policy/combat/RunnerFinishPolicy.h` | Explicit attack-order target first; then a raid mark; runners below 30% health; then the group tank's target; then the lowest-health non-CC attacker. |
| DPS, area | `dps aoe target` | `DpsTargetValue.h` | Highest-health enemy, so damage-over-time and cleaves run their full duration. |
| Crowd control | `cc target`, `rti cc target`, `rti cc` | `CcTargetValue.cpp`, `policy/combat/CcMarkHoldPolicy.h` has the mark-hold rule; `AutoCcStrategy` the automatic pick | One mark has one owner. The assigned mark wins over any free pick. Automatic CC takes loose adds on healers, skips targets with periodic damage, the last mob, the skull and the tank's target, and does not re-sheep. |
| Grind destination | `travel target`, `grind target` | `GrindSpotPolicy.h`, `GrindTargetValue.cpp` | A destination is level-appropriate: below level 10 the creature window is `[bot-2, bot+1]`, from 10 it is `[bot-2, bot+2]`. A spot is skipped once as many bots hold it as it has room for. |
| Grind mob | `grind target` | `GrindTargetValue.cpp`, `PullRegenPolicy.h` | `attack anything` picks the mob (solo grind only). A masterless bot below 10 takes mobs at most 1 level above it; from 10 the ceiling is +4. A wounded masterless pool bot starts no new pull below `AiPlayerbot.MediumHealth` (70) or `AiPlayerbot.MediumMana` (40). |
| Attack arming | `attack` | `AttackAction.cpp` | `Player::Attack(target, meleeAttack)` arms melee. A bot designated ranged with no usable ranged weapon is armed for melee so it swings once the mob closes. |
| Dispel / decurse | `party member to dispel` | `PartyMemberToDispel.cpp`, `policy/heal/DispelTargetPolicy.h` | Spreaders first, then players/bots, then combat pets; explicit boss priorities stay above spreading. Each bot claims a pick per dispel type for a few seconds so several decursers clear different targets. |
| Enemy player | `enemy player target`, `enemy player targets`, `enemy player near` | `EnemyPlayerValue.cpp`, `WorldPvpSeekChecks.cpp` | Duel opponent and players attacking the bot come first, then proximity. Open-world seeking needs a PvP-flagged target and stands down near a graveyard (80 yd, either faction). |
| Enemy healer | `enemy healer target` | `EnemyHealerTargetValue.cpp` | Surfaces an enemy casting an interruptible heal as the interrupt/focus target. |

**Helping a party member in a fight.** A grouped bot fights what attacks a living member of its group within `AiPlayerbot.SightDistance` (default 60 yd), not only what attacks itself: `AttackersValue::AddTargetsOf(Group*)` adds each member's attackers, current and last target, and pet's attackers, and `IsValid` keeps one only while it fights that member. A bot in `follow`, `stay` or `guard` does not refuse such a target for its distance from its leader.

## 2. Interrupts and crowd control

An interrupt order first checks that the target is casting and picks one bot with a ready interrupt (`TbTactics`); the queued order then runs through that bot's class context, which supplies the spell action. The class contexts are compiled, so the class-keyed actions are real:

| Class | Interrupt action key(s) |
| :--- | :--- |
| Rogue | `kick` |
| Warrior | `pummel`, `shield bash`, `intercept` |
| Shaman | `earth shock interrupt` |
| Mage | `counterspell` |
| Priest | `silence` (shadow) |
| Warlock | spell lock (Felhunter) actions in the warlock context |
| Paladin | `hammer of justice interrupt` |
| Druid | `bash` |

The class actions re-check range and castability on their own turn; a refusal ends the order rather than inventing a fake cast.

**Pets stay off controlled targets.** `PossibleAttackTargetsValue::HasBreakableCC` and `HasUnBreakableCC` strip breakable and unbreakable control from the pet's candidate list. Breakable covers polymorph, frost nova/frozen, sap, gouge, shackle undead and seduce; unbreakable covers stuns, fears and roots. The skull mark remains an explicit override.

## 3. Movement

| Subsystem | Behaviour |
| :--- | :--- |
| Formations | `default`/`near`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `far`, `custom`. Offsets are relative to the master's orientation. |
| Catch-up mount | With `AiPlayerbot.BoostFollow` on, a bot far behind mounts to catch up; it does not mount inside combat. |
| Flee and kite | The `flee` strategy reacts to panic, being outnumbered, and critical health with no master; `kite` is registered too. A bot answering a real player keeps following the player's orders instead of running away on its own. In a dungeon a caster's flee backs off along the party's trail, but never to a point more than 30 yd from the group's tank. |
| Dungeon follow stall | A bot behind the leader inside a dungeon that has not moved a yard for 2 s (beyond follow range + 3 yd, while not eating, casting or rooted) latches a rescue: ledge step-down first, else a module `MoveTo` along the navmesh path, at most once per 2 s, backing off to 8 s, and giving up for a minute after five walks that could not start. Past sight range the same rescue arms at once. `DungeonFollowStallPolicy.h`, `FollowActions.cpp`. |
| Party facing | Tanks holding a mob sidestep so it faces away from the party (`tank face needed`); melee DPS sidestep out of frontal and tail cleaves (`rear flank`). |

The legacy taxi and transport helpers remain unported (`forever/docs/UNPORTED.md`), so the engine's direct ride and fly calls answer "did not happen" rather than pretending to ride. The travel graph still gets bots onto boats and zeppelins and onto taxi flights through the host services (`host/TbHostTransport.cpp`, `host/TbHostTaxi.cpp`, wired in `compat/TbCompat_WL4n.h`). Jumping and falling stay unported.

## 4. Sustenance and gear

| Mechanic | What the Forever build does |
| :--- | :--- |
| Food and water | `host/TbAutoFood` seeds pool and free bots with level-fitting vendor food, and water for mana users, on login and level-up. The engine's `food` and `drink` strategies then eat and drink out of combat; a rest order sets the `rest to full` value. |
| Group combat | A bot does not sit down while group members within sight are fighting; eating and drinking bots stand when combat starts. |
| Conjured sharing | Mages share conjured food and water through `ConjuredTradePolicy.h`, which `TradeStatusAction.cpp` consults. |
| Gear upgrades | `equipment audit` (300 s) plus the `item push result` trigger run `equip upgrades`; a commanded bot self-equips only when `AiPlayerbot.ClaimedBotSelfEquip` (default 1) allows it. Worn gear is never changed in combat. |
| Pool gear top-up | `pool gear top up`, only for a masterless pool bot and only with `AiPlayerbot.PoolGearTopUpOnLevel` on (default 0): one gear pass per level. |
| Vendor trips | `VendorTripPolicy` decides when a bot needs a vendor; `ChooseTravelTargetAction` turns that into a travel destination. |
| Bag pressure | The maintenance strategy has `smart destroy item`, and `bag space` is a registered value; the item-cleaning ladder uses them. |
| Starting kit | Bots are created and dressed by the host (`forever/src/host/pool`, `TbAutogear`, the gear index), not by the legacy factory: `PlayerbotFactory.cpp` is not in the Forever slice. |

## 5. Guards and limits that are active

| Guard | Rule |
| :--- | :--- |
| Ghost rescue | A free bot's ghost that makes no 5-yard headway toward its corpse or the spirit healer for 5 minutes is repop'd once per death by the core. `GhostRescuePolicy.h`, `BotMgr::GhostRescue`. |
| Graveyard peace | An open-world PvP seeker starts no fight while it or its target stands within 80 yd of a spirit healer of either faction; a fight already under way still answers attacks. |
| Stuck rescue | The registered `unstuck`, `hearthstone` and `repop` actions and the `move stuck` / `move long stuck` / `combat stuck` triggers form the rescue chain; `nopath` walks feed it. |
| Health and mana lines | `AiPlayerbot.LowHealth` (50), `AiPlayerbot.MediumHealth` (70) and `AiPlayerbot.MediumMana` (40) are the shipped defaults; many triggers read them instead of hard-coded numbers. |
| Sight | `AiPlayerbot.SightDistance` (60 yd) bounds the party-help scan and several travel and loot checks. |
| Chat gate | A bot whose activity is off ignores all chat; one that is not recording messages accepts orders only. The gate is published per bot for the packet hook and refreshed at most every 30 s. `PlayerbotAI::PublishChatGate`, `runtime/BotChatGate.h`. |

For player-facing consequences and troubleshooting, see [Known Limitations & Quirks](../play/known-limitations.md).
