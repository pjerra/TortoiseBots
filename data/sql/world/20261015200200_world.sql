-- Zul'Farrak plan row (ai_dungeon_plan 'zulfarrak'): crowd control and kill-first, owner decision 2026-10-15
-- (issue #999). The run leader polymorphs the Sandfury Shadowhunter 7246 (its Hex 11641 takes the tank out) before
-- the pull, and kills the Sandfury Witch Doctor 5650 (it drops the Greater Healing Ward 8179) first instead of
-- sheeping it: focus the ward 8179, then 5650, then the Soul Eater 7247.
--   cc_priority 7246,7247,5648   Shadowhunter, Soul Eater, Shadowcaster
--   cc_always   7246             in any pack it is in
--   kill_first  8179,5650,7247   the ward first, then the witch doctor and the soul eater
-- These values are set by hand, as in 20261015200100_world.sql, pending pjerra/turtle-agent#190 (the exporter will
-- write them; the plan file takes these values). tools/test_dungeon_plan_sql.py fails when the Zul'Farrak plan row
-- ends up with an empty cc_always or kill_first after all migrations.
--
-- Idempotent: one UPDATE of the plan row (columns from 20261015200000_world.sql). Never edit this file once applied;
-- add a newer migration.

UPDATE `ai_dungeon_plan` SET `cc_priority` = '7246,7247,5648', `cc_always` = '7246', `kill_first` = '8179,5650,7247'
WHERE `plan` = 'zulfarrak';
