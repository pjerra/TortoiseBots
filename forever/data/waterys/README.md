# Data and notes from Waterys (received 2026-11-17)

| File | What |
|---|---|
| `forever-spell-ids.csv` | 259 class spells TortoiseBots uses by name: status, ForeverCore rank 1 id, all rank ids, source, replacement. 222 OK, 2 CHANGED, 29 MISSING (Turtle-only or later expansions), 6 PSEUDO. |
| `bosses.csv` | 230 rows for 7 raids (206 bosses, 23 adds, 1 trash): Turtle entry, ForeverCore entry, source. 27 are absent from ForeverCore's `creature_template`. |
| `spawn-id-sites.txt` | 16 places in the bot code that find a creature or game object by database spawn id taken from the GUID. On ForeverCore the GUID carries entry and map, not the spawn id, so each needs another lookup. |
| `TBM-PROTOCOL.md` | The addon-to-server contract of TortoiseBotsManagerForever, unchanged from TBM 2.4.1. |
| `TBMF-PORT-NOTES.md` | What changed in the addon for the 1.60.1 client, what is verified, open questions. |
| `forever-feature-compare.md` | TortoiseBots against ForeverBots (PlayerbotV2), area by area. |

The addon itself (`TortoiseBotsManagerForever-2.4.1-forever.1.zip`) is not in this folder; it belongs in its own
top-level folder or repository.

## Before these ids drive code

167 of the 259 spell rows name ForeverBots source code as where the id came from, not ForeverCore's data. The rule
for this port is that the core's own data wins (DB2 and the world database). So every id is checked against a running
ForeverCore server (name and rank for that id) before a class strategy uses it. Until that check is recorded here,
treat the table as a lead, not as proof.
