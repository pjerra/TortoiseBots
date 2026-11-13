-- Rollback of data/sql/world/20261113150000_world.sql (Hakkar's Sons: the tank-first dwell back to 3 s, r13 round 2).
--
-- Run this file by hand to undo the round-2 dwell change. This file lives outside data/sql, so the migration runner
-- never applies it. Run the forward file by hand to apply it again.
--
-- It restores the values 20261113100000_world.sql wrote, on that file's exact key. Idempotent.

UPDATE `ai_boss_tactic` SET `param` = 3,
    `comment` = 'Hakkar: no damage on a Son of Hakkar until a tank has been its victim for 3 s and again after up to 2 losses; a hold ends after 15 s without a tank'
WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'tank_first' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
