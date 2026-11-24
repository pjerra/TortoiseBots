---
id: play-boss-tactics
title: Boss Tactics
category: play
summary: Boss tactics are not on ForeverCore yet; bots fight with their ordinary class and raid AI.
tags: [guide, dungeon, boss, tactics]
relates_to:
  - play-dungeons-and-raids
  - play-dungeon-run-leader
  - play-dungeon-reference
---

# Boss Tactics

Boss tactics are **not on ForeverCore yet.**

The Forever build applies no per-boss rules. Bots fight every boss with their ordinary class and raid AI: no scripted interrupt, kill order, stand seat, tank post or aura handling.

- The Forever data creates an empty boss tactics table with the dungeon-plan tables and ships no rows.
- The stand-seat table is not created.
- The `boss tactics` strategy does not exist on ForeverCore; the boss-tactics actions are not part of the build and answer "no rule applies".
- `AiPlayerbot.DungeonTactics` and the settings that gate boss rows have no effect on ForeverCore.

For what the bots do in dungeons on ForeverCore, see the [dungeon run leader](dungeon-run-leader.md) and the [dungeon reference](dungeon-reference.md).

## Kinds

None. No boss tactic kind (interrupt, kill-first, stand seat, tank post and the rest) runs on ForeverCore.
