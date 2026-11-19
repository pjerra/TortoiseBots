# TB custom entry ranges vs Forever DBs

Core wins. Checked against `TDB_full_forever_2026_10_14.7z` world SQL (creature/quest/gameobject_template row sets; hotfixes carry no template rows).

## Ranges TB uses

| Range | Table | Use | Status on Forever |
|---|---|---|---|
| 95000–95067 | creature_template + npc_text + broadcast_text | 68 innkeeper-clone recruiters (`tortoise_mercenary_hire`) | **BLOCKED**: 149 of 95000–95167 taken (e.g. 95001 Whitewater Grouper, 95005 Altruis the Sufferer). Renumber required |
| 95100–95101 | creature_template (credit-only, never spawned) | boss/aura kill credits | BLOCKED (same overlap) |
| 95110–95121 | creature_template (credit-only) | hire-unlock counters (HireUnlockPolicy.h:258) | BLOCKED (same overlap) |
| 95100–95167 | npc_text + broadcast_text | recruiter/credit texts | free (0 rows in Forever world; hotfix spell_name 95000 is a locale row, unrelated table) |
| 90000–90013 | quest_template | boss quests (Rend/Hakkar/Rally etc.) | free (0 rows; nearest Forever rows 90081+) |
| 90100–90106 | quest_template | follow-up quests | **BLOCKED**: 90100–90103 taken |
| 1000001 (A) / 1000236 (H) | gameobject_template | camp tents (CampPolicy.h) | free (no rows found) |
| 1100000 (alt) | gameobject_template | camp tent alt | **BLOCKED**: 1100000 taken (Forever row exists) |
| 1000380 | gameobject_template | Bone Chew Toy piles (Goldshire) | free (no row; verify spawn exists on live) |

## Free ranges (proposed)

- creatures: 1000000–1099999 (0 rows), 4000000–4099999 through 7000000–7099999 (0 rows).
  Avoid 96000–99999 (772+940+1828 rows), 200000–299999 (53188 rows), 8102000 (taken), 10000000 (taken).
  max creature_template id below 1M = 277882, so anything ≥1000000 is above all legacy rows.
- quests: 100000–199999, 300000–399999, 500000–599999 (0 rows). Avoid 91000–98999.
- gameobjects: 1002000–1009999, 1101000–1199999, 2000000–2099999 (0 rows).
- npc_text / broadcast_text 95000–95167: free in world DB. gossip_menu: max id 92251965, TB uses none custom.

Proposal: move creatures to 1000000+, quests to 100000+, camp-tent alt off 1100000 to 1002000+,
keep texts where they are.
