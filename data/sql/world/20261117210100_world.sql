-- Onyxia's Lair (map 249), walk 2q: a P3 healer stack (anchor 10) on her two flanks, behind a new health gate.
--
-- Owner rule from walk 2n: "the healers should gather since they pull the aggro for the whelps, and dps should aoe on
-- the healers." P2 has that stack (anchor 9, Hover-aura gated, 20261116120000); P3 has none because a stand row
-- cannot tell P1 from P3 on the aura alone. This adds the health gate the code needs: `boss_hp_max` on
-- `ai_boss_stand`, read only with AiPlayerbot.BossStandHpGate (default 0). A row whose boss_hp_max is below 100
-- applies only while the boss is at or below that health; with the key at 0 such a row never applies.
--
-- Walk 2o-2p, P3: healers spread 18 yd from their centroid (7 yd in P2), die first (first healer death median 47 s,
-- 4.3 deaths a pull-minute against 0.23 in P2), 24 % of them stand within 40 yd of her in her front 60 deg, and 78 %
-- of the whelp-on-healer frames have no bot on that whelp.
--
-- The seats: anchor 10 at (-22.0, -213.5), radius 60 (the P2 stack's anchor, so the stack reads the same place), eight
-- points, one bot a point (`slots` 1: kBossStandSpacing lines all the slots of a point up 9 yd apart), no window
-- spell, no aura, side 0, hold 8000, no release anchor, signal 'cast', boss_hp_max 40. Seats in seq order, alternating
-- north/south so any healer count splits evenly:
--   0 N (2.5,-185.5) -86.0   1 S (2.5,-243.5) -85.9   2 N (7.5,-185.5) -85.6   3 S (7.5,-243.5) -85.6
--   4 N (2.5,-190.5) -86.2   5 S (2.5,-248.5) -85.2   6 N (7.5,-190.5) -85.7   7 S (7.5,-248.5) -85.1
-- The centres N (5,-188) and S (5,-246) stand 33.5 / 31.8 yd from the main tank's spot E (20,-218), 34 yd from the
-- Hatcher posts (-29,-183.8) and (-29,-245.7), 28 / 36 yd median from her P3 position, 73-87 deg off her facing to E
-- (her flank, out of the front cone and the tail), 37 yd from the whelp spawns, and east of x 2 (outside the egg
-- field, whose east edge is x -32.6).
--
-- z from the offline path-check (turtle-agent path-check over the server mmaps, both walk STARTs
-- -8.9 -212.8 -88.5 and -65.8 -213.8 -84.3, the probe of ony_breath_seats-2b.py): every seat offline complete, server
-- verdict ok, short path <= 1.4 yd, end within 1.7 yd, floor z in -93..-80, from both starts; no point had to move.
--
-- The column: added through information_schema.COLUMNS + PREPARE (MySQL 8 and MariaDB) only when it is missing.
--
-- Known limit: these rows need AiPlayerbot.BossStandHpGate in the binary. Deploying this file on an old binary leaves
-- anchor 10 applying in every phase (the column is ignored), so ship the migration with the code, in one commit.
--
-- Idempotent: the column is added only when absent, and the DELETE removes only the (249, 10184) anchor-10 rows this
-- file inserts (anchor 9's P2 stack is untouched). Never edit this file once applied; add a newer migration.
--
-- Rollback: DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 10184 AND `anchor` = 10;
--   ALTER TABLE `ai_boss_stand` DROP COLUMN `boss_hp_max` (only if no other row uses it).

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_stand` ADD COLUMN `boss_hp_max` tinyint unsigned NOT NULL DEFAULT 100 AFTER `window_signal`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_stand' AND COLUMN_NAME = 'boss_hp_max');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 10184 AND `anchor` = 10;
INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal`, `boss_hp_max`, `comment`) VALUES
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 0, 2.5, -185.5, -86.0, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 0/8 north: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 1, 2.5, -243.5, -85.9, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 1/8 south: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 2, 7.5, -185.5, -85.6, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 2/8 north: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 3, 7.5, -243.5, -85.6, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 3/8 south: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 4, 2.5, -190.5, -86.2, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 4/8 north: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 5, 2.5, -248.5, -85.2, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 5/8 south: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 6, 7.5, -190.5, -85.7, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 6/8 north: her flank at 40 % (walk 2q)'),
(249, 10184, 10, -22.0, -213.5, 60, 'healer', 0, 0, 0, 7, 7.5, -248.5, -85.1, 0, 0, 1, 8000, 0, 'cast', 40, 'P3 healer stack 7/8 south: her flank at 40 % (walk 2q)');
