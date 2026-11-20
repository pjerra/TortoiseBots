-- TortoiseBots on ForeverCore: the opt-in commander binding table (package P2 of the commander port; decision D3 of
-- FOREVER-COMMANDER-PORT-SPEC.md).
--
-- One row per commander. A character guid is the whole key: no group id and no group counter is stored, because the core reuses
-- its database group ids (GroupMgr::FreeGroupDbStoreId) and a stale number would name another party. The live group is always
-- found again through the lead (characters.guid of lead_guid). `members` is a comma-separated list of character guid counters,
-- the lead first; `flags` is the binding's kind (1 = in-party, 2 = raid, 4 = player-led, 8 = the server's own system binding).
-- `bound_at` is the unix second the binding was first seen.
--
-- The module runs this same CREATE TABLE IF NOT EXISTS itself, once, when TortoiseBots.Commander.KeepBindings = 1 and the
-- characters-database user may create tables (forever/src/host/cmd/TbCmdBindingStore.cpp). A server that applies this file by hand
-- (or through the characters updater) needs no such privilege; with the table missing, an enabled server logs one line and stores
-- nothing (it does less, never the core's abort on a query naming a missing table).
--
-- Applying it by hand on the build VM's test server, from the module folder (characters database, `forever-db` container):
--   docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" characters < forever/sql/characters/2026_11_20_00_tb_commander_binding.sql
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, no ALTER, no row is touched.

CREATE TABLE IF NOT EXISTS `tb_commander_binding` (
  `commander_guid` bigint unsigned NOT NULL,
  `lead_guid` bigint unsigned NOT NULL,
  `members` text NOT NULL,
  `flags` tinyint unsigned NOT NULL DEFAULT '0',
  `bound_at` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`commander_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
