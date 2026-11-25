CREATE TABLE IF NOT EXISTS `tortoise_bots_resist_hold` (
  `guid` bigint unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `school` tinyint unsigned NOT NULL,
  `own_guid` bigint unsigned NOT NULL DEFAULT '0',
  `resist_guid` bigint unsigned NOT NULL,
  PRIMARY KEY (`guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
