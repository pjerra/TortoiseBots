# What real WoW Forever players use (aotc.gg, read 2026-11-24)

Statistics from aotc.gg, which reads the official WoW Forever armory. They show what real players run on the
Forever trees and items, not on Turtle 1.18.1. Use them as a reference for bot behaviour on ForeverCore. The core's own
data (DB2 and the world database) still wins when the two disagree.

All numbers cover every region and realm type. "60" means level 60 characters. A spec on aotc.gg is the tree that
holds the most points.

| File | What |
|---|---|
| `talent-picks.csv` | For every spec, level group (`10-19` … `50-59`, `60`, `all`) and talent: the share of characters with enough points who took it (`pick_pct`), the usual rank, the talent's spell id, and its place in the tree (`tab`, `row`, `col`). `our_trait_node`, `our_talent_id` and `our_max_rank` match it to our trees in `talent-trees-70959.csv` (from PR #235) by class and name. All 9,807 rows match. |
| `talent-tree-points.csv` | Average points per tree for every spec and level group. It shows how the points move into each tree while players level. |
| `talent-splits-60.csv` | The 20 most common point splits (e.g. `12/8/31`) per spec at 60, with share and character count. |
| `stat-priority-60.csv` | aotc.gg's stat priority per spec at 60 (stat and weight, highest first). |
| `gear-60.csv` | The 20 most worn items per slot, and item-set bonus use (`Set: …` rows), per spec at 60. `population` is `best_geared` (the top-geared characters) or `all_60`. `wowhead_id` is the item id. |
| `enchants-60.csv` | The most used enchants per slot, per spec at 60, in the same two populations. `wowhead_id` names the item that teaches or applies the enchant (empty for "No enchant"). |
| `spec-population.csv` | The share of each spec among a class's level 60 characters. |
| `census-classes.csv` | Every class: characters tracked, Alliance and Horde counts, characters at 60 and their share of the class. |
| `census-races.csv` | Every race: faction, share of its faction, share of female characters. |
| `census-class-gender.csv` | Share of female characters per class. |
| `census-race-class-pairs.csv` | The 10 most and 10 least played race and class pairs. |
| `census-levels.csv` | Characters per level from 10 to 60 (Blizzard hides lower levels). The median is 24; 2.9% are 60. |
| `behaviour-stats.csv` | Lifetime totals: causes of death (Normal and PvP rulesets), how players travel (flight path, hearthstone, summon, portal), what they consume (drinks, food, potions, bandages, elixirs, healthstones) and the favourite bandages. |
| `new-forever-bosses.csv` | Bosses that are new in WoW Forever, their instance, and the share of characters who killed each. |
| `legacy-achievements.csv` | The 65 achievements that award a Legacy Point (raids, dungeons, reputations, PvP ranks, Field of Honor weeks, class levels, professions). They name Forever-only content such as Hyjal Summit, the Barrow Deeps and the Darkspear Islands factions. |

## Limits

- aotc.gg reads the live Forever game. Our pinned ForeverCore build can differ; check a talent or item against the
  core before code uses it.
- These are human choices. Bots should follow the common ones, not every outlier: low `pick_pct` rows are noise.
- There is no levelling route data (zones, quests). The level groups only tell how talent points are spent.
- No raw player names or guilds are kept here.

## Refreshing

The site blocks plain HTTP clients with a bot check, so read it from a normal browser and do not try to get around
the check.

1. `python3 -I forever/tools/aotc/recv.py <raw-dir>`: a small receiver on 127.0.0.1:8799 that saves each page as JSON.
2. Open any `https://www.aotc.gg/forever/...` page, paste `forever/tools/aotc/scrape.js` into the DevTools console and
   keep the tab in front. About an hour; `window.__aotcLog` ends with `DONE`.
3. `python3 -I forever/tools/aotc/convert.py <raw-dir> forever/data/aotc forever/data/waterys/talent-trees-70959.csv`
   writes the CSV files. It also writes `talent-unmatched.csv`, which lists talents not found in our trees. That file
   should be empty; do not commit it.
