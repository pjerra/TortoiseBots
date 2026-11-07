---
id: developers-lua-api
title: Lua API (TB.*, Eluna builds only)
category: developers
summary: A read API over the bots and the run, plus one write (TB.Command, queued through the .bot path), bound in the world Lua state only through the core's custom-methods registry.
tags: [lua, eluna, api, scripting, testing, threads]
relates_to:
  - developers-architecture
  - developers-host-api
  - server-run-trace
---

# Lua API

A small Lua API for test and walk scenarios, on a core built with Eluna (`-DBUILD_ELUNA=ON`). It is **not** part of any other build: without `ENABLE_ELUNA` the module compiles the same files to empty shells and no `TB` table exists. It is **off** unless a script uses it, and costs one mutex lock per world tick while nothing is pending.

The API is read-mostly. It returns plain Lua tables (numbers, booleans, strings), never `Player` userdata, so a script cannot hold a game object across a tick. The one write, `TB.Command`, is a `.bot` command: it goes through the same queue and the same handlers as a typed one.

## Calls

All four exist in the **world** Lua state only. In a map state (dungeon, raid or continent state) the global is registered as Eluna's wrong-state closure and calling it raises `attempt to call method 'TBGetBots' that does not exist for state: <mapId>`. A map-state script hands a guid to the world state (a timer reading a shared queue) instead.

Every argument is checked before TB code runs. A bad call never raises a Lua error: it returns `nil, "<code>"`.

| Call | Returns |
| :--- | :--- |
| `TB.GetBots([masterGuid])` | An array of guid counters (the low part of the player guid): every in-world bot, or the live bots of one master. |
| `TB.GetBot(guid)` | A table of the bot's roster row, or `nil` when the guid is not an in-world bot. |
| `TB.GetRun(leaderGuid)` | `{ active = false }`, or the `.bot run` run the player owns. |
| `TB.Command(requesterGuid, text [, callback])` | A command id, or `nil, code`. |

Guids are numbers: whole, 1 to 2^32 - 1 (`bad_guid` otherwise). Lua doubles hold them exactly.

### GetBot fields

The dashboard's roster row, copied: `guid name class role kind level hp maxHp power maxPower powerType map zone instance raidMap group subgroup x y z o target strategy state lastAction travelTo isAlt`. `state` is the macro state (`combat`, `moving`, `busy`, `resting`, `dead`, `idle`, ...), `instance` is 0 in the open world.

### GetRun fields

With a run: `active=true plan state leg kind phase note step steps pass elapsedMs`. `step` is 1-based and `pass` counts revisits of the current step. A paused run is active; a finished one is not. Boss kills and wipes are not fields yet (the run keeps them in its own records, see [run trace](../server/run-trace.md)).

### TB.Command

```lua
local id, err = TB.Command(playerGuid, ".bot co +avoid aoe", function(id, requester, lines, truncated)
    for _, line in ipairs(lines) do print(id, line) end
end)
```

- `text` is what a player would type: `.bot hire x`, `bot hire x`, a bare `hire x`, or `.autogear ...`. Up to 255 characters, no control characters or newlines (`empty`, `too_long`, `bad_char`).
- The requester must be an online player (`no_such_player`). The command runs **as that player**, on the world thread, at the next drain at the start of a world tick (at most one tick later), through TB's own handlers: ownership, master and GM checks are the ones a typed command gets. A script cannot command bots a player could not.
- It is never run inline, even from a callback or a hook, so a command can never run inside the tick that emitted an event.
- The reply lines (at most 40, each cut at 400 characters; `truncated` says so) go to `callback(id, requesterGuid, lines, truncated)` on the world thread, outside every command. Without a callback they go to the server log at detail level. At most 64 callbacks wait at once (`too_many_callbacks`); one that gets no reply within 60 s (the requester logged out) is dropped. A `.reload eluna` drops the callbacks with the state; the reply is then logged.
- The queue's per-requester cap (128 pending) answers `queue_full`.

## Where it lives

| Piece | File |
| :--- | :--- |
| Registration, bindings, callback store | `host/LuaBindings.cpp` |
| Pure rules (arguments, command text, reply buffer, callback ledger, field lists) | `runtime/LuaApiPolicy.h`, tested by `tools/test_lua_api_policy.cpp` |
| Reply route `BotCommandReply::Lua` | `commands/BotCommandQueue.h`, `host/BotChatAdapter.cpp` |
| Roster row reader | `ObservabilityEmitter::SnapshotBot` |
| Run reader | `BotCommands::ReadRunForLua` (Commander.cpp) |
| Core registry | tortoise-wow `src/game/Eluna/ElunaCustomRegistry.h` |
| Wiring test | `tools/test_lua_api_wiring.py` |
| Smoke script | `tools/eluna/test-toolkit/lua_scripts/tt/tt_tbapi.lua` (`.tbapi smoke`) |

The core calls every registrar added to `ElunaCustomRegistry` when it builds a Lua state. TB adds its registrar from a **static initializer** in `LuaBindings.cpp`, not from `RegisterScripts`: the world state is created before `ScriptMgr::Initialize` runs the module's `RegisterScripts`, so a registrar added there would miss the world state.

## Thread rules

- Module and bot AI state is touched on the world thread only ([architecture](architecture.md)). The world Lua state is handed to the world thread only, and the bots update on that same thread after the maps finish, so the read calls see a consistent tick.
- Reads copy values; nothing a script receives points into the module.
- Writes are queued, never inline. The reply sink and the callback ledger sit behind a mutex as belt and braces.
- No binding calls `luaL_error`: a longjmp over C++ frames is not allowed (Lua is built as C).

## Smoke test

On a test server with Eluna and the toolkit installed ([toolkit README](../../tools/eluna/test-toolkit/README.md)):

```text
.tbapi smoke
```

prints one `TBAPI {...}` line with every check (`ok:true`, `failed:0` expected) and, a tick later, a `reply` line with the output of `.bot help`. The first player to enter a dungeon or raid state logs `TBAPI {"cmd":"mapstate","refused":true,...}`.
