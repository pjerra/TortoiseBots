-- Onyxia's Lair (map 249), walk 2o-2p: the P3 damage crew stops burning the Hatchers and stays on her. Data only.
--
-- Walks 2o (12 pulls), 2o2 (7) and 2p (11), roster-matched frames: the Hatchers 50144 (233,512 HP each, two per P3)
-- take melee 7 -> 26 -> 41 % and ranged 9 -> 30 -> 36 % of the dps frames at 0-30 / 30-60 / 60-90 s after the landing,
-- yet lose a median 4.2 % a minute and are last seen at a median 87 % health: that damage was wasted. Damage on
-- Onyxia per frame falls 12.9 (0-20 s) -> 8.9 -> 5.7 -> 2.9 (60-80 s) and she loses 6.8 % a minute at 0-30 s, then
-- 4.9, 2.3 and about nothing after 90 s. The rows: `kill_first` on 50144 (priority 1, run at ACTION_HIGH+1 like the
-- whelp hunt) pulls the melee crew off the whelps, and `hold_kill` (10184 <- 50144) stops every free damage dealer's
-- work on her while a Hatcher fights within 50 yd of the bot, which is most of the arena.
--
-- What stays: the two Hatcher posts (slots 1-2 on 50144, 20261112100000/20261112200000), both Hatcher
-- taunt_after_cast rows (20261109200100/20261111170100) and the Hatcher `behind` row. The posts kill the Hatchers
-- (300 s despawn) while the crew keeps her.
--
-- The new hold_kill and its role: a hold_kill row's boss_entry is the unit the damage dealers leave and its
-- target_entry the blocker they finish first. So (50144, 10184) holds the Hatchers while Onyxia herself is within 50 yd
-- of the bot, and the tank exemption is the hold's own (a tank or a healer keeps its pick: DpsTargetValue.cpp's role
-- gate and IsFreeDps).
--
-- The landing row: a dps_hold_after row on her in the 0-40 band, mode until_tank, 5 s (a hold of up to 15 s while she
-- is on nobody but a tank, the engine's 3 * param cap). Its band is shut through P1/P2, so the hold starts at the
-- landing, not at the first sighting: the band's opening is the row's trigger.
--
-- Idempotent: the three DELETEs name only the rows this file replaces, and the two INSERTs follow them, so a re-run
-- ends with the same table. Never edit this file once applied; add a newer migration.
--
-- Rollback: DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 50144 AND `kind` = 'hold_kill' AND
--   `target_entry` = 10184;
--   DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'dps_hold_after' AND
--   `hp_max` = 40;
--   then re-insert the rows of 20261109200100 (kill_first 50144) and 20261111170200 (hold_kill 10184 <- 50144).

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'hold_kill' AND `target_entry` = 50144;
DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 50144 AND `kind` IN ('kill_first', 'hold_kill');
DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'dps_hold_after' AND `hp_max` = 40;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 50144, 'hold_kill', 10184, 0, 0, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxian Hatcher: DPS leave both Hatchers (233k HP, 300 s despawn) to their post tanks while Onyxia fights within 50 yd (walks 2o-2p: 30-40 % of P3 dps frames on them, median 87 % hp left, Onyxia 6.8 -> 1.2 %/min)'),
(249, 10184, 'dps_hold_after', 0, 0, 0, 5, '', 0, 40, 0, 'until_tank', 0, 0, 0, 0, 0, 0, 'Onyxia P3 landing: her threat list is wiped, so DPS hold until she is on a tank (cap 15 s); walks 2o-2p: on a non-tank 27 % / no target 34 % of the first 20 s');
