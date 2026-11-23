-- TortoiseBots on ForeverCore: the guild relist ledger (TBF #155).
--
-- One row per auction-house surplus a guild order posted for the guild: a
-- sale notification pays its gold into the bot's hands later, an expired one
-- returns the stack, often after the order is gone. The row keeps that value
-- attached to the guild until the bank trip pays it back. `state` is 0 posted,
-- 1 claimed (the row names the settlement mail and the value it promises) and
-- 2 received (the value is in the bot's hands, waiting for the banker).
-- Read and written through the guarded characters helpers in
-- forever/src/compat/TbCompat_CharStore.h; if the table or a column is
-- missing, the ledger logs one warning and stays off (does less, never
-- aborts). The module also runs this CREATE itself when its database user may
-- create tables.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist` (
  `bot_guid` int unsigned NOT NULL,
  `seq` int unsigned NOT NULL,
  `guild_id` int unsigned NOT NULL,
  `item_id` int unsigned NOT NULL,
  `count` int unsigned NOT NULL,
  `unit_price` int unsigned NOT NULL,
  `bid` int unsigned NOT NULL DEFAULT '0',
  `buyout` int unsigned NOT NULL DEFAULT '0',
  `deposit` int unsigned NOT NULL DEFAULT '0',
  `posted_at` int unsigned NOT NULL DEFAULT '0',
  `expires_at` int unsigned NOT NULL DEFAULT '0',
  `state` tinyint unsigned NOT NULL DEFAULT '0',
  `claim_mail` int unsigned NOT NULL DEFAULT '0',
  `held_gold` int unsigned NOT NULL DEFAULT '0',
  `held_count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`bot_guid`, `seq`),
  KEY `guild_id` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
