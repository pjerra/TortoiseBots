-- TortoiseBots on ForeverCore: the pool registry's rows carry what proves they are still about the module's account (world slice W0,
-- fix round of round W0-A of FOREVER-W0-W4-SPEC.md).
--
-- 2026_11_20_01_tb_pool.sql created the first shape of the two tables (round W0-A). This file upgrades it, and creates the current
-- shape on a database that has neither table:
--   `tb_pool_account`   keyed by `username`, with `stamp` (the random creation token, written as the account's e-mail) and `state`
--                       (`creating` | `ready` | `stale`);
--   `tb_pool_character` keyed by `name`, with a nullable `guid` (the name is the pool's reservation before the character exists)
--                       and `state`.
-- The module's own CREATE TABLE IF NOT EXISTS statements in forever/src/host/pool/TbPoolRegistry.cpp are the same shape, so an
-- enabled server needs this file only when it already has the old tables.
--
-- Rows of the old shape carry no stamp, so they cannot be proven the module's: they are set to `stale` and are never used or
-- touched again (their accounts and characters stay exactly as they are). A row whose character no longer exists is removed from
-- the character table (a registry row, never a game character).
--
-- Idempotent: every step asks information_schema first, with the PREPARE pattern of the repository's other migrations (no stored
-- procedure and no DELIMITER: the updater has no support for either); a second run changes nothing. What a step does only
-- because a column was missing (the `stale` marks, the name back-fill) is decided once, into a variable, before the ALTER that
-- adds the column. Apply on the characters database:
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_20_02_tb_pool_rows.sql

-- The current shape of both tables on a database that has neither (the same text as the module's own statements).
CREATE TABLE IF NOT EXISTS `tb_pool_account` (
  `username` varchar(16) NOT NULL,
  `account_id` int unsigned DEFAULT NULL,
  `stamp` char(16) NOT NULL DEFAULT '',
  `state` varchar(8) NOT NULL DEFAULT 'creating',
  `created` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`username`),
  UNIQUE KEY `account_id` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `tb_pool_character` (
  `name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `guid` bigint unsigned DEFAULT NULL,
  `account_id` int unsigned NOT NULL,
  `race` tinyint unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `gender` tinyint unsigned NOT NULL,
  `state` varchar(8) NOT NULL DEFAULT 'creating',
  `created` bigint NOT NULL DEFAULT '0',
  `last_login` bigint NOT NULL DEFAULT '0',
  `last_logout` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`name`),
  UNIQUE KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- What the first round's tables lack, asked once, before anything changes (a later step must not re-ask a question an
-- earlier step of this file has already changed the answer to).
SET @tb_acct_old := (SELECT COUNT(*) = 0 FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tb_pool_account' AND COLUMN_NAME = 'stamp');
SET @tb_acct_pk := (SELECT COUNT(*) > 0 FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tb_pool_account' AND INDEX_NAME = 'PRIMARY' AND COLUMN_NAME = 'account_id');
SET @tb_char_old := (SELECT COUNT(*) = 0 FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tb_pool_character' AND COLUMN_NAME = 'name');
SET @tb_char_pk := (SELECT COUNT(*) > 0 FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tb_pool_character' AND INDEX_NAME = 'PRIMARY' AND COLUMN_NAME = 'guid');

-- tb_pool_account: the stamp and the state.
SET @s := IF(@tb_acct_old,
  'ALTER TABLE `tb_pool_account` ADD COLUMN `stamp` char(16) NOT NULL DEFAULT '''' AFTER `account_id`, ADD COLUMN `state` varchar(8) NOT NULL DEFAULT ''creating'' AFTER `stamp`',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Rows of the old shape carry no stamp: they are stale. Keyed on the table still being old-keyed (@tb_acct_pk) as well as on the
-- column having just been added, so a run that stopped after the ALTER is repaired by a rerun.
SET @s := IF(@tb_acct_old OR @tb_acct_pk, 'UPDATE `tb_pool_account` SET `state` = ''stale'' WHERE `stamp` = ''''', 'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- tb_pool_account: keyed by the username, the id nullable and unique.
SET @s := IF(@tb_acct_pk,
  'ALTER TABLE `tb_pool_account` DROP PRIMARY KEY, MODIFY `account_id` int unsigned DEFAULT NULL, ADD PRIMARY KEY (`username`), ADD UNIQUE KEY `account_id` (`account_id`)',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- tb_pool_character: the name (the pool's reservation) and the state.
SET @s := IF(@tb_char_old,
  'ALTER TABLE `tb_pool_character` ADD COLUMN `name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '''' AFTER `guid`, ADD COLUMN `state` varchar(8) NOT NULL DEFAULT ''creating'' AFTER `gender`',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- The steps below are keyed on the table still being keyed by `guid` (@tb_char_pk), not on the column having just been added: a
-- run that stopped after the ALTER above (columns there, rows still name '' / state creating) is repaired by a rerun, and a
-- table that is already the current shape is never touched.
-- Names come from the game characters.
SET @s := IF(@tb_char_pk,
  'UPDATE `tb_pool_character` c JOIN `characters` ch ON ch.`guid` = c.`guid` SET c.`name` = ch.`name` WHERE c.`name` = ''''',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- A row with no name (its character no longer exists, or the core soft-deleted it and blanked the name) is a registry row, never
-- a game character: it is removed, so the primary key on `name` never meets a duplicate empty name.
SET @s := IF(@tb_char_pk, 'DELETE FROM `tb_pool_character` WHERE `name` = ''''', 'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- The rest of the old rows carry no proof: stale.
SET @s := IF(@tb_char_pk, 'UPDATE `tb_pool_character` SET `state` = ''stale''', 'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- tb_pool_character: keyed by the name, the guid nullable and unique.
SET @s := IF(@tb_char_pk,
  'ALTER TABLE `tb_pool_character` DROP PRIMARY KEY, MODIFY `guid` bigint unsigned DEFAULT NULL, ADD PRIMARY KEY (`name`), ADD UNIQUE KEY `guid` (`guid`)',
  'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
