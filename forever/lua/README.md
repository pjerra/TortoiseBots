# ForeverCore test toolkit (Lua, runs in ForeverLua; TEST SERVERS ONLY)

The `.tt` GM command of the agent's test toolkit, ported from the Eluna toolkit (`tools/eluna/test-toolkit`) to the
ForeverLua module (`TortoiseBots/forever-lua`, built into ForeverCore with `-DBUILD_FOREVER_LUA=1`). It targets the
module's **test mode** (`ForeverLua.Mode = "test"`, the default): the `Cmd`/`World`/`Timer`/`Json` tables and the
lowercase handle/Map methods. It makes no Eluna-mode calls.

Checked against the module at `d3de0cc` (Eluna stage E6, 2026-11-21): every call below exists with the arguments used
here; see "API audit" at the end.

## Install on a test server

Test mode loads **only the files directly in `ForeverLua.ScriptPath`** (no subfolders) and `require("x")` resolves
`<ScriptPath>/x.lua`. Copy the three files flat:

```
cp forever/lua/tt/tt_core.lua forever/lua/tt/tt_query.lua forever/lua/tt/tt_actions.lua  <ScriptPath>/
```

`ForeverLua.CommandRoots` must contain `tt` (it is the default); changing it needs a restart. Then `.lua reload`
(or restarting the server). `tt_core.lua` logs a warning at load when `Cmd.register("tt", ...)` is refused because the
root is not configured, so a wrong `CommandRoots` is visible instead of silent.

Optional live self-test: also copy `forever-lua/scripts/selftest/selftest.lua` from the module repository flat into
`ScriptPath`; it adds `.tt selftest #id` and `.tt selftest-loop` (it reuses this toolkit's dispatcher when `tt_core` is
loaded).

Guids in every reply and argument are `ObjectGuid::ToString()` strings (e.g. `Creature-0-1-2-3-14-0000A1B2C3`), not
64-bit hex numbers. The old Eluna toolkit's hex guids do not work here.

## Commands

`.tt <sub> [args] #<id>` — the caller needs GM rank 3+ (`TT.CFG.min_gm_rank`); anyone lower gets
`{"ok":false,"err":"not_allowed"}`. The core already requires the GM permission for `.tt`, so in practice only GMs reach
the dispatcher.

| Command | Does |
|---|---|
| `.tt` / `.tt help` | the one-line help |
| `.tt q floor [x y [z]]` | floor under the caller (or at x y within 40 yd, searched down from the caller's z), the surface above (`top`), `caller_above`, `dz_wanted`, `multi` (two levels) |
| `.tt q units [r] [x y z] [all] [dead] [friendly] [e=<entry>] [au=<id,...>] [of=<lead>]` | creatures within r (2D, default 40, max 100): guid, entry, name, pos, hp, combat, dead, victim, cast `cs`, channel `ch`, DB spawn id, rank, home; `au=` looks those spell ids up (`caster` is a guid string, `stacks`, `remainingMs`) |
| `.tt q unit <guid> [au=...]` | one unit on the caller's map; `asked` echoes the guid (the client drops a reply whose `unit.g` differs); a game object answers `not_a_unit` |
| `.tt q los <x> <y> <z> <guid>...` | line of sight (1-8 guids) from an eye 2 yd above the point: `in`, `d`; a bad guid answers its own `err` (`not_found`, `not_a_unit`), and a sight read the engine cannot make answers `los_failed`, never "not in sight" |
| `.tt q gos [r] [x y z] [e=<entry>]` | game objects within r (default 30, max 100): state (0 active/open, 1 ready/closed, 2 alternative), loot state, spawned, spawn id |
| `.tt q group [of=<lead>]` | the caller's group (or `of=`'s): map, instance, pos, hp, alive, combat, GM flag, distance and height above the floor for members on the caller's map |
| `.tt quiet <r> [x y z] [w=<s>] [dz=<yd>] [skip=<e,e>] [of=<lead>]` | the owner's quiet rule: no hostile alive within r (2D) now and after w s (default 20, max 60), and no hostile within 80 yd that moved or arrived; two replies (`read` 1 then 2 with `final`), `quiet` on read 2 |
| `.tt clearto <map> <x> <y> <z> <r> [kill] [boss] [dz=<yd>] [skip=<e,e>] [all=1] [of=<lead>]` | despawn (default) or kill hostile creatures within r (max 60): bosses (rank 3+, world boss) skipped unless `boss`, pets/guardians always skipped, friendly NPCs never touched unless `all=1` (friendliness judged from a non-GM group member; without one it refuses `no_reference`); `skipped[]` carries `why` = `skip`/`dz`/`friendly`/`owned`/`boss`/`cap` |
| `.tt place [here \| <x> <y> <z>] [spread=<yd>] [force] [me] [of=<lead>]` | move every group member on the caller's map and instance onto the floor at the point in one burst (ring, default 3 yd, facing the centre); refuses a fighting member (unless `force`), a floor that does not match z, a caller under the floor; second reply after 4 s with where each landed (`placed` true when every member is within 2 yd of z and 5 yd of its spot) |
| `.tt reset <mapId>` | `.instance unbind <mapId>` through the GM bridge with the caller's own rights; refused while the caller is inside that map, and refused for maps 0/1 |

`dz=` (default 8, `0` = no vertical limit) and `skip=` are shared by `quiet` and `clearto`.

## Replies

Every answer is one system-chat line `TTJSON {"v":1,"cmd":...,"id":...,"ok":...,"ts":...}`; `quiet` and `place` send a
second line (`"read":2,"final":true`) from a `Timer.after` callback. `#<id>` is echoed. A reply longer than 3500 chars
has its longest list cut and carries `"trunc":true`; the engine caps one reply line at 4000 bytes.

## Tests

`tests/mock_tt_test.lua` runs the real `tt/*.lua` on plain Lua 5.4 with fake `Cmd`/`World`/`Timer`/`Json` and fake
handles that mirror the module's return shapes (read `nil` on a gone object, `false, "gone"` from an action, the group
leader guid as `Player:group()`'s second return, `Map:los` `nil` for "cannot judge"), so it needs no server:

```
bash forever/lua/check.sh                      # luac -p, luacheck when installed, the mock test
lua5.4 forever/lua/tests/mock_tt_test.lua      # the mock test alone, from the repository root
```

The mock test prints `<n> checks, 0 failed` and exits 0. `tools/verify_all.sh` (step 4b, so also the Verify workflow)
runs `forever/lua/check.sh` when `lua5.4` is installed. Lint alone: `luacheck --config forever/lua/.luacheckrc
forever/lua/tt/*.lua forever/lua/tests/*.lua` (0 warnings).

## API audit (module `d3de0cc`, stage E6)

Every call was matched against the module's bindings (`src/bind/BindWorld.cpp`, `src/bind/BindObjects.cpp`,
`src/engine/Handle.cpp`, `src/engine/Api.cpp`). **No name, argument or return-shape mismatch was found**; the module's
test-mode API did not change between the 11-20 build this toolkit was written for and E6. What was checked and is
relied on:

* `Cmd.register("tt", fn, 0)` → `ctx.player`, `ctx.security`, `ctx.args` (root word excluded), `ctx.id`, `ctx:reply`;
  returns `true` or `false, "root not configured"` (now logged, see above).
* `Timer.after(ms, fn)`, `World.now()`, `World.player(nameOrGuid)`, `Json.encode(v)`, `Json.null` (lightuserdata;
  encoded as `null`).
* base handles: `valid`, `guid`, `kind`, `mapId`, `instanceId`, `entry`, `name`, `alive`, `pos`, `despawn`.
* `Map`: `id`, `instance`, `isDungeon`, `name` (kind-dispatched since 11-20; `Unit:name` depends on it), `height`
  (nil for bad coords/no map), `los(..., [unit])` (boolean, or nil when it cannot be judged), `creatures(x,y,z,r,{entry,alive,dead})`,
  `gameObjects(x,y,z,r,{entry})`, `find(guid)`.
* `Unit`: `map`, `health`, `maxHealth`, `healthPct`, `inCombat`, `victim`, `isHostileTo`, `aura(id)` →
  `{caster (guid string), stacks, remainingMs}`, `casting()` → `spellId, targetGuid|nil`, `channel()` → `spellId`.
* `Player`: `security` (nil without a session), `group()` → members, leaderGuid, `message`, `teleport(map,x,y,z,o)`,
  `run(line)` → ok, lines.
* `Creature`: `home`, `rank` (classification; 3+ is treated as boss), `isBoss`, `owner`, `kill`, `spawnId`.
* `GameObject`: `state`, `lootState`, `spawned`, `spawnId`.

## Live check

2026-11-20 on `forever-test2` (core `c0341987`, module before E0/E6), the old-engine build this branch was cut from:
`.tt q floor`, `q units`, `q group`, `quiet`, `clearto`, `place` and the module selftest passed; every reply carried
ObjectGuid strings. `q unit`, `q los`, `q gos` and `reset` were **not** live-tested then. A live re-check on E6 has
**not** been run.
