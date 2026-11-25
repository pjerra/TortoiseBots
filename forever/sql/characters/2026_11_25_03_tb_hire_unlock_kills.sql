-- Hire contract kills: one row per boss a player has killed toward a hire-unlock contract (TbHireTiers.cpp). The
-- module also runs this CREATE itself, so a server that never applied the file still stores kills when its database
-- user may create tables; a missing table simply means no progress is restored after a quest is retaken.
--
-- `boss_entry` is the encounter key, not necessarily the killed creature: the Four Horsemen store 16062 (one boss).
-- Read at login through the guarded characters helpers, written once per boss, deleted with the character.
CREATE TABLE IF NOT EXISTS `tortoise_bots_hire_unlock_kills` (
  `guid` bigint unsigned NOT NULL,
  `boss_entry` int unsigned NOT NULL,
  `killed_at` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`, `boss_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
