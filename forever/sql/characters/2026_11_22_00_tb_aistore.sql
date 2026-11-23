-- TortoiseBots on ForeverCore: the VALUES half of the AI store (piece c-store, TBF #133).
--
-- TB persists every saved AI value (the qualified "manual saved string" choices: totem/blessing/choice keys, plus
-- formation, rti, wait-for-attack time and the other saved values) in characters.`ai_playerbot_db_store`
-- (guid, preset, key, value), one row per value with `key` = 'value' (ai/playerbot/PlayerbotDbStore.cpp).
-- The strategy lists (`co`/`nc`/`dead`/`react` rows) are #135's and are neither written nor read yet.
--
-- Schema: TB's (TortoiseBots/data/sql/character/20260824090001_char.sql:52-60), utf8mb4 like the module's other tables
-- (choice values hold player names, which are UTF-8). The module runs the same CREATE TABLE IF NOT EXISTS itself once
-- at the first load/save (forever/src/slice/SliceDbStore.cpp), so a server that applies this file by hand (or through
-- the characters updater) needs no such privilege; with the table missing, an enabled server logs one line and stores
-- nothing (it does less, never the core's abort on a query naming a missing table).
--
-- Applying it by hand on the build VM's test server, from the module folder (characters database, `forever-db` container):
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_22_00_tb_aistore.sql
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `ai_playerbot_db_store` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `guid` bigint unsigned NOT NULL,
  `preset` varchar(32) NOT NULL,
  `key` varchar(32) NOT NULL,
  `value` varchar(4000) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
