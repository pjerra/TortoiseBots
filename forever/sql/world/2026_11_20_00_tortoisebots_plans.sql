-- TortoiseBots on ForeverCore: the dungeon plan tables (package H5 of slice S5).
--
-- The seven tables the run leader reads, in their FINAL form as the Turtle build has them after all its migrations
-- (data/sql/world/20261014150000 plan/leg/step, 20261021114931 boss, 20261014120000 + 20261102210000..20261113200000 boss_tactic,
-- 20261026130000 leg_need, 20261015200000 + 20261105180000 the plan unit lists). One file, CREATE TABLE IF NOT EXISTS with every
-- column, no ALTER chain: applying it twice, or on a database that already has any of the tables, changes nothing. A table that exists
-- with an older column set is NOT altered here (it never exists on a ForeverCore world database before this file).
--
-- ai_dungeon_step_unit is new and ForeverCore only: one row per member of a pull group, per boss and per boss.first member and per
-- patrol unit, with the position the plan names, so the module can find each pack member in a fresh instance by creature entry plus
-- position (`.tb plan <plan> check`) instead of by the centroid and radius of the step row. The Turtle loader never reads it.
--
-- Delivery (forever/sql/README.md): the test setup copies this folder to <SourceDirectory>/sql/tortoisebots/world and adds ONE row
-- (path $/sql/tortoisebots/world, state RELEASED) to `updates_include`; the core's updater (UpdateFetcher.cpp) applies the files in file name order.

CREATE TABLE IF NOT EXISTS `ai_dungeon_plan` (
  `plan` varchar(32) NOT NULL,
  `map` int unsigned NOT NULL,
  `cc_priority` varchar(255) NOT NULL DEFAULT '',
  `cc_always` varchar(255) NOT NULL DEFAULT '',
  `kill_first` varchar(255) NOT NULL DEFAULT '',
  `cc_never` varchar(255) NOT NULL DEFAULT '',
  `source` varchar(64) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_dungeon_leg` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `name` varchar(32) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_dungeon_step` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `seq` smallint unsigned NOT NULL,
  `kind` varchar(16) NOT NULL,
  `entry` int unsigned NOT NULL DEFAULT 0,
  `x` float NOT NULL DEFAULT 0,
  `y` float NOT NULL DEFAULT 0,
  `z` float NOT NULL DEFAULT 0,
  `radius` float NOT NULL DEFAULT 0,
  `item` int unsigned NOT NULL DEFAULT 0,
  `param` int unsigned NOT NULL DEFAULT 0,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `timeout_s` smallint unsigned NOT NULL DEFAULT 0,
  `entries` varchar(255) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_dungeon_step_unit` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `seq` smallint unsigned NOT NULL,
  `idx` tinyint unsigned NOT NULL,
  `entry` int unsigned NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `role` varchar(8) NOT NULL DEFAULT 'pack',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`, `seq`, `idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_dungeon_leg_need` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `item` int unsigned NOT NULL,
  `mode` tinyint unsigned NOT NULL,
  `pick` smallint unsigned NOT NULL DEFAULT 0,
  `object` int unsigned NOT NULL DEFAULT 0,
  `name` varchar(64) NOT NULL DEFAULT '',
  `how` varchar(255) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`, `item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_dungeon_boss` (
  `map` int unsigned NOT NULL,
  `entry` int unsigned NOT NULL,
  `name` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`map`, `entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ai_boss_tactic` (
  `map` int unsigned NOT NULL,
  `boss_entry` int unsigned NOT NULL,
  `kind` varchar(16) NOT NULL,
  `target_entry` int unsigned NOT NULL DEFAULT 0,
  `spell_id` int unsigned NOT NULL DEFAULT 0,
  `priority` tinyint unsigned NOT NULL DEFAULT 0,
  `param` float NOT NULL DEFAULT 0,
  `burn_order` varchar(96) NOT NULL DEFAULT '',
  `hp_min` tinyint unsigned NOT NULL DEFAULT 0,
  `hp_max` tinyint unsigned NOT NULL DEFAULT 100,
  `bot_aura` int unsigned NOT NULL DEFAULT 0,
  `tactic_mode` varchar(16) NOT NULL DEFAULT '',
  `window_s` float NOT NULL DEFAULT 7,
  `cycle_spell` int unsigned NOT NULL DEFAULT 0,
  `cycle_s` float NOT NULL DEFAULT 0,
  `spot_x` float NOT NULL DEFAULT 0,
  `spot_y` float NOT NULL DEFAULT 0,
  `spot_z` float NOT NULL DEFAULT 0,
  `healers` tinyint unsigned NOT NULL DEFAULT 0,
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `hp_min`, `hp_max`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
