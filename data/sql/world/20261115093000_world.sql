-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.


--
-- turtle-agent 636297f (fix/route-order-stratholme_undead): `plan-export --allow-draft --gm-cross-direct --floor-aware
-- --approach-hops --mmaps <the server's mmaps> --rev 636297f --stamp 20261115093000`. Stratholme undead route order fix
-- (issue #1542): the same six legs and the same five bosses in the same order, each pack in the leg whose boss-to-boss
-- walk passes it. 22 packs move to another leg, the four leg `to` points move onto that walk (existing deployed rows at
-- steps 9, 103, 139 and 144) and the 23 packs of the draft's northern lane (King's Square and the street north of
-- Elder's Square) are dropped. 101 steps (108 rows) instead of the deployed 202 (a fresh export of the old order: 214),
-- 2,530 yd instead of 5,126 (5,184.5 fresh); the five boss-to-boss walks are 1,250 yd. The turtle-agent commit message
-- names the row behind each moved point. plan-check: 168 points, 104 lint findings (69 fail) against the old order's 289
-- (97 fail); plan-probe: 101 steps, 1 bad (the last leg's boss row the server answers NOPATH for, as on dev); trash-check
-- at level 56: no unplanned pack woken (dev: 30 spawns). The three ziggurats' five-acolyte packs stay in their own boss's
-- leg and all 13 of leg 4's Slaughter Square pulls stay. tools/test_dungeon_plan_sql.py pins the rows.

-- stratholme_undead: map 329, 6 legs, 101 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'stratholme_undead';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'stratholme_undead';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'stratholme_undead';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('stratholme_undead', 329, '', '', '10393,10411,10400,10398', 'turtle-agent 636297f dungeons/stratholme_undead.json', 'plan v1, draft (not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('stratholme_undead', 0, 'magistrate_barthilas', ''),
('stratholme_undead', 1, 'nerub''enkan', ''),
('stratholme_undead', 2, 'maleki_the_pallid', ''),
('stratholme_undead', 3, 'baroness_anastari', ''),
('stratholme_undead', 4, 'baron_rivendare', ''),
('stratholme_undead', 5, 'baron_rivendare_room', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('stratholme_undead', 0, 0, 'move', 0, 3636.06, -3636.82, 138.218, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 0, 1, 'pull', 0, 3642.62, -3637.1, 138.34, 10.8, 0, 0, 0, 0, '10381,10391,10390,10382', 'pull group 0'),
('stratholme_undead', 0, 2, 'boss', 10435, 3663.23, -3619.14, 137.984, 0, 0, 0, 0, 0, '', 'boss'),
('stratholme_undead', 1, 0, 'move', 0, 3681.2666, -3611.2, 138.25056, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 1, 1, 'pull', 0, 3688.11, -3598.35, 138.14, 12.6, 0, 0, 0, 0, '10381,10382,10390,10391', 'pull group 0'),
('stratholme_undead', 1, 2, 'pull', 0, 3710.47, -3610.39, 141.27, 14.2, 0, 0, 0, 0, '10391,10382,10390,10381', 'pull group 1'),
('stratholme_undead', 1, 3, 'move', 0, 3755.9333, -3588, 142.52452, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 4, 'move', 0, 3772.4666, -3585.3333, 142.58629, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 5, 'move', 0, 3778.7717, -3586.7478, 143.41035, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 6, 'move', 0, 3794.2383, -3612.1853, 145.94307, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 7, 'pull', 0, 3807.68, -3650.65, 147.2, 5, 0, 0, 0, 0, '10407,10406', 'pull group 2'),
('stratholme_undead', 1, 8, 'pull', 0, 3828.49, -3646.93, 145.92, 5, 0, 0, 0, 0, '10413,10463', 'pull group 3'),
('stratholme_undead', 1, 9, 'pull', 0, 3842.06, -3634.63, 146.44, 5, 0, 0, 0, 0, '10406', 'pull group 4'),
('stratholme_undead', 1, 10, 'pull', 0, 3836.66, -3619.58, 145.11, 5, 0, 0, 0, 0, '10463', 'pull group 5'),
('stratholme_undead', 1, 11, 'pull', 0, 3814.97, -3626.9, 145.38, 9.1, 0, 0, 0, 0, '10413,10464', 'pull group 6'),
('stratholme_undead', 1, 12, 'pull', 0, 3824.23, -3617.32, 145.3, 5, 0, 0, 0, 0, '10408', 'pull group 7'),
('stratholme_undead', 1, 13, 'pull', 0, 3832.49, -3590.92, 145.26, 5, 0, 0, 0, 0, '10406,10407', 'pull group 8'),
('stratholme_undead', 1, 14, 'pull', 0, 3853.56, -3612.3, 145.16, 5, 0, 0, 0, 0, '10400,10398', 'pull group 9'),
('stratholme_undead', 1, 15, 'pull', 0, 3857.67, -3586.82, 142.26, 5.6, 0, 0, 0, 0, '10463,10412', 'pull group 10'),
('stratholme_undead', 1, 16, 'pull', 0, 3872.36, -3565.96, 138.63, 8.6, 0, 0, 0, 0, '10464,10413', 'pull group 11'),
('stratholme_undead', 1, 17, 'pull', 0, 3854.75, -3557.1, 140, 5, 0, 0, 0, 0, '10412,10398', 'pull group 12'),
('stratholme_undead', 1, 18, 'move', 0, 3850.923, -3560.8833, 141.27379, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 19, 'move', 0, 3844.5332, -3557.4668, 141.0661, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 20, 'move', 0, 3852.081, -3546.2383, 139.05237, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 21, 'move', 0, 3855.4546, -3535.7212, 142.75221, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 1, 22, 'pull', 0, 3838.87, -3498.28, 141.89, 8.8, 0, 0, 0, 0, '10399', 'pull group 13'),
('stratholme_undead', 1, 23, 'boss', 10437, 3854.98, -3528.84, 144.293, 0, 0, 0, 0, 0, '', 'boss'),
('stratholme_undead', 2, 0, 'move', 0, 3882.6665, -3541.3333, 138.17378, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 2, 1, 'pull', 0, 3877.69, -3533.13, 138.5, 9.3, 0, 0, 0, 0, '10408,10400,10398,10413', 'pull group 0'),
('stratholme_undead', 2, 2, 'move', 0, 3888.4282, -3539.8147, 138.7212, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 2, 3, 'move', 0, 3906.121, -3543.4546, 136.78096, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 2, 4, 'move', 0, 3930.1, -3552.09, 134.17, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 2, 5, 'pull', 0, 3954.89, -3565.85, 130.02, 5, 0, 0, 0, 0, '10398,10400', 'pull group 1'),
('stratholme_undead', 2, 6, 'pull', 0, 3970.55, -3546.59, 126.49, 5, 0, 0, 0, 0, '10407,10406', 'pull group 2'),
('stratholme_undead', 2, 7, 'pull', 0, 3972, -3567.57, 126.13, 5, 0, 0, 0, 0, '10464', 'pull group 3'),
('stratholme_undead', 2, 8, 'pull', 0, 3972.17, -3587.59, 128.57, 5, 0, 0, 0, 0, '10405', 'pull group 4'),
('stratholme_undead', 2, 9, 'pull', 0, 4001.46, -3630.33, 130.05, 5, 0, 0, 0, 0, '10463', 'pull group 5'),
('stratholme_undead', 2, 10, 'pull', 0, 4022.6, -3617.64, 131.35, 5, 0, 0, 0, 0, '10400,10398', 'pull group 6'),
('stratholme_undead', 2, 11, 'move', 0, 4022.3333, -3630.9333, 129.30379, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 2, 12, 'move', 0, 4034.121, -3645.8787, 135.67378, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 2, 13, 'pull', 0, 4059.17, -3668.05, 133.34, 8.8, 0, 0, 0, 0, '10399', 'pull group 7'),
('stratholme_undead', 2, 14, 'boss', 10438, 4035.81, -3647.78, 135.738, 0, 0, 0, 0, 0, '10398,10400', 'boss'),
('stratholme_undead', 3, 0, 'move', 0, 4028.0664, -3644, 135.19165, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 3, 1, 'pull', 0, 4001.6, -3640.67, 131.84, 5, 0, 0, 0, 0, '10400,10398', 'pull group 0'),
('stratholme_undead', 3, 2, 'pull', 0, 3974.18, -3645.93, 133.51, 6, 0, 0, 0, 0, '10463,10406,10405', 'pull group 1'),
('stratholme_undead', 3, 3, 'pull', 0, 3949.78, -3641.36, 135.65, 8.1, 0, 0, 0, 0, '10408,10406,10407', 'pull group 2'),
('stratholme_undead', 3, 4, 'move', 0, 3945.5186, -3645.2283, 137.74643, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 3, 5, 'move', 0, 3926.59, -3661.67, 138.46, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 3, 6, 'pull', 0, 3898.52, -3665.43, 140.33, 5, 0, 0, 0, 0, '10400,10405', 'pull group 3'),
('stratholme_undead', 3, 7, 'pull', 0, 3893.2, -3682.22, 142.12, 8.2, 0, 0, 0, 0, '10408,10406,10407,10412', 'pull group 4'),
('stratholme_undead', 3, 8, 'pull', 0, 3887.78, -3709.84, 143.55, 5, 0, 0, 0, 0, '10400,10398', 'pull group 5'),
('stratholme_undead', 3, 9, 'pull', 0, 3875.71, -3701.37, 142.76, 5, 0, 0, 0, 0, '10463', 'pull group 6'),
('stratholme_undead', 3, 10, 'pull', 0, 3867.81, -3683.01, 142.6, 7.5, 0, 0, 0, 0, '10464,10412', 'pull group 7'),
('stratholme_undead', 3, 11, 'pull', 0, 3846.54, -3666.64, 144.45, 13.5, 0, 0, 0, 0, '10463,10413,10406,10405', 'pull group 8'),
('stratholme_undead', 3, 12, 'pull', 0, 3822.39, -3683.73, 144.96, 5, 0, 0, 0, 0, '10406,10405', 'pull group 9'),
('stratholme_undead', 3, 13, 'pull', 0, 3835.8, -3695.67, 143.08, 9.9, 0, 0, 0, 0, '10463,10400,10398', 'pull group 10'),
('stratholme_undead', 3, 14, 'move', 0, 3849.0544, -3699.4785, 141.76828, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 3, 15, 'move', 0, 3855.0383, -3713.148, 148.15134, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 3, 16, 'pull', 0, 3847.35, -3750.09, 145.77, 8.8, 0, 0, 0, 0, '10399', 'pull group 11'),
('stratholme_undead', 3, 17, 'boss', 10436, 3855.34, -3715.75, 148.175, 0, 0, 0, 0, 0, '', 'boss'),
('stratholme_undead', 4, 0, 'move', 0, 3861.3333, -3714.4666, 149.29129, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 4, 1, 'move', 0, 3865.0667, -3705.8667, 143.67378, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 4, 2, 'move', 0, 3945.5186, -3645.2283, 137.74643, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 4, 3, 'pull', 0, 3980.43, -3623.11, 130.01, 9.5, 0, 0, 0, 0, '10412,10407,10405', 'pull group 0'),
('stratholme_undead', 4, 4, 'pull', 0, 3991.8, -3593.17, 128.3, 10.1, 0, 0, 0, 0, '10412,10413,10464', 'pull group 1'),
('stratholme_undead', 4, 5, 'pull', 0, 4008.57, -3594.09, 130.41, 5, 0, 0, 0, 0, '10405,10406', 'pull group 2'),
('stratholme_undead', 4, 6, 'pull', 0, 3996.18, -3561.07, 125.51, 6.5, 0, 0, 0, 0, '10463,10412', 'pull group 3'),
('stratholme_undead', 4, 7, 'pull', 0, 3988.89, -3541.87, 124.69, 5, 0, 0, 0, 0, '10408,10400', 'pull group 4'),
('stratholme_undead', 4, 8, 'pull', 0, 4022.35, -3558.36, 126.13, 5, 0, 0, 0, 0, '10406,10405', 'pull group 5'),
('stratholme_undead', 4, 9, 'pull', 0, 4014.25, -3540.64, 122.87, 5, 0, 0, 0, 0, '10464', 'pull group 6'),
('stratholme_undead', 4, 10, 'pull', 0, 4017.73, -3520.39, 122.1, 7.6, 0, 0, 0, 0, '10405,10407', 'pull group 7'),
('stratholme_undead', 4, 11, 'pull', 0, 4048.21, -3520.04, 121.66, 5, 0, 0, 0, 0, '10408', 'pull group 8'),
('stratholme_undead', 4, 12, 'pull', 0, 4058.86, -3530.33, 122.25, 5, 0, 0, 0, 0, '10409,10408,10809', 'pull group 9'),
('stratholme_undead', 4, 13, 'pull', 0, 4067.56, -3534.45, 122.68, 5, 0, 0, 0, 0, '10411', 'pull group 10'),
('stratholme_undead', 4, 14, 'pull', 0, 4056.42, -3516.3, 122.23, 5, 0, 0, 0, 0, '10405,10406', 'pull group 11'),
('stratholme_undead', 4, 15, 'move', 0, 4047.8948, -3509.4146, 121.87173, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 4, 16, 'move', 0, 4046.2947, -3504.6145, 121.973785, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 4, 17, 'move', 0, 4043.133, -3466.4, 122.090965, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 4, 18, 'move', 0, 4060.1997, -3445.8667, 120.40081, 0, 0, 0, 0, 0, '', 'approach path'),
('stratholme_undead', 4, 19, 'pull', 0, 4089.61, -3431.32, 116.94, 18, 0, 0, 0, 0, '10417', 'pull group 12'),
('stratholme_undead', 4, 20, 'pull', 0, 4097.98, -3397.96, 115.99, 18, 0, 0, 0, 0, '10416', 'pull group 13'),
('stratholme_undead', 4, 21, 'pull', 0, 4083.99, -3379.84, 116.32, 18, 0, 0, 0, 0, '10417', 'pull group 14'),
('stratholme_undead', 4, 22, 'pull', 0, 4072.01, -3401.01, 115.65, 18, 0, 0, 0, 0, '10417', 'pull group 15'),
('stratholme_undead', 4, 23, 'pull', 0, 4053.98, -3413.3, 116.08, 18, 0, 0, 0, 0, '10416', 'pull group 16'),
('stratholme_undead', 4, 24, 'pull', 0, 4052.9, -3432.7, 118.53, 18, 0, 0, 0, 0, '10417', 'pull group 17'),
('stratholme_undead', 4, 25, 'pull', 0, 4040.22, -3443.81, 119.75, 18, 0, 0, 0, 0, '10417', 'pull group 18'),
('stratholme_undead', 4, 26, 'pull', 0, 4022.83, -3424.39, 117.55, 18, 0, 0, 0, 0, '10417', 'pull group 19'),
('stratholme_undead', 4, 27, 'pull', 0, 4009.16, -3434.68, 120.95, 18, 0, 0, 0, 0, '10416', 'pull group 20'),
('stratholme_undead', 4, 28, 'pull', 0, 3995.31, -3425.23, 120.99, 18, 0, 0, 0, 0, '10417', 'pull group 21'),
('stratholme_undead', 4, 29, 'pull', 0, 3998.95, -3403.29, 117.52, 18, 0, 0, 0, 0, '10417', 'pull group 22'),
('stratholme_undead', 4, 30, 'pull', 0, 3990.78, -3381.93, 118.72, 18, 0, 0, 0, 0, '10416', 'pull group 23'),
('stratholme_undead', 4, 31, 'pull', 0, 4012.37, -3401.48, 116.07, 18, 0, 0, 0, 0, '10417', 'pull group 24'),
('stratholme_undead', 4, 32, 'hold', 0, 4040.01, -3413.15, 117.77, 68, 0, 0, 0, 300, '10440,10416,10417', 'boss hold: fought on the spot until every unit of it is dead (10440 counts while still friendly)'),
('stratholme_undead', 5, 0, 'move', 0, 4033.5332, -3398.6667, 116.22569, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 1, 'move', 0, 4032.4666, -3392, 119.77378, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 2, 'move', 0, 4031.6665, -3382.6667, 119.77378, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 3, 'move', 0, 4040.6145, -3378.7717, 118.93202, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 4, 'move', 0, 4042.2383, -3374.748, 117.77956, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 5, 'move', 0, 4031.6665, -3367.2, 115.073784, 0, 0, 0, 0, 0, '', 'path'),
('stratholme_undead', 5, 6, 'move', 0, 4032.79, -3333.02, 115.061, 0, 0, 0, 0, 0, '', 'to'),
('stratholme_undead', 5, 7, 'boss', 10440, 4032.79, -3333.02, 115.061, 0, 0, 0, 0, 0, '', 'boss');
