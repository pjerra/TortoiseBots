-- Onyxia's Lair (map 249), walk 2n: a resting healer stack for phase 2 (anchor 9). Data only.
--
-- Owner: "the healers should gather since they pull the aggro for the whelps, and dps should aoe on the healers."
-- Walk 2m: the healers have no point of their own (a `healers` column only seats two of them by a hold_spot holder,
-- 20261114100000) and the rest take the eight Deep Breath seats (20261111170300), so the whelps that peel to them
-- scatter across the arena and the AoE crew has no pile to burn.
--
-- No spot clears every Deep Breath lane (the tick spells reach 30 yd; the best walkable clearance here is 27.5 yd
-- against the seats' own 33 yd rule), so this stack is a RESTING spot: it is read only while the breath seats' windows
-- are closed, and a breath's own anchor (within 6 yd of the boss) outranks it in ActiveStandPoints (BossStandPolicy.h).
-- The aura 17131 gate makes it the flying phase: she wears the Hover aura only in P2.
--
-- Centre C = (-15.0, -170.0): 22.5 yd of lane clearance, 37.8 yd from the raid centre T (8.0, -200.0), and 18.9 yd
-- from the nearest egg. Ten points, 6 yd apart, one row each (one bot a point: kBossStandSpacing lines up the slots of
-- one point, so `slots` stays 1), each at least 12.7 yd of lane clearance and 13.6 yd from an egg:
--   C, C+(+-6,0), C+(+-3,+-5.2), C+(+-9,-5.2), C+(0,-10.4)
-- Two of those (C+(+-3,+5.2), the north pair at y -164.8) are off the walkable floor: the arena's floor edge there is
-- the cave wall (path-check ends 4.3 yd short), so they are the south pair C+(-6,+-11.4) instead, which keeps the 6 yd
-- spacing and both bounds.
--
-- z from the offline path-check from both walk STARTs (-8.9 -212.8 -88.5 and -65.8 -213.8 -84.3), the same probe as
-- ony_breath_seats-2b.py: offline path complete, server verdict ok, short path < 1.5 yd, end within 3 yd, floor z in
-- -93..-80. Points in seq order: (-15.0,-170.0) -84.7, (-9.0,-170.0) -83.6, (-21.0,-170.0) -86.0,
-- (-12.0,-175.2) -85.7, (-18.0,-175.2) -86.6, (-6.0,-175.2) -84.9, (-24.0,-175.2) -87.8, (-15.0,-180.4) -86.9,
-- (-21.0,-181.4) -87.6, (-18.0,-185.6) -87.5.
--
-- Idempotent: the DELETE removes only the (249, 10184) anchor-9 rows, which this file inserts; anchors 1-8 (the 601
-- breath seats) are untouched. Never edit this file once applied; add a newer migration.
--
-- Rollback: DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 10184 AND `anchor` = 9;

DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 10184 AND `anchor` = 9;
INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal`, `comment`) VALUES
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 0, -15.0, -170.0, -84.7, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 0/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 1, -9.0, -170.0, -83.6, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 1/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 2, -21.0, -170.0, -86.0, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 2/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 3, -12.0, -175.2, -85.7, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 3/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 4, -18.0, -175.2, -86.6, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 4/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 5, -6.0, -175.2, -84.9, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 5/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 6, -24.0, -175.2, -87.8, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 6/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 7, -15.0, -180.4, -86.9, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 7/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 8, -21.0, -181.4, -87.6, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 8/10: resting spot between breaths (walk 2n)'),
(249, 10184, 9, -22.0, -213.5, 60, 'healer', 0, 17131, 0, 9, -18.0, -185.6, -87.5, 0, 0, 1, 8000, 0, 'cast', 'P2 healer stack 9/10: resting spot between breaths (walk 2n)');
