-- TortoiseBots on ForeverCore: custom strategy storage (piece P135-C, TBF #135).
--
-- TB persists per-owner and global (owner = 0) custom strategy lines in characters.`ai_playerbot_custom_strategy`
-- (name, idx, owner, action_line), ordered by idx (ai/playerbot/strategy/CustomStrategy.cpp, CustomStrategyEditAction.cpp).
--
-- Schema: TB's (TortoiseBots/data/sql/character/20260824090001_char.sql:62-70), utf8mb4 like the module's other tables.
-- The module also runs the same CREATE TABLE IF NOT EXISTS itself once at the first load/edit
-- (forever/src/slice/SliceCustomStrategy.cpp), so a server that applies this file by hand (or through the characters
-- updater) needs no such privilege; with the table missing, an enabled server logs one warning and loads no lines.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `ai_playerbot_custom_strategy` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `idx` bigint DEFAULT NULL,
  `owner` bigint DEFAULT NULL,
  `action_line` varchar(1024) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
