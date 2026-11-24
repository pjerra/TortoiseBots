---
id: play-dungeons-and-raids
title: Dungeon & Raid Tactics Guide
category: play
summary: How companion bots fight in ForeverCore instances, including pulling, crowd control, interrupts, hazards, wipe recovery, loot and the GameMaster run leader.
tags: [dungeon, raids, tactics, pulling, cc, wipes, instances, loot]
relates_to:
  - play-commands-party-actions
  - play-dungeon-reference
  - play-known-limitations
  - server-settings-dungeons-raids
---

# Dungeon & Raid Tactics Guide

A bot party on **ForeverCore** fights with the same class AI it uses in the open world: it assists, interrupts, crowd-controls, loots and follows. This page covers what works inside instances, the orders that help, and the parts that are not built yet.

## Before you go in

- The server must be built with the bot engine (`TORTOISEBOTS_SLICE1=ON`) and the module enabled.
- Your party is made of your own characters (`.bot add <Name>`, up to four) and, if the server runs a pool, free bots that joined you.
- Bring a tank and a healer. A bot picks its role from its talents; a protection warrior or bear druid tanks, a holy/restoration bot heals.
- Everything in the instance, including the loot rules below, applies to bots in your group. A free bot that is not grouped with you is not yours to order.

## Pulling

| You want | Order | What happens |
| :--- | :--- | :--- |
| Open a pull | `pull` (or `.bot attack <Name>` on your target) | The tank attacks. With `AiPlayerbot.RangedPull = 1` it opens with a ranged weapon before closing in. |
| Pull around a corner | `pullback` (addon action) | The tank pulls from range and runs back to your spot; the party waits for it. |
| Abandon a pull | `end pull` | The tank drops the pull and returns. |
| Give the pull time | `pull 5`, `pull ranged`, `pull body` | Sets the opener or a wait in seconds (up to 60). |

The pullback waits are `AiPlayerbot.PullBackMaxReturnTime` (15 seconds for the tank to come back) and `AiPlayerbot.PullBackDpsJoinDelay` (5 seconds before damage dealers join). If the pack does not arrive, the pull ends instead of holding the party forever.

## Crowd control

- Bots use their own class control: Polymorph, Sap, Shackle Undead, Banish, Hibernate, Freezing Trap and fear effects. A bot keeps damage off a unit it is holding.
- `cc clear` clears the bot's mark. Assigning a mark from the party (`cc <mark>`) is **not on ForeverCore yet**; a GameMaster can set one with `.tb cc <bot> <mark> <unit>`.
- Area spells are not the main danger they are in the Turtle build: the engine checks its area targets, but it does not have the full Turtle CC-protection rules. Call your AoE off with the `aoe` behaviour toggle when a pull depends on a sheep or a sap.
- Pets follow their owner's target. A hunter's pet and a warlock's demon are part of the fight.

## Interrupts and dispels

The addon's `interrupt` action picks one party member that can interrupt the current cast and sends it. The engine also interrupts on its own when a dangerous cast is in progress (`AiPlayerbot.InterruptIgnoresThreat`, `AiPlayerbot.InterruptAllowStun`). Healers and hybrid bots dispel what their class can dispel.

## Hazards

Bots step out of harmful ground effects and area auras when `AiPlayerbot.HazardEvasionDistance` (12 yards) and `AiPlayerbot.AvoidCreatureAuras` (on) apply. This is a general reaction, not per-boss scripting: a boss mechanic that needs a specific movement (a kite, a jump, a lineup) is still your call.

## Following and catching up

- Followers that fall far behind mount up when `AiPlayerbot.BoostFollow = 1` (on by default).
- A follower that is far behind does **not** stop before a skipped pack: `AiPlayerbot.DungeonFollowHoldBeforePacks = 0`. Clear the pack or walk back for the stragglers.
- In a narrow corridor, let the party bunch up before the next pull; the engine has no special anti-aggro hold.

## Dead bots {#dead-bots}

- A dead bot can be revived by a player, a healer or a spirit healer. `.bot summon` refuses a dead bot.
- A **commanded** bot whose body lies in a dungeon with nothing raising it releases after `TortoiseBots.Slice.DeadBackstopSeconds` (180 seconds) and comes back at the instance entrance.
- A GameMaster can raise a bot directly with `.tb revive`.
- Free bots (not commanded by you) handle their own death: they release and corpse-run.

## Loot {#loot}

- Bots in your group take part in the group's loot rolls. They roll for gear they can use; a bot does not Need an item just to fill a bag.
- `AiPlayerbot.RollBadItemsWithPlayer = 0`: bots do not roll Need on items that only fill an empty gear slot while a player is in the group.
- A GameMaster can inspect a bot's bags, equipment and recent rolls with `.tb gear`, `.tb equip` and `.tb lootlog`.
- Mages and warlocks can conjure food, water and healthstones. `.bot service <bot>` reports what a bot knows; `.bot service <bot> conjure <food|water|healthstone>` orders it. The addon's trade verb moves items between you and a bot.

## The run leader (GameMaster) {#run-leader}

The run leader walks a stored dungeon plan for a bound party. It is a **server-admin tool**, not a player command:

```text
.tb master party <LeadBot> <Commander>
.tb plan list
.tb run start <LeadBot> <plan>
.tb run status <LeadBot>
.tb run pause|resume|stop <LeadBot>
```

- The lead bot must be in your party and bound to a commander first. A player-led start (`lead=player`) is opt-in and still experimental.
- Plans live in the world database (`ai_dungeon_plan`, `ai_dungeon_step_unit`). `.tb plan <plan> check <character>` checks that every unit of a plan can be found in a fresh instance.
- The run pulls, fights, loots, opens doors and regroups. It is work in progress: expect a plan to get stuck at a step, and use `.tb run status` and `.tb run events` to see where.

See [Dungeon run leader](dungeon-run-leader.md) for the full command set and route fields.

## Raids

- Generic raid behaviour runs: assist, tank assignment, healing, dispels, hazard avoidance and loot.
- **Boss-specific tactics are not on ForeverCore yet.** The old per-boss rules (Molten Core, Blackwing Lair, Naxxramas, Ahn'Qiraj, the Turtle custom raids) are not loaded, and `AiPlayerbot.RaidReactions` is off. Do not expect a bot to run out with a bomb, turn a dragon, or take a Patchwerk role on its own.

## Turtle content

The [dungeon reference](dungeon-reference.md) is written for ForeverCore and lists the Forever plans and creature entries. Turtle raid guides and Turtle custom raids do not apply to ForeverCore.
