-- TortoiseBots on ForeverCore: guild order board table (W9).
--
-- Matches all 44 columns of runtime/GuildOrderStoreColumns.h (`StoreColumns::kSelect`).
-- Read at startup through `CharacterSchemaHasCompat` + `CharacterQueryGuardedCompat`
-- (forever/src/compat/TbCompat_CharStore.h); if the table or any column is missing,
-- the store logs one warning and disables persistence (does less, never aborts).
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_order` (
  `bot_guid` int unsigned NOT NULL,
  `guild_id` int unsigned NOT NULL,
  `giver_guid` int unsigned NOT NULL,
  `parent_bot` int unsigned NOT NULL DEFAULT '0',
  `root_giver` int unsigned NOT NULL DEFAULT '0',
  `type` varchar(16) NOT NULL DEFAULT 'farm',
  `item_id` int unsigned NOT NULL,
  `amount` int unsigned NOT NULL,
  `hours` int unsigned NOT NULL DEFAULT '1',
  `delivered` int unsigned NOT NULL DEFAULT '0',
  `state` varchar(16) NOT NULL DEFAULT 'working',
  `reason` varchar(24) NOT NULL DEFAULT '-',
  `farm_zone` int unsigned NOT NULL DEFAULT '0',
  `farm_chance` tinyint unsigned NOT NULL DEFAULT '0',
  `farm_item` int unsigned NOT NULL DEFAULT '0',
  `zone_index` int unsigned NOT NULL DEFAULT '0',
  `deaths_in_zone` int unsigned NOT NULL DEFAULT '0',
  `worked_sec` int unsigned NOT NULL DEFAULT '0',
  `worked_total` int unsigned NOT NULL DEFAULT '0',
  `worked_nogain` int unsigned NOT NULL DEFAULT '0',
  `reported` int unsigned NOT NULL DEFAULT '0',
  `farm_rest` tinyint unsigned NOT NULL DEFAULT '0',
  `keep` tinyint unsigned NOT NULL DEFAULT '0',
  `gold_cap` int unsigned NOT NULL DEFAULT '0',
  `gold_cap_set` tinyint unsigned NOT NULL DEFAULT '0',
  `guild_gold_withdrawn` int unsigned NOT NULL DEFAULT '0',
  `guild_gold_held` int unsigned NOT NULL DEFAULT '0',
  `cancelled` tinyint unsigned NOT NULL DEFAULT '0',
  `supply_step` varchar(24) NOT NULL DEFAULT 'none',
  `supply_return_step` varchar(24) NOT NULL DEFAULT 'none',
  `supply_item` int unsigned NOT NULL DEFAULT '0',
  `supply_count` int unsigned NOT NULL DEFAULT '0',
  `supply_gold_count` int unsigned NOT NULL DEFAULT '0',
  `need_items` varchar(1024) NOT NULL DEFAULT '',
  `need_whispered` tinyint unsigned NOT NULL DEFAULT '0',
  `last_need_whisper` varchar(1024) NOT NULL DEFAULT '',
  `need_bank_denied` tinyint unsigned NOT NULL DEFAULT '0',
  `stock_target` int unsigned NOT NULL DEFAULT '0',
  `last_bank_check` int unsigned NOT NULL DEFAULT '0',
  `created_at` bigint unsigned NOT NULL DEFAULT '0',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0',
  `carried` int unsigned NOT NULL DEFAULT '0',
  `bring` varchar(1024) NOT NULL DEFAULT '',
  `given` varchar(1024) NOT NULL DEFAULT '',
  `ah_gold_spent` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`bot_guid`),
  KEY `idx_guild` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
