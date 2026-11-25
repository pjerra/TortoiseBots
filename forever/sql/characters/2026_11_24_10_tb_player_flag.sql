-- TortoiseBots on ForeverCore: per-character "told once" flags (TBF #46).
--
-- `flag = 'addon-hint'` means the character was told where to get the
-- TortoiseBotsForeverManager add-on, or was seen with it (one TBM message in a
-- session). The add-on hint is sent only to a real player with no row, so this
-- row is what keeps it once across relogs and server restarts; `.tb addonhint
-- reset <character>` deletes it. The on/off switch is TortoiseBots.Player.AddonHint,
-- not this table.
-- Read at startup through `CharacterSchemaHasCompat` + `CharacterQueryGuardedCompat`
-- (forever/src/compat/TbCompat_CharStore.h); the module also runs this CREATE itself,
-- so a server without this file still works when its database user may create tables.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `tortoise_bots_player_flag` (`guid` bigint unsigned NOT NULL, `flag` varchar(32) NOT NULL, `set_at` int unsigned NOT NULL DEFAULT '0', PRIMARY KEY (`guid`, `flag`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
