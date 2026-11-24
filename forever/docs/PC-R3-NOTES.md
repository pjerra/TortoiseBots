# Player commands, round R3: `summon`, `stats`, the run guard on every verb, docs

Spec: `~/TortoiseBots-audit/PLAYER-COMMANDS-SPEC.md` (TBF #129 + #132), round R3 of section 6. Branch
`feat/player-commands-r3` from `tbf/feat/player-commands-r2` (R2 = PR #197, stacked). No pin move; no ForeverCore core change.

## What the round adds

* **`h4::TeleportBotNextTo`** (`host/TbH4.h`, `host/TbH4.cpp`) — `TeleportNextTo` leaves the anonymous namespace as
  `TortoiseBots::h4::TeleportBotNextTo(Player* source, Player* dest, bool revive, h3::Rec& rec)`; `.tb tele <character> to
  <character>` calls it unchanged. `.bot summon` calls the same function, so the map, instance (owning-group), combat, death,
  teleporting and entry rules exist once (D-8: "no second copy").
* **`.bot summon <bot>`** (`host/player/TbPlayerCmd.cpp`, `HandleSummon`) — Owner tier (`Decide`), then, in this order:
  `not-master` (the caller must be the bot's master; a GM need not be — the bot would walk back to its master), `teleporting`
  (the caller is being teleported) or `in-flight` (the caller rides a taxi), `on-cooldown` (`SummonCooldownLeft`), then the
  shared teleport with `revive = false`. The shared refusal is read back out of the reply record and its code and message pass
  through unchanged (`in-combat`, `dead`, `already-on-map`, `not-in-group`, `teleport-refused`, entry codes, ...).
* **The cooldown** (D-8, D-13) — one stamp per bot guid in an in-memory map, written **only** when `TeleportBotNextTo`
  started; a refused summon stamps nothing. `AiPlayerbot.OwnedBotSummonCooldown` is the legacy key, read at config load
  through `sConfigMgr` with the legacy read's own default (300 s) and cap (3600 s); 0 = no wait; GMs are exempt (the pure
  rule's `SummonCooldownLeft`). The other `AiPlayerbot.OwnedBotSummon*` keys stay unread.
* **`.bot stats <bot>`** (`HandleStats`) — Tactical tier; one line built from the bot's own objects on the world thread
  (D-9), no engine tick and no `stats` whisper: level, class, health %, mana %, money in g/s/c, zone id and name (DB2
  `AreaTableEntry`), `alive|dead`, and the movement the bot keeps (`follow|stay|free`, the same word `.bot who` prints).
* **Run guard** — nothing new in code: `Decide` already answers `in-run` for every caller (GMs included) before the GM and
  owner branches; R3's two verbs go through `Decide` (`Tier::Owner` for summon, `Tier::Tactical` for stats), so a bot inside
  a running dungeon run refuses both.
* **`EncDecode`** (`host/player/TbPlayerAuthPure.h`) — the reply record encodes every value (TbH3.h `Rec`); the summon
  refusal decodes the shared message back to human text. A malformed escape stays as typed. Unit-tested
  (`case: encoded_text_decoded` … see `test_player_auth.cpp`).
* **Docs and config** — `docs/TB-COMMANDS.md` section 1a: every R1-R3 verb, the summon rules, the reply lines and the code
  list. `conf/tortoisebots.conf.dist`: the `AiPlayerbot.OwnedBotSummonCooldown` paragraph (where the key lives, its default
  and cap, that the other `OwnedBotSummon*` keys are not read).

## Files changed (lines added / removed against the R2 tip, `git diff --numstat`)

* `forever/src/host/TbH4.h` +6/-1 — `class Player;`, `class Rec;` forward declaration, `TeleportBotNextTo` declaration.
* `forever/src/host/TbH4.cpp` +4/-6 — the function moved out of the anonymous namespace and renamed; `.tb tele` form 1 calls it.
* `forever/src/host/player/TbPlayerAuthPure.h` +36 — `EncDecode`.
* `forever/src/host/player/TbPlayerCmd.cpp` +157 — `HandleSummon`, `HandleStats`, `SummonStamps`/`SummonStampOf`,
  `TeleportRefusal`, the `s_summonCooldown` key, two help rows, two `Dispatch` routes, `host/TbH4.h`, `Timer.h`,
  `SharedDefines.h`, `<cmath>` includes.
* `forever/tools/test_player_cmd_wiring.py` +88/-2 — the R3 wiring tests (the two named failing cases first).
* `forever/tools/test_player_auth.cpp` +13 — `case: encoded_text_decodes`.
* `forever/conf/tortoisebots.conf.dist` +12, `forever/docs/TB-COMMANDS.md` +20/-4 — as above.
* `forever/docs/PC-R3-NOTES.md` +65 — this file.


## Tests and gates

| Gate | Result |
|---|---|
| `python3 forever/tools/test_player_cmd_wiring.py` | 32 tests OK |
| `PLAYER_CMD_MUTATE=1 python3 forever/tools/test_player_cmd_wiring.py` (build VM) | 8/8 mutations caught |
| `test_player_auth.cpp` (g++ -std=c++17 -Wall -Wextra, build VM) | 96 checks, 0 failed |
| no-direct-queries test, Turtle build, module SLICE1 ON and OFF (slot 6) on the stack itself | PASS (`tbf-premerge` on top of tbf/main stops at a merge conflict in `TbOrders.cpp`, the R2 file; R3 does not touch it) |

The two named failing cases were written first and failed on the R2 source:
`test_summon_uses_shared_teleport` (FAIL: `'h4::TeleportBotNextTo(' not found`) and `test_cooldown_stamped_only_on_start`
(FAIL: `'SummonCooldownLeft(' not found`); the cooldown-stamp case also pins the refusal branch between the teleport and the
stamp, which is the reviewer's mutation 9.

## Review round (Codex adversarial + Opus cold review)

* Both found the same real gap: a bot on a taxi could be summoned (`Player::TeleportTo` does not end a flight; the cooldown was spent).
  Fixed in the shared `TeleportBotNextTo`: `in-flight` for either side, so `.tb tele` gets it too. Test `test_shared_teleport_refuses_a_bot_on_a_taxi`.
* Found in the lead's pass: the cooldown clock was the core's 32-bit `getMSTime()` (wraps after 49.7 days; a stamp near the wrap would
  lock a bot out until restart). Now a 64-bit `steady_clock`. Test `test_summon_clock_is_64_bit_monotonic`.
* Noted, not changed: `stats` is tactical tier (D-4), so a group leader or commander reads the bot's money; the cooldown is stamped
  when the core accepts the teleport (D-8 wording) and a GM's summon stamps it too; a requester on a transport drops the bot at world
  coordinates (already true of `.tb tele`).

## Live

Queued with R2 on the test server (`LIVE-QUEUE.md`, checks A-4 in the spec): owner teleported away, `.bot summon` ok, an
immediate second summon refused `on-cooldown`, a GM's summon not held by the wait, and a summon refused `in-run` inside a
running dungeon run.
