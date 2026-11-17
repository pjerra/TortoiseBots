# Forever Agent (with Forever Trials inside): plan

Ports `pjerra/turtle-agent` (Go, headless game client, raid and dungeon autopilot) and `pjerra/tortoise-trials`
(Python, bot behaviour test bench) to ForeverCore. One repository, `forever-agent`; trials is a tool inside it.

Measured 2026-11-17: turtle-agent `dev` e461eb42 (about 207k lines of Go, 1,060 files), tortoise-trials `main`
5fcf700 (about 30k lines of Python, 65 test files). Both AGPL-3.0.

## Owner decisions (2026-11-17)

1. **Public repository `pjerra/forever-agent`** (owner 2026-11-17; private for a few minutes in between). Its public text describes a client for ForeverCore servers and does
   not mention the TortoiseBots port, which stays private (head start note).
2. **Same as turtle-agent: a real network client.** The agent logs in over the wire like a game client. No
   player-inside-the-server shortcut.
3. **Trials is rewritten in Go**, as a tool inside the agent. No bundled Python.

## The hard fact

turtle-agent speaks the 1.12 wire protocol. ForeverCore speaks the modern one:

| Layer | Turtle (today) | ForeverCore |
|---|---|---|
| Login | realmd, SRP6 | bnetserver: TLS, REST login, protobuf RPC services, then a join ticket |
| World socket | header crypt | encrypted mode handshake, compressed packets |
| Packets | about 150 hand-built 1.12 packets | 2,457 opcodes, bit-packed structs, build 70938 (70888 also accepted) |
| Objects | 1.12 update fields | modern update fields with change masks |
| Client files | MPQ | CASC |
| Server test toolkit | Eluna scripts | no Lua in ForeverCore |

Everything under `internal/session`, `auth`, `worldconn`, `packet`, `opcode`, `objects` and most of `game`
(about 20k lines) is written new. The layers above are not cleanly cut off from it: `internal/packet` is imported
by 17 packages, including `brain`, `dungeon` and `raidwalk`.

The source of truth for every packet is the core itself (`src/server/game/Server/Packets/*`, `WorldSocket.cpp`,
`src/server/bnetserver`, `src/server/proto`) at the pinned commit. Only the packets the agent uses are written
(about 150 to 200 of 2,457). A packet layout is never guessed: each one cites the core file and line it follows,
and a round-trip test feeds it bytes captured from a real session.

## Repository

- `pjerra/forever-agent` (public), checkout `~/forever-agent`, made from turtle-agent's history, Go module
  `github.com/pjerra/forever-agent`, binary `forever-agent`, default branch `main`.
- turtle-agent and tortoise-trials stay as they are for the Turtle server.
- AGPL-3.0 and both NOTICE files are kept.

## Trials in Go

- `internal/trials` (scenario, runner, recorder, rules, scorer, report) and `forever-agent trials <run|score|report|...>`.
- The runner half (leases, slots, setup, recording) is agent calls already and becomes direct Go calls.
- The rules and the scorer are translated one file at a time **with their tests** (65 test files become Go tests).
- Proof that the rewrite scores the same: the Python scorer and the Go scorer both score the same recorded Turtle
  runs (the run folders we already have), and the report cards must be equal. Only after that are rules changed for
  ForeverCore. The Python bench stays in tortoise-trials as the reference until then.

## Phases

| # | Phase | Result | Agent time | Needs |
|---|---|---|---|---|
| 0 | Seam audit | Every place above the protocol layer that touches `packet`, `opcode`, `objects`; the `game.Client` interface written down | 2-3 days | nothing |
| 1 | Repository | `pjerra/forever-agent` with turtle-agent's history, renamed module and binary, tests green for the parts that need no server | 2-3 days | nothing |
| 2 | Login | bnetserver login in Go (TLS, REST, protobuf services, realm join) until the world server accepts the session | 2-3 weeks | a running ForeverCore server |
| 3 | World socket and core packets | Encrypted and compressed socket, character list, enter world, time sync, movement, chat, addon messages | 2-3 weeks | phase 2 |
| 4 | Objects | Modern update fields: units, players, items, game objects, auras, for the fields the agent reads | 2-3 weeks | phase 3, a packet capture from a real client for the tests |
| 5 | `game` layer | Combat, casting, group and raid, loot, gossip, quests, trade, pets on the new packets; `forever-agent login/status/...` work as on Turtle | 3-4 weeks | phase 4 |
| 6 | Test toolkit | What Eluna did on Turtle (spawn a mob, read exact positions and health, set gear and level) as GM commands and TortoiseBots module test commands | 1 week | TortoiseBots module on the server |
| 7 | Trials in Go | `internal/trials`, equal scores to the Python bench on recorded runs | 3-4 weeks | can start after phase 1, in parallel with 2-5 |
| 8 | Trials on ForeverCore | One trial end to end, then the data pass (spell ids, talents, gear, mob entries, rule citations) and the class sweep | 3-4 weeks | phases 5-7, and the bot brain ported for the class |
| 9 | Navmesh and maps | Navmesh from ForeverCore `mmaps`, map and minimap views from extracted data instead of MPQ | 1-2 weeks | extracted client data |
| 10 | Dungeon and raid autopilot | `dungeons/*.json` for the vanilla instances checked against the Forever world database; raid-walk | 3-5 weeks | TortoiseBots dungeons and raids ported |

First login to first fight (phases 2-5) is about 9 to 13 weeks of agent time. Phases 0, 1 and 7 need no Forever
server and can start at once.

## Seam audit result (2026-11-17, `docs/forever/SEAM-AUDIT.md` in forever-agent)

Phase 0 is done. It changes two things in this plan:

- **The seam is `*session.Session`, not `internal/game`.** The session has 116 exported methods; `cmd` calls 108 of
  them. `brain` and `dungeon` already sit behind their own small interfaces (`brain.Game`, `dungeon.Game`) that four
  adapters in `cmd` implement. So phase 5 is "a new session with the same method set" (`game.Client`, 108 methods in
  16 areas), not a rewrite of the layers above.
- **What leaks upward is value types:** `packet.GUID` (530 uses in 95 files) and the `objects` structs (285 uses in
  64 files). The refactor order starts with Go type aliases, so the 1.12 side keeps its behaviour and tests.

**A creature GUID no longer names its spawn.** turtle-agent takes the low 24 bits of a GUID as the database spawn id
(9 places), and plan files store spawn ids. On ForeverCore the GUID counter is a number the map hands out when the
creature is created (`Creature::LoadFromDB`: `Create(map->GenerateLowGuid<HighGuid::Creature>(), ...)`); the spawn id
is kept server-side only (`m_spawnId`). The GUID does carry the creature entry and the map. So plans need another way
to find "this spawn": entry plus spawn position, or a lookup the TortoiseBots module answers. To decide in phase 4.

The server-specific text protocols are larger than the wire seam (TBM addon messages 694 uses, GM dot-commands on 143
lines, `.bot` on 82): they follow the TortoiseBots module port, not the client protocol.

## What is dropped or replaced

- Dropped: Turtle LFT (`internal/lft`), the realmd hour-start login hold, Turtle-only dungeons (Crescent Grove,
  Karazhan Crypt, Gilneas City, Hateforge Quarry, Stormwind Vault and the like), MPQ reading.
- Replaced: Eluna toolkit by GM and module test commands; TBM addon messages by the modern addon channel.
- Re-checked, never copied blindly: every spell id, talent build, creature entry, door and route point.

## Risks

1. **The protocol is the long pole.** Nothing above it can be tried on ForeverCore before phases 2-4 work.
2. **The core pin moves almost daily.** A packet layout can change with it. Each packet cites its core source, and a
   pin move re-runs a check that those cited lines did not change.
3. **Depends on the TortoiseBots port.** Trials score bot behaviour; before the bot brain runs on ForeverCore there
   is nothing to score except the spike.
4. **Login needs the client's certificate setup.** The server certificate is for `trinity.actual.battle.net`; the
   agent has to be told which root to trust, as a client PC is.
5. **Two trees drift.** turtle-agent keeps changing daily; cherry-picks stop applying over time.
