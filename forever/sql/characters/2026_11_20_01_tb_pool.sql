-- TortoiseBots on ForeverCore: the opt-in bot pool registry (world slice W0, round W0-A of FOREVER-W0-W4-SPEC.md; decision D2).
--
-- `tb_pool_account` holds the accounts the module itself created for the pool (`TBPOOL0001` ...), and `tb_pool_character` the
-- characters it created on them. Only a registered account or character is ever touched: the pool never looks at an account the
-- module did not make. The password of a pool account is random and is not stored anywhere (a pool bot is only ever driven by the
-- host's own headless session), and `username` is kept so a row can be matched to `AccountMgr` without reading the auth database.
--
-- `last_login` / `last_logout` are unix seconds, written by the scheduler (round W0-B): the login order is the oldest logout
-- first, and the offline window is counted from `last_logout`.
--
-- The module runs these same CREATE TABLE IF NOT EXISTS statements itself, once, when TortoiseBots.Pool.Enable = 1 and the
-- characters-database user may create tables (forever/src/host/pool/TbPoolRegistry.cpp). A server that applies this file by hand
-- (or through the characters updater) needs no such privilege; with a table missing, an enabled server logs one error line and
-- the pool is off (it does less, never the core's abort on a query naming a missing table).
--
-- Applying it by hand on the build VM's test server, from the module folder (characters database, `forever-db` container):
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_20_01_tb_pool.sql
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `tb_pool_account` (
  `account_id` int unsigned NOT NULL,
  `username` varchar(16) NOT NULL,
  `created` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `tb_pool_character` (
  `guid` bigint unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `race` tinyint unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `gender` tinyint unsigned NOT NULL,
  `created` bigint NOT NULL DEFAULT '0',
  `last_login` bigint NOT NULL DEFAULT '0',
  `last_logout` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
