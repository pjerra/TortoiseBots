-- Onyxia's Lair (map 249), walk 2e a-1: the P3 kill order, damage off Onyxia 10184 while a Hatcher 50144 fights.
--
-- Walk 2e a-1 entered P3 with both Hatchers alive and killed nobody: in t 775-815 s the ranged bots spent 69 frames
-- on a Hatcher, 58 on Onyxia and 24 on a whelp, and the Hatchers still stood at 88 % and 97 % when the raid wiped
-- (14 of 24 P3 deaths were theirs). A living Hatcher within 50 yd now holds every damage dealer off her; the tanks
-- and the healers are not held, and the hold needs only the "boss tactics" strategy. Her threat list is wiped at the
-- landing, so the tanks also hold the lead on her afterwards.
--
-- Idempotent: the DELETE removes only this key. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'hold_kill';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'hold_kill', 50144, 0, 0, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia P3: no DPS on her while a Hatcher fights within 50 yd of the bot; her threat list is wiped at the landing, so the tanks also get the lead on her (walk 2e a-1: ranged frames 69 Hatcher / 58 Onyxia / 24 whelp, Hatchers at 88 % and 97 % at the wipe)');
