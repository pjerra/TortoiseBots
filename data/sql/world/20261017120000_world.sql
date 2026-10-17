-- Uldaman (map 70): two plan points from the navmesh and log analysis of the live run-leader rerun of 2026-10-17
-- (uldaman-events-2). Rows of 20261016220000_world.sql, changed in place (that file stays as applied); idempotent.
-- Mirrored in turtle-agent dungeons/uldaman.json (pjerra/turtle-agent#218).
--
-- Step 39 (leg 5 sentinel, seq 1, the first direct step across the scorpid pit): -162.8 294.8 -51.2 is 2.6 yd off the
-- map 70 navmesh (nearest floor -164.3 296.3 -52.7), so a follower had no way to the lead bot's spot and the next direct
-- step had to summon Ironthorn first at step 40 of the run. It moves to -161.1 284.5 -52.1, the last navmesh point before
-- the ramp (0.0 yd off the mesh; navmesh path from the lip -163.5 298.7 -52.5 complete, 17.0 yd). The direct step to
-- step 40 (-158.4 283.7 -48.9, east mesh) shrinks from 11.9 to 4.2 yd. Checked on the live map 70 vmaps (lostool,
-- yulon-ubuntu2 ~/tb-ci/vmaps70): sight lines clear at 1.5, 2 and 2.5 yd both ways, lip -> new point clear too; vmap
-- floor -52.32 under the new point.
UPDATE `ai_dungeon_step` SET `x` = -161.1, `y` = 284.5, `z` = -52.1
WHERE `plan` = 'uldaman' AND `leg` = 5 AND `seq` = 1 AND `kind` = 'move_direct';

-- Step 99 (leg 9 keepers_door, seq 6, the pullback stand of pull group 3, step 100): at 44.5 348 -42.2 the stand was
-- 40 yd from the pack (Brawler, Brawler, Geomancer at 44.3 308.4). The Geomancer cast from range and came only to
-- 42 318; the mage walked after it to 37 315 and woke the two Stonevault Maulers of pull group 4 (7320 at 26 301 and
-- 26 293, 16-18 yd from him), which had stood idle through the tank's own pull; healer, mage and both rogues died.
-- The stand moves 9.5 yd back up the corridor to 47.6 357.3 -43.3, where pull 98 fought clean: 49 yd from the pack,
-- 64 yd from the Maulers, so a caster has to come about 20 yd toward the party and the fight stays farther from pull
-- group 4. The approach point of pull group 2 (50.4 365.8 -43.0, 57.8 yd) would be farther still, but it breaks the
-- plan's 50 yd reach rule (turtle-agent TestUldamanRunLeaderReach: the run leader judges a pack from 60 yd, a stalled
-- arrival stops up to 6 yd short). Pulling groups 3 and 4 as one (five mobs) was not chosen: the Maulers joined only
-- the chase, never the pull. On the mesh (0.03 yd off), navmesh paths complete: from the old stand 9.9 yd, to the
-- pack 50.1 yd, on to pull group 4's approach (38.8 334.3) 24.6 yd. Matches `turtle-agent plan-export` of the mirrored
-- plan row for row (only these two rows differ from 20261016220000_world.sql).
UPDATE `ai_dungeon_step` SET `x` = 47.6, `y` = 357.3, `z` = -43.3
WHERE `plan` = 'uldaman' AND `leg` = 9 AND `seq` = 6 AND `kind` = 'move';
