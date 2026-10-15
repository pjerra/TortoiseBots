-- Zul'Farrak (map 209), bots-only observe run, re-exported (issue #999). Replaces the rows of 20261015013000_world.sql
-- (that file stays as applied). Exported from turtle-agent 6cd0b13 (PR #188: dungeons/zulfarrak.json, which passed a live
-- run with 0 stops) with `turtle-agent plan-export --mmaps <map 209 mmaps> --rev 6cd0b13 --stamp 20261015200100 zulfarrak`:
--   * the direct hops at 1289.9 816.1, 1420.7 801.1 and 1535 834 are gone (plain moves now: no move_direct row);
--   * the leg 'STOP not supported (gong)' and its ritual step are gone: the plan ends at Velratha.
-- HAND-SET VALUES: the ai_dungeon_plan columns cc_priority, cc_always and kill_first of this row (columns from
-- 20261015200000_world.sql) were set by hand, pending pjerra/turtle-agent#190 (the exporter will write them). The
-- exporter at 6cd0b13 does not write them: it reads cc_priority and cc_always into its plan but has no kill_first
-- field and no column for any of the three, so a re-export from it would write this row WITHOUT them.
-- tools/test_dungeon_plan_sql.py fails when the newest Zul'Farrak plan row has an empty cc_always or kill_first.
--   cc_priority 5650,7247,7246,5648  Sandfury Witch Doctor (Flash Heal 17843, Healing Ward 11899 -> Greater Healing Ward
--                                    8179), Sandfury Soul Eater (Dark Offering), Sandfury Shadowhunter (Hex), Sandfury
--                                    Shadowcaster
--   cc_always   5650,7246            in any pack they are in
--   kill_first  8179,5650,7247       the summoned ward first, then the witch doctor and the soul eater
-- The plan file's own cc_priority (7246,5650,5648) and cc_always (7246) differ; turtle-agent#190 brings these values
-- and a kill_first field into the plan file and the exporter.
--
-- Everything below is the exporter's output, unchanged but for those three values:
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql, the cc columns from
-- 20261015200000_world.sql.

-- zulfarrak: map 209, 9 legs, 86 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'zulfarrak';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'zulfarrak';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'zulfarrak';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('zulfarrak', 209, '5650,7247,7246,5648', '5650,7246', '8179,5650,7247', 'turtle-agent 6cd0b13 dungeons/zulfarrak.json', 'plan v1, live-checked');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('zulfarrak', 0, 'antusul_a', ''),
('zulfarrak', 1, 'antusul_a2', ''),
('zulfarrak', 2, 'antusul_a3', ''),
('zulfarrak', 3, 'antusul_a4', ''),
('zulfarrak', 4, 'antusul', ''),
('zulfarrak', 5, 'antusul_c', ''),
('zulfarrak', 6, 'antusul_b', ''),
('zulfarrak', 7, 'theka', ''),
('zulfarrak', 8, 'velratha', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('zulfarrak', 0, 0, 'move', 0, 1252.3, 836.4, 9.1, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 0, 1, 'pull', 0, 1263.13, 842.53, 9.13, 15, 0, 0, 0, 0, '5649,5648', 'pull group 0'),
('zulfarrak', 0, 2, 'pull', 0, 1295.37, 810.53, 11.3, 30, 0, 0, 0, 0, '5649,5648,5650', 'pull group 1'),
('zulfarrak', 1, 0, 'move', 0, 1289.9, 816.1, 9.4, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 1, 1, 'move', 0, 1297.1, 827.9, 9.3, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 1, 2, 'move', 0, 1310, 834.3, 9.5, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 1, 3, 'pull', 0, 1324.33, 839.5, 10, 30, 0, 0, 0, 0, '5649,5648,5650', 'pull group 0'),
('zulfarrak', 1, 4, 'pull', 0, 1361.35, 818.6, 9, 15, 0, 0, 0, 0, '5649', 'pull group 1'),
('zulfarrak', 1, 5, 'pull', 0, 1406.5, 813.05, 8.9, 60, 0, 0, 0, 0, '5649', 'pull group 2'),
('zulfarrak', 1, 6, 'pull', 0, 1428.1, 796.63, 11.47, 15, 0, 0, 0, 0, '5648', 'pull group 3'),
('zulfarrak', 2, 0, 'move', 0, 1358.0547, 820.49274, 9.347955, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 2, 1, 'move', 0, 1406.1093, 806.68555, 9.682786, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 2, 2, 'move', 0, 1413.6, 804.53326, 9.73498, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 2, 3, 'move', 0, 1420.7, 801.1, 11, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 4, 'move', 0, 1433.9, 804.7, 10.3, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 5, 'move', 0, 1442.5, 804.7, 12.7, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 6, 'move', 0, 1450.1, 805.9, 12.9, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 7, 'move', 0, 1460.5, 808.5, 10.8, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 2, 8, 'pull', 0, 1491.13, 824.87, 9.07, 30, 0, 0, 0, 0, '5650,5649', 'pull group 0'),
('zulfarrak', 3, 0, 'move', 0, 1506, 844.9, 10.2, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 3, 1, 'pull', 0, 1506.75, 838.5, 9.45, 15, 0, 0, 0, 0, '5648', 'pull group 0'),
('zulfarrak', 4, 0, 'move', 0, 1506, 844.9, 10.2, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 4, 1, 'move', 0, 1520, 840, 9.5, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 4, 2, 'move', 0, 1535, 834, 9, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 4, 3, 'pull', 0, 1530.05, 823.93, 9.07, 30, 0, 0, 0, 0, '5649,5648', 'pull group 0'),
('zulfarrak', 4, 4, 'pull', 0, 1545.6, 837.05, 8.9, 30, 0, 0, 0, 0, '5649,5648', 'pull group 1'),
('zulfarrak', 4, 5, 'pull', 0, 1583.83, 850.53, 10.3, 15, 0, 0, 0, 0, '5650,5648', 'pull group 2'),
('zulfarrak', 4, 6, 'pull', 0, 1633.3, 868.3, 10.5, 15, 0, 0, 0, 0, '5650', 'pull group 3'),
('zulfarrak', 4, 7, 'pull', 0, 1643.65, 904, 9.4, 10, 0, 0, 0, 0, '5648,7247', 'pull group 4'),
('zulfarrak', 5, 0, 'move', 0, 1578.6666, 853.33325, 9.23498, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 1, 'move', 0, 1626.9175, 866.3581, 10.7200775, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 2, 'move', 0, 1633, 868, 10.5, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 5, 3, 'pull', 0, 1685.03, 876.3, 9, 20, 0, 0, 0, 0, '5649,5648', 'pull group 0'),
('zulfarrak', 5, 4, 'pull', 0, 1713.7, 784.1, 17.3, 15, 0, 0, 0, 0, '5649', 'pull group 1'),
('zulfarrak', 5, 5, 'pull', 0, 1776.9, 813.7, 23.2, 15, 0, 0, 0, 0, '5650', 'pull group 2'),
('zulfarrak', 6, 0, 'move', 0, 1638.4, 880.79987, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 1, 'move', 0, 1642.6666, 883.46655, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 2, 'move', 0, 1651.2, 884.26654, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 3, 'move', 0, 1664, 874.66656, 16.325752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 4, 'move', 0, 1677.8667, 870.9332, 9.225753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 5, 'move', 0, 1679.7333, 869.5999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 6, 'move', 0, 1679.7333, 868.79987, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 7, 'move', 0, 1674.9333, 821.5999, 9.025752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 8, 'move', 0, 1676.5333, 818.13324, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 9, 'move', 0, 1682.4, 807.9999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 10, 'move', 0, 1699.4667, 801.5999, 9.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 11, 'move', 0, 1700.7999, 800.7999, 9.625752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 12, 'move', 0, 1706.6666, 794.9332, 10.825752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 13, 'move', 0, 1708, 794.9332, 11.125752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 14, 'move', 0, 1749.3333, 807.9999, 19.625753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 15, 'move', 0, 1754.3999, 808.53326, 19.725752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 16, 'move', 0, 1766.9332, 804.26654, 24.725752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 17, 'move', 0, 1769.03, 802.98, 24.04, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 6, 18, 'move', 0, 1787.8, 790.6, 17.7, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 6, 19, 'pull', 0, 1793.5, 753.4, 14.3, 50, 0, 0, 0, 0, '5649', 'pull group 0'),
('zulfarrak', 6, 20, 'pull', 0, 1802.2, 738.7, 14.5, 30, 0, 0, 0, 0, '8095', 'pull group 1'),
('zulfarrak', 6, 21, 'pull', 0, 1816.5, 673.5, 14.9, 15, 0, 0, 0, 0, '8130', 'pull group 2'),
('zulfarrak', 6, 22, 'boss', 8127, 1815.8, 670.4, 15, 0, 0, 0, 0, 0, '', 'boss'),
('zulfarrak', 7, 0, 'move', 0, 1807.2, 798.5, 13.9, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 7, 1, 'pull', 0, 1767.15, 853.55, 8.9, 30, 0, 0, 0, 0, '5649,7247', 'pull group 0'),
('zulfarrak', 7, 2, 'boss', 7272, 1778, 859.7, 8.9, 0, 0, 0, 0, 0, '', 'boss'),
('zulfarrak', 8, 0, 'move', 0, 1792, 810.66656, 21.525753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 1, 'move', 0, 1749.3333, 810.66656, 17.625753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 2, 'move', 0, 1708, 794.9332, 11.125752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 3, 'move', 0, 1706.6666, 794.9332, 10.825752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 4, 'move', 0, 1700.7999, 800.7999, 9.625752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 5, 'move', 0, 1699.4667, 801.5999, 9.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 6, 'move', 0, 1682.4, 807.9999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 7, 'move', 0, 1676.5333, 818.13324, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 8, 'move', 0, 1674.9333, 821.5999, 9.025752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 9, 'move', 0, 1676.1, 833.5, 9.5, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 10, 'move', 0, 1678.48, 856.47, 8.95, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 11, 'move', 0, 1668.92, 872.47, 11.57, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 12, 'move', 0, 1658.35, 878.91, 13.06, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 8, 13, 'pull', 0, 1666.77, 903.85, 8.97, 15, 0, 0, 0, 0, '5648,5650', 'pull group 0'),
('zulfarrak', 8, 14, 'pull', 0, 1660.45, 937.2, 9.15, 15, 0, 0, 0, 0, '7246,5648,5649', 'pull group 1'),
('zulfarrak', 8, 15, 'pull', 0, 1643.65, 904, 9.4, 15, 0, 0, 0, 0, '5648,7247', 'pull group 2'),
('zulfarrak', 8, 16, 'pull', 0, 1605.77, 973.07, 9.13, 15, 0, 0, 0, 0, '5649', 'pull group 3'),
('zulfarrak', 8, 17, 'pull', 0, 1589.4, 985.4, 9.2, 30, 0, 0, 0, 0, '5648', 'pull group 4'),
('zulfarrak', 8, 18, 'pull', 0, 1585.1, 1035.5, 9, 30, 0, 0, 0, 0, '5649', 'pull group 5'),
('zulfarrak', 8, 19, 'pull', 0, 1583, 1086.76, 8.98, 30, 0, 0, 0, 0, '5650,7246,8095,5649', 'pull group 6'),
('zulfarrak', 8, 20, 'pull', 0, 1606.7, 1107.4, 9, 15, 0, 0, 0, 0, '7246,5650', 'pull group 7'),
('zulfarrak', 8, 21, 'pull', 0, 1639.44, 1106.82, 9.04, 15, 0, 0, 0, 0, '5649,5650,7246', 'pull group 8'),
('zulfarrak', 8, 22, 'pull', 0, 1649.33, 1157, 9.03, 30, 0, 0, 0, 0, '5649', 'pull group 9'),
('zulfarrak', 8, 23, 'pull', 0, 1715.48, 1186.18, 10.02, 15, 0, 0, 0, 0, '5649,5650,7246', 'pull group 10'),
('zulfarrak', 8, 24, 'boss', 7795, 1650.4, 1161.4, 9.9, 0, 0, 0, 0, 0, '', 'boss');
