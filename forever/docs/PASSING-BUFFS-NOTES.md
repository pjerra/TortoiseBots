# Passing buffs on ForeverCore (#158, package P2)

`runtime/PassingBuffsService.cpp` is now on the slice file list (`forever/SLICE-W4.txt`) and its sweep runs from the host
world tick. Three reads that named Turtle-only types were replaced with the both-core helpers; the decision code
(`ai/playerbot/PassingBuffsPolicy.h`) and the service's behaviour are unchanged. `AiPlayerbot.PassingBuffs` stays on by
default, as on Turtle.

## The three swaps

| Site | Turtle (unchanged meaning) | ForeverCore |
|---|---|---|
| which bots may buff | `BotManager::IsRandomBot(guid)` (the random-bot record) | `sRandomBotFacade.IsFreeBot(bot)` (`forever/src/host/TbHostBotManager.cpp:128`): the host's free flag, cleared when a master is bound (`forever/src/TbBotMgr.cpp:481-496`); no random-bot record exists on this host |
| hidden GM | `GetVisibility() == VISIBILITY_OFF` (`ai/cmangos-compat-unported.h:191`) | `PlayerHiddenGmCompat(player)` (`forever/src/compat/TbCompat_Unported.h:351`: `!isGMVisible()`); the `IsGMVisible` test above it stays |
| the grid search | `MaNGOS::AnyFriendlyUnitInObjectRangeCheck` + `MaNGOS::UnitListSearcher` | `VisitFriendlyUnitsCompat(player, nearby, range)` (`forever/src/compat/TbCompat_V.h:85-91`): Trinity's searcher and check, asked not to add combat reaches, so the plain sight range is what Turtle uses |

The sweep still runs every 2.5 s over the online network sessions only, casts at most 8 times per sweep, and never touches a
bot target (a headless session is rejected). It remains outside every bot's tick.

## Build and tick

- `runtime/PassingBuffsService.cpp` is a legacy-listed file, so the slice build compiles it with `compat/botpch_forever.h`
  in front of it (`CMakeLists.txt:128-134`).
- The call sits after `BotMgr::Instance().Update(diff)` in `tortoisebots_world::OnBeforeMapsUpdate`
  (`forever/src/TbScripts.cpp:344-355`), inside the `TORTOISEBOTS_SLICE1` block: the service exists only in the slice build,
  exactly like the rest of the engine.

## Known, deliberately not changed

`TryHeal` does not roll the 1-in-2 chance the docs promise (`docs/world/social.md:33`); only `TryBuff` rolls. That is a
Turtle-side bug and this port must not change Turtle behaviour, so it stays. The docs sentence is therefore already wrong
before and after this change.

## What the lead must check live

A real client is needed: the target must be a network session, and the buff roll is 1 in 5 per new meeting.

1. Player logs in; note map and coordinates.
2. `.tb login <priest> free`, `.tb engine <priest> on`. The engine must be on: the sweep needs the bot's AI object.
3. `.tb spells <priest>` lists `Power Word: Fortitude`; otherwise `.tb level <priest> 60` and `.tb learn <priest> <rank id>`.
4. Bring them together: `.tb tele <priest> <PlayerName>` from the player's own session, or
   `.tb tele <priest> <map> <x> <y> <z>` from the console, or `.tb order <priest> come <PlayerName>`.
5. Wait ~5 s, then repeat an away/back cycle up to 10 times (teleport the bot out of sight and back): each new meeting rolls the
   1-in-5 buff chance, and the pair loses its cooldown on a sweep that does not see it.
6. Pass: `TortoiseBots: passing buff <priest> -> <Player> (power word: fortitude)` in the server log and the aura on the
   player. A player below 35% health additionally gets `passing heal ... (renew)`, which is deterministic and proves the sweep
   is running before the buff roll lands.
7. Control: a bot next to a bot must never fire (the target is a bot); `.tb where <priest>` shows it did not stop or turn.
