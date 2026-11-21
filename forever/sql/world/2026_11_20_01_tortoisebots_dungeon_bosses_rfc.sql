-- TortoiseBots on ForeverCore: the dungeon bosses of Ragefire Chasm (map 389), package H5 of slice S5.
--
-- Copied unchanged from the Turtle migration data/sql/world/20261021114931_world.sql (its first four rows): the four bosses no boss
-- step has to name, Oggleflint 11517, Taragaman the Hungerer 11520, Jergosh the Invoker 11518 and Bazzalan 11519. The plan rows
-- themselves (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step, ai_dungeon_step_unit) are the plan exporter output and ship in a
-- file of their own; nothing here is set by hand except these four copied rows.
--
-- Idempotent: INSERT IGNORE on the primary key (map, entry).

INSERT IGNORE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(389, 11517, 'Oggleflint'),
(389, 11520, 'Taragaman the Hungerer'),
(389, 11518, 'Jergosh the Invoker'),
(389, 11519, 'Bazzalan');
