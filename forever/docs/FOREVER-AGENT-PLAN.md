# Forever Agent (with Forever Trials inside): plan

Ports `pjerra/turtle-agent` (Go, headless game client, raid and dungeon autopilot) and `pjerra/tortoise-trials`
(Python, bot behaviour test bench) to ForeverCore. One repository, `forever-agent`; trials is a tool inside it.

Measured 2026-11-17: turtle-agent `dev` e461eb42 (about 207k lines of Go, 1,060 files), tortoise-trials `main`
5fcf700 (about 30k lines of Python, 65 test files). Both AGPL-3.0.

## The one hard fact

turtle-agent speaks the 1.12 wire protocol (realmd SRP6, RC4-style header crypt, 1.12 opcodes and update fields).
ForeverCore speaks the modern one:

| Layer | Turtle (today) | ForeverCore |
|---|---|---|
| Login | realmd, SRP6 | bnetserver: TLS, REST login, protobuf RPC services, then a join ticket |
| World socket | header crypt | encrypted mode handshake, compressed packets |
| Packets | about 150 hand-built 1.12 packets | 2,457 opcodes, bit-packed structs, build 70938 |
| Objects | 1.12 update fields | modern update fields with change masks |
| Client files | MPQ | CASC |
| Server test toolkit | Eluna scripts | no Lua in ForeverCore |

Everything under `internal/session`, `auth`, `worldconn`, `packet`, `opcode`, `objects` and most of `game`
(about 20k lines) is protocol and cannot be carried over. And the layers above are not cleanly cut off from it:
`internal/packet` is imported by 17 packages, including `brain`, `dungeon` and `raidwalk`.

## Decision: how the agent is "a player"

| | A. Real network client | B. Player inside the server |
|---|---|---|
| What | Write the modern protocol in Go | The TortoiseBots module logs a character in without a client (the spike's session) and offers a local control port; the agent drives that |
| Tests the real client path | yes | no (handlers are called directly) |
| Work before the first trial runs | 6 to 10 weeks | 1 to 2 weeks |
| Breaks when the core pin moves | often (packet layouts) | rarely (module compiles against the core) |
| Needs | every packet layout from the core's own `WorldPackets` code | the module on the server (it is there anyway) |

**Plan: B first, A later, behind one interface.** Trials measure what a bot does next to a player. A player
that is in the world through the module is a real `Player` to the bots, so the rules stay valid. The agent's upper
layers talk to a `game.Client` interface; B is the first implementation, A can be added as a second one when the
client path itself needs testing. The control port and the test commands it needs also replace the Eluna toolkit,
which has to be replaced in any case.

## Repository

- New repository `forever-agent`, made from turtle-agent's history (not a GitHub fork, so it is not tied to the
  public network), Go module `github.com/pjerra/forever-agent`, binary `forever-agent`.
- tortoise-trials is merged in with its history under `trials/` (`git subtree add`).
- Private until the TortoiseBots port is public. AGPL-3.0 and both NOTICE files are kept.
- turtle-agent and tortoise-trials stay as they are for the Turtle server. Fixes that apply to both are
  cherry-picked while the trees are still close.

## Trials as a tool inside the agent

Stage 1 (with the move): the Python bench lives in `trials/` and runs as `forever-agent trials <run|score|report|...>`,
a subcommand that starts the bundled Python. It no longer looks for a separate binary, a slot table or a lease
through the command line: it calls the agent it is part of.

Stage 2 (only if it pays): the runner (leases, slots, setup, recording) moves into Go (`internal/trials`), because
that half is all agent calls. The rules and the scorer (12k lines, 65 test files) stay in Python until there is a
reason to rewrite them; rewriting them changes no behaviour and risks the scores.

## Phases

| # | Phase | Result | Agent time | Needs |
|---|---|---|---|---|
| 0 | Seam audit | List of every place above the protocol layer that touches `packet`, `opcode`, `objects`; the `game.Client` interface written down | 2-3 days | nothing |
| 1 | Repository | `forever-agent` with both histories, renamed module and binary, CI green on the parts that do not need a server | 2-3 days | your OK to create the repository |
| 2 | Control port in the module | `forever/src`: local JSON-RPC (login a player character, move, target, cast, chat and commands, read units, auras, combat log, positions) plus test commands (spawn a mob, set level and gear, reset) | 1-2 weeks | TortoiseBots spike run live |
| 3 | Agent on the control port | `game.Client` implementation B; `login`, `status`, movement, combat log, chat work against a Forever server | 1-2 weeks | phase 2 |
| 4 | Trials move in | `forever-agent trials run` plays one trial end to end with a hired bot | 1 week | phase 3, and the bot brain far enough for one class (TortoiseBots PORT-PLAN step 4-5) |
| 5 | Trials data pass | Spell ids, talents, gear sets, mob entries and rule citations (`file:line`) re-derived for ForeverCore; class sweep, tanks first | 2-3 weeks, with the class ports | phase 4 |
| 6 | Navmesh and maps | Navmesh from ForeverCore's `mmaps`, map and minimap views from extracted data instead of MPQ | 1-2 weeks | extracted client data |
| 7 | Dungeon and raid autopilot | `dungeons/*.json` for the vanilla instances checked against the Forever world database (entries, spawns, doors); Turtle-only instances dropped; raid-walk | 3-5 weeks | TortoiseBots PORT-PLAN step 10 |
| 8 | Real client (option A) | Login through bnetserver and the world socket in Go, as a second `game.Client` | 6-10 weeks | only when wanted |

Phases 0 and 1 can start now. Phases 2 and 3 are the first that need the server running.

## What is dropped or replaced

- Dropped: Turtle LFT (`internal/lft`), the realmd hour-start login hold, Turtle-only dungeons
  (Crescent Grove, Karazhan Crypt, Gilneas City, Hateforge Quarry, Stormwind Vault and the like), MPQ reading.
- Replaced: Eluna toolkit by module test commands; TBM addon messages by the module's addon channel on ForeverCore;
  the dashboard feeds by whatever the ported module emits.
- Re-checked, never copied blindly: every spell id, talent build, creature entry, door and route point.

## Risks

1. **Depends on the TortoiseBots port.** Trials score bot behaviour. Before the bot brain runs on ForeverCore
   there is nothing to score except the spike.
2. **Option B does not test packets.** A bot bug that only shows through a real client's packet order stays hidden
   until phase 8.
3. **Upper layers leak the protocol.** If the seam audit finds `brain` or `dungeon` reading raw packets in many
   places, phase 3 grows.
4. **Two trees drift.** turtle-agent keeps changing daily. The longer the fork lives, the less a cherry-pick applies.

## Open decisions (yours)

1. Create the repository `pjerra/forever-agent`, private: yes or no.
2. B first (recommended) or the real client from the start.
3. Trials stage 1 as bundled Python (recommended) or a Go rewrite at once.
