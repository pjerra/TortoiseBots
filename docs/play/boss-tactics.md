---
id: play-boss-tactics
title: Boss Tactics
category: play
summary: Boss tactics are not on WoW Forever yet; bots fight with their ordinary class and raid AI and the boss table is unread.
tags: [guide, dungeon, boss, tactics]
relates_to:
  - play-dungeons-and-raids
  - play-dungeon-run-leader
  - play-dungeon-reference
---

# Boss Tactics

Boss tactics are **not on WoW Forever yet.**

The Forever build applies no per-boss rules. Bots fight every boss with their ordinary class and raid AI: no scripted interrupt, kill order, stand seat, tank post or aura handling.

- The Forever data creates an empty `ai_boss_tactic` table with the dungeon-plan tables and ships no rows.
- The stand-seat table `ai_boss_stand` is not created.
- The `boss tactics` strategy is not in the Forever AI registry, and the boss-tactics actions are link stand-ins that answer "no rule applies".
- `AiPlayerbot.DungeonTactics` and the switches that gate boss rows have no effect on WoW Forever.

For what the bots do in dungeons on WoW Forever, see the [dungeon run leader](dungeon-run-leader.md) and the [dungeon reference](dungeon-reference.md).
