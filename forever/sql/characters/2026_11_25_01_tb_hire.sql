-- TortoiseBots on ForeverCore: the hired-companion ledger (package P1 of the hire port).
--
-- One row per hire, written before the hire is charged and deleted together with the character. It is the proof that a
-- character was created by a hire: `HireLifecycle::ProcessDeletion` refuses to delete any character without this row, and
-- a row whose character is gone is removed. `state` moves `active` -> `dismissed` when the hire ends (the durable intent a
-- crash or restart recovers); `character_account_id` is the hire account the module made, and deletion also requires the
-- account to still carry that account id.
--
-- No hire survives a restart: every row a fresh process finds is recovered and its character deleted.
--
-- The module runs this same CREATE TABLE IF NOT EXISTS itself when the characters-database user may create tables
-- (forever/src/host/hire/TbHireLifecycle.cpp). Applying it by hand on the build VM's test server, from the module folder:
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_25_01_tb_hire.sql
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `tb_hire` (
  `character_guid` bigint unsigned NOT NULL,
  `master_guid` bigint unsigned NOT NULL,
  `owner_account_id` int unsigned NOT NULL,
  `character_account_id` int unsigned NOT NULL,
  `gear_tier` tinyint unsigned NOT NULL DEFAULT '0',
  `state` varchar(16) NOT NULL DEFAULT 'active',
  `hired_at` bigint NOT NULL DEFAULT '0',
  `dismissed_at` bigint DEFAULT NULL,
  PRIMARY KEY (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
