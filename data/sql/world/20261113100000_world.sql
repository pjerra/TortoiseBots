-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — no damage on a Son of Hakkar until a tank
-- has held it (r13). Evidence: 215 Hakkar fights of 60 s or longer (walks 77, 81-87, step 689), 25 kills (11.6 %).
--
--   Son melee is 2206 of 4344 member deaths, and the first divergence in 126 of 190 wipes (66 %). In healthy pulls
--   (pulls 1-3, 16 or more alive, 3 tanks; 462 pulls) 202 Sons die before the tank take and 829 after it, a median 18.6 s
--   after the take (25 % within 10 s): four fifths of the deaths happen after a tank "took" the Son, because the burn
--   starts at once and the damage dealers pass the tank. The take comes a median 4 s after the Son joins the fight (p95
--   11.3 s) and the Son dies a median 22 s after the take (p90 33 s).
--
--   The dwell is 3 s: the damage dealers' first casts are off the tank's threat, so three seconds of the add on a tank
--   is what makes the take hold. A hold of one episode lasts 15 s (window_s) because the take comes within 11.3 s of the
--   Son joining the fight in 95 % of healthy pulls, so the cap ends under 5 % of arrival holds; a released Son is today's
--   behaviour, never worse. Two losses: each held loss can cost up to 15 s, so two bound the delay at 45 s against a
--   walk-out that starts 60-70 s before a Blood Siphon, and the last 15 s before the siphon releases the Son anyway.
--
--   AiPlayerbot.DungeonRunAddTankFirst (default 0) reads this row; off, the fight behaves as today.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'tank_first' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'tank_first', 11357, 0, 2, 3, '', 15, 0, 0, 0, 0, 0, 'Hakkar: no damage on a Son of Hakkar until a tank has been its victim for 3 s, and again after up to 2 losses; a hold ends after 15 s without a tank');
