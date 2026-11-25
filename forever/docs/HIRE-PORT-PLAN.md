# Hire port plan (issue #298)

Goal: a player hires a bot at a `<Mercenary Hire>` recruiter, the bot joins the party at the chosen gear tier, and dismissing
it deletes the character. Same model as TortoiseBots (TB), on ForeverCore's APIs. Raid hire (`.bot hire raid`) is a later issue.

Owner decisions (2026-11-25):
- Hiring is at recruiter NPCs only by default. Hiring anywhere (`.bot hire`, add-on Hire tab) only behind
  `AiPlayerbot.HireAnywhere` (default 0). TBFM shows its Hire tab only when the server advertises that flag.
- Gear tiers: full TB parity (contract unlocks by boss kills, fixed sets at level 60), without Turtle-only content
  (no Turtle custom dungeons/raids/items); add Forever's new dungeons/raids where they fit. Check each set on Forever's item
  data: no drastic stat/ilvl change from TB.
- World buffs: TB model (earn the buff once for real, unlock it at a capital recruiter, then buy it) on Forever's spells,
  Alliance gets Might of Stormwind 460939 in place of Warchief's Blessing 16609.
- Recruiter spots: every Forever inn, incl. the new zones (Riverglades, Zephras Isle, Shen'dralas, Mount Hyjal, Talrendis Point).

Contract: `forever/src/host/hire/TbHire.h` (functions per package) and `runtime/HireLifecycle.h` (the ledger API the engine
already calls). Forever code lives in `forever/src/host/hire/`, Forever SQL in `forever/sql/{world,characters}/`. TB files under
`runtime/`, `host/`, `commands/` are the Turtle build: change them only if Turtle still builds, prefer not to.

Ids (forever/data/waterys/id-ranges.md): recruiters 1000000-1000099, credit creatures 1000070-1000099, quests 100000+,
npc_text/broadcast_text 95000-95167 (broadcast_text is in the hotfixes DB on Forever). Spawn guids 4000000+.
Draft rows to start from: `forever/data/waterys/tortoisebots-npcs-forever.sql` + `npcs-sql-notes.md`.

## Packages

| # | Package | Owns | Depends on |
|---|---|---|---|
| P1 | Ledger + lifecycle | Forever `HireLifecycle` (claim, master link, party leave / dismiss / logout grace, BG rule, deletion with the three guards, startup recovery), `tb_hire` character table, hire accounts separate from the pool, removing the stub in `TbHostOwnership.cpp`, hooks (group remove/disband, master login/logout, world update) | - |
| P2 | Provision + commands | `hire::Hire/QuoteFee/Update`: gates, fee/refund (`HireCost`, `HireFeePolicy`), create via `BotMgr::CreateCharacter`, level, spec (`slice::spec`), levelled kit (`gear::SetGearForTarget`), tier set via P4, login bound to the hirer, invite, intro; `.bot hire` (fast path, HireAnywhere gate), `.bot remove/logout` on a hire; add-on: `hire` verb, `hiring`/`hire-anywhere` caps, ServerInfo feature rows, hires out of `TBM:ROSTER` | P1 API |
| P3 | Recruiters | world SQL: templates, models, levels, 55 spawns from the inn table, greetings (npc_text + hotfix broadcast_text); `CreatureAI` gossip wizard (class, race, gender, spec, tier, confirm) registered by ScriptName, neutral-inn cross-faction, root menu routing to P4/P5 pages | P2/P4/P5 signatures |
| P4 | Tiers + contracts | tier sets on Forever items (TB parity minus Turtle content, plus new Forever instances), tier fee mults, unlock quests (Trinity schema, 100000+), kill-credit counting (Forever kill hook), contracts page, `EquipTierSet` | - |
| P5 | World buffs | unlock quests + credits (boss kill, aura gained, Silithus PvP), purchase page at the six capitals, `recruiter->AddAura` on buyer + group in 40 yd, faction table (460939 Alliance), raid rule, 22817 check | - |
| P6 | TBFM Hire tab (after P2) | tab shown only with the `hire-anywhere` cap; class/spec/tier/level, quote, Hire button; regression cases | P2 wire |

Lead: build each branch (slice ON, slice OFF, Turtle), integrate on `feat/hire-integ`, one headless live test
(hire at a recruiter, bot joins with tier gear, dismiss deletes the character), then the PR to `dev`.
