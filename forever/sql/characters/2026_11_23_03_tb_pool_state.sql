-- TortoiseBots on ForeverCore: the pool reset's one row (the startup pool reset).
--
-- `applied_generation` is the `once:<token>` value the last completed reset recorded, compared as data (never a
-- keyword); `completed_at` says when the last reset finished, in either mode. The generation is written only after
-- the characters table proved every target gone, so a crash mid-reset leaves it unrecorded and the next start plans
-- again. `always` never overwrites a stored token.
--
-- The module runs this same CREATE TABLE IF NOT EXISTS itself when TortoiseBots.Pool.Reset is on, so a server whose
-- user may not create tables needs this file applied by hand:
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_23_03_tb_pool_state.sql
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `tb_pool_state` (
  `pool_name` varchar(32) NOT NULL,
  `applied_generation` varchar(128) NOT NULL DEFAULT '',
  `completed_at` datetime DEFAULT NULL,
  PRIMARY KEY (`pool_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
