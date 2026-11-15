-- Onyxia's Lair (map 249), walk 2e a-1: a second off-tank for the two Onyxian Hatchers 50144 in P3.
--
-- The (249, 50144) row sends off-tank 2 whenever a Hatcher stands on a non-tank, and one tank cannot hold two
-- Hatchers: walk 2e a-1 had one Hatcher taunt in the whole P3 (the other stayed free), 14 of 24 deaths went to the
-- Hatchers and both were still alive, at 88 % and 97 %, when the raid wiped. The row sits under boss 10184 on
-- purpose: the loader drops a row with the same kind, target, spell and health band as one it already has, and a
-- separate window key keeps both windows open (the 50144 row's subject is still the Hatcher). Needs
-- AiPlayerbot.DungeonRunAuraTactics = 1.
--
-- Idempotent: the DELETE removes only this key. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'taunt_after_cast' AND `target_entry` = 50144 AND `spell_id` = 0;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'taunt_after_cast', 50144, 0, 3, 4, '', 0, 100, 0, 'until_tank', 0, 0, 0, 0, 0, 0, 'Onyxian Hatcher, second window: off-tank 3 takes a Hatcher off a non-tank, a new window every 4 s; the 50144 row sends off-tank 2, and one tank cannot hold both (walk 2e a-1: one Hatcher taunt in P3, 14 of 24 deaths to Hatchers, both alive at the wipe)');
