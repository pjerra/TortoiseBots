-- The three travel-node tables (TB's schema), created empty: the module builds the graph on the first start from this server's
-- own data (AiPlayerbot.GenerateTravelNodes) and saves it here. No rows ship.
CREATE TABLE IF NOT EXISTS `world`.`ai_playerbot_travelnode` (
  `id` mediumint unsigned NOT NULL,
  `name` varchar(1024) NOT NULL,
  `map_id` mediumint unsigned NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `linked` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `world`.`ai_playerbot_travelnode_link` (
  `node_id` mediumint unsigned NOT NULL,
  `to_node_id` mediumint unsigned NOT NULL,
  `type` tinyint unsigned NOT NULL DEFAULT 0,
  `object` bigint unsigned NOT NULL DEFAULT 0,
  `distance` float NOT NULL DEFAULT 0,
  `swim_distance` float NOT NULL DEFAULT 0,
  `extra_cost` float NOT NULL DEFAULT 0,
  `calculated` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_0` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_1` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_2` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`node_id`, `to_node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `world`.`ai_playerbot_travelnode_path` (
  `node_id` mediumint unsigned NOT NULL,
  `to_node_id` mediumint unsigned NOT NULL,
  `nr` mediumint unsigned NOT NULL,
  `map_id` mediumint unsigned NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  PRIMARY KEY (`node_id`, `to_node_id`, `nr`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
