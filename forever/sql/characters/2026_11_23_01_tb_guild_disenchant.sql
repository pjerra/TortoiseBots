-- TortoiseBots on ForeverCore: the disenchant service queue (TBF #155).
--
-- One row per item a guild member mailed to a bot with the subject
-- `disenchant`: which member it goes back to, the item, and the loot its cast
-- landed once the service has collected it. The service deletes the row when
-- the results (or the item, with a reason) are mailed back. Read and written
-- through the guarded characters helpers in
-- forever/src/compat/TbCompat_CharStore.h; if the table or a column is
-- missing, the service logs one warning and stays off (does less, never
-- aborts).
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_disenchant` (
  `bot_guid` int unsigned NOT NULL,
  `sender_guid` int unsigned NOT NULL,
  `item_guid` bigint unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `item_name` varchar(128) NOT NULL DEFAULT '',
  `results` varchar(512) NOT NULL DEFAULT '',
  `pending` tinyint unsigned NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`bot_guid`, `item_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
