-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — the tank-first dwell, 3 s to 2 s (r13,
-- round 2). Walk 91 (22 Hakkar fights of 60 s or longer, 0 kills, stopped on two hold lines):
--
--   The take came a median 4 s after the Son joined the fight and the dwell asked 3 s more on top, while the hold's cap
--   is 15 s from the first sighting: a take that landed at 12-14 s could not fit its dwell and the hold ended on the cap
--   (19 of 47 arm segments, 8 of them still in `wait`, 7 in `dwell`). The take's taunt rows said `range` 209 of 322
--   refusals: the tank chased to melee through the boss anchor's tank circle and the taunt was never in reach, so most
--   settled takes came as `how=threat` and the Son lived a median 82 s.
--
--   Round 2 walks a leased take to the Son and casts the taunt there, settles the dwell at once on a landed taunt by that
--   very tank, sums the dwell's tank time across sub-second flips instead of restarting it, and gives a running dwell the
--   dwell's own time on top of the cap (17 s); the dwell itself drops to 2 s so a late take still settles.
--
--   AiPlayerbot.DungeonRunAddTankFirst (default 0) reads this row; off, the fight behaves as today.
--
-- This file only moves `param` (and the comment that repeats it); the row and its key are 20261113100000_world.sql's,
-- named here exactly as that file's DELETE names them. Never edit an applied file: a later change is a newer migration.
-- Idempotent: the UPDATE sets the same values however often it runs.

UPDATE `ai_boss_tactic` SET `param` = 2,
    `comment` = 'Hakkar: no damage on a Son of Hakkar until a tank has been its victim for 2 s and again after up to 2 losses; a hold ends after 15 s without a tank'
WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'tank_first' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
