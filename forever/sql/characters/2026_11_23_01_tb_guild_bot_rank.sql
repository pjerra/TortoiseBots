-- TortoiseBots on ForeverCore: per-guild bot rank for guild bank access (TBF #155).
--
-- One row per guild whose player picked a rank with `.tb guildorder rank <rank>`; `bot_rank` is the rank's
-- place in the guild (0 = master, which is never chosen, through the lowest rank). `updated_at` is the
-- unix time of the last change. Guilds without a row use their lowest rank. The on/off switch is
-- TortoiseBots.World.GuildOrderBotRank, not this table.
-- Read at startup through `CharacterSchemaHasCompat` + `CharacterQueryGuardedCompat`
-- (forever/src/compat/TbCompat_CharStore.h); the module also runs this CREATE itself, so a server without
-- this file still works when its database user may create tables.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank` (
  `guild_id` int unsigned NOT NULL,
  `bot_rank` tinyint unsigned NOT NULL DEFAULT '1',
  `updated_at` bigint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
