-- The resist-gear hold's state (TBF #20): `releasing` = 0 means the set is on and nothing is due (HELD); 1 means a
-- restore was asked for and did not finish (RELEASING), so the world tick retries it and a restart finishes it.
-- The module's own CREATE already writes the column; this file upgrades a table created by the first version of
-- forever/sql/characters/2026_11_24_11_tb_resist_hold.sql. Idempotent: the ALTER runs only when the table exists without
-- the column.

SET @tb_resist_table := (SELECT COUNT(*) > 0 FROM information_schema.TABLES
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_resist_hold');
SET @tb_resist_state := (SELECT COUNT(*) = 0 FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_resist_hold' AND COLUMN_NAME = 'releasing');

SET @s := IF(@tb_resist_table AND @tb_resist_state,
  'ALTER TABLE `tortoise_bots_resist_hold` ADD COLUMN `releasing` tinyint unsigned NOT NULL DEFAULT 0 AFTER `resist_guid`',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
