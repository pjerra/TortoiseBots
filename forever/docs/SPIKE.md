# ForeverCore port: the spike

Target: `Karrz1/TrinityCore`, branch `forever`, pinned at the commit in `forever/PINNED_CORE`.
Rule: **no change to the core.** Everything is in this repository, loaded as `src/modules/TortoiseBots`.

## What the spike is

A small module (`forever/src`) that proves the seams the whole port stands on:

| Step | How | State |
|---|---|---|
| Module is found and linked | root `CMakeLists.txt` + `trinity_add_module` | done: `worldserver` and `bnetserver` link with the module, core untouched |
| Bot session without a client | `TortoiseBots::BotSession : WorldSession`, `isBot = true`, null socket | run live |
| Log one existing character in | `LoginQueryHolder` + `AddQueryHolderCallback` + `HandlePlayerLogin` | run live 2026-11-17 |
| Stand in the world, follow the master | `MotionMaster::MoveFollow`, plus the shared `FollowTrailPolicy.h` when far behind | run live 2026-11-17 |
| Help the master fight | `Unit::Attack` + `MoveChase` on the master's victim | run live 2026-11-17 |
| Cast one spell | `.tb cast <bot> <spell>` -> `CastSpell` | run live 2026-11-17 |
| Log out cleanly | `LogoutPlayer(true)` from the session tick | run live 2026-11-17 |

Live run 2026-11-17 on a ForeverCore server at the pinned commit, no client connected, everything from the server
console: two characters created (`.tb create`), both logged in, the priest followed the warrior through two walks
(about 6 yd behind), the warrior attacked a Young Wolf, the priest took the same target by itself and cast Smite
(result 0), the wolf died, both left combat; logout saved position and state, and a login after a server restart
brought both back where they stood.

Commands (GM or console): `.tb create <account> <name> <race> <class> [gender]`, `.tb login <character> [master]`,
`.tb logout <character>`, `.tb cast <character> <spell id>`, `.tb go <character> <dx> <dy>`, `.tb attack <character>`,
`.tb where <character>`, `.tb status`.
Config: `TortoiseBots.Enable = 1` in `worldserver.conf.d/tortoisebots.conf`.

## Private members: none needed

`WorldSession.h` names two friend classes, both for Karrz's own bot module. Our session is not a friend and does not
need to be. What his session reaches through friendship, and what we do instead:

| His session uses (private) | We use (public) |
|---|---|
| `ProcessQueryCallbacks()` | `WorldSession::Update(diff, WorldSessionFilter)`. With a null socket it reads no packets, runs the callbacks and the timed logout, and returns false; false only means "remove from the world session map", which a bot session is not in. |
| `m_playerLoading = guid` | our own `_loginPending` flag. `HandlePlayerLogin` takes the guid from the holder. |
| `forceExit` (set by `KickPlayer`) | not readable. See the one gap below. |
| `m_playerLogout`, `_player` | `PlayerLogout()`, `GetPlayer()` |

So the "name our class `Playerbot::BotSession`" trick from the head start note is **not** needed.

### The one gap: a kick is invisible

For a bot session the core's `KickPlayer` only sets the private `forceExit`. Nothing public reads it. Cases:

- The account owner logs in to the character that is in the world as a bot. The core kicks the bot and waits up to
  30 s for it to leave. **Workaround in the module:** `BotMgr::Update` watches the account's real session; if it has
  been loading a character for more than 2 s, the account's bots log out.
- A failed character load: seen by `GetPlayer() == nullptr` after the callback.
- A GM `.kick` / ban of a bot: **not seen.** The bot stays. Use `.tb logout`.

A one-line public getter in the core (`bool IsKickRequested() const { return forceExit; }`) would close this. It is a
request for Karrz, not something we patch.

## Things found on the way

- `src/modules/README.md` shows `CollectSourceFiles(...)`. That macro does not exist at the pinned commit; the module
  globs its sources itself.
- `Player::SetTarget` does nothing for players; a bot's target is set with `SetSelection`.
- Teleports need the client's answer. The session gives it: `HandleMoveTeleportAck` (same map) and
  `HandleMoveWorldportAck()` (other map).

- **A bot sees nothing until its "client" reports its clock.** `Player::CanNeverSee` hides every object from a player
  until `PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME` is set, which the core does when the client sends
  `CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE`. A headless bot was in the world, with 23 creatures around it, and could
  attack none ("may attack: no"). The session sets the flag itself once the player is in the world.
- **New characters are saved through the asynchronous connection.** The character statements are not prepared on the
  synchronous one; `DirectCommitTransaction` there stops the server with an assertion.
- The module's config file is only read when worldserver is started with `--config-dir <folder>` or from the folder
  that holds `worldserver.conf.d`.

## Shared code

Measured on `main` 284395a84 by following every `#include` to the end:

- **564 source files (114,526 lines)** include no core header, directly or through another file. 554 are headers
  (`*Policy.h` and friends). They are used from the ForeverCore module by their repository path, unchanged.
- **478 test files (134,504 lines)** test only those. Built with the ForeverCore toolchain (clang, C++20):
  470 pass. 7 need the extra sources or arguments `tools/verify_all.sh` gives them. 1 real C++20 problem:
  `runtime/DecisionCapture*.h` (a `std::vector` of an incomplete type).

The other ~1,250 files reach the MaNGOS API and are the real port.

## Build

```
git clone -b forever https://github.com/Karrz1/TrinityCore.git ForeverCore
git -C ForeverCore checkout $(cat TortoiseBots/forever/PINNED_CORE)
ln -s /path/to/TortoiseBots ForeverCore/src/modules/TortoiseBots
cmake -S ForeverCore -B build -DSERVERS=1 -DSCRIPTS=static -DBUILD_TORTOISEBOTS=1
cmake --build build --target worldserver
```

`ninja tortoisebots` builds only the module (about 2 minutes on the build VM) and is the compile gate for port work.

## Open

1. The same steps with five bots in a party, and a master who is a real player on a game client.
2. Pathfinding: the run used `mmap.enablePathFinding = 0` (no `mmaps` extracted yet), so bots walk straight lines.

## Licence

The head start note says both projects are GPL-2.0. That is not right for this repository: original TortoiseBots work
is **AGPL-3.0-or-later** (`NOTICE.md`). TrinityCore is GPL-2.0-**or-later**, so linking the module into ForeverCore is
allowed, and the combined server is then under the AGPL terms (source must be offered to its players). Code may flow
from ForeverCore into this module, not the other way.
