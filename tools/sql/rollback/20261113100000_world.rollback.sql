-- Rollback of data/sql/world/20261113100000_world.sql (Hakkar's Sons: no damage until a tank has held one, r13).
--
-- Run this file by hand to turn r13's data off. This file lives outside data/sql, so the migration runner never applies
-- it. Run the forward file by hand to turn it on again.
--
-- It deletes the same key and inserts nothing: before the migration the row did not exist.
-- Idempotent.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'tank_first' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
