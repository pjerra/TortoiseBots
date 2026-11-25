-- Shared five-man boss tactics; audited against the Forever RFC exports.
-- Boss abilities: forever/data/waterys/rfc/rfc-bosses.csv (TDB + DB2 70959).
-- Engaged adds: rfc-mobs.csv. Do not import Turtle raid rows.
CREATE TABLE IF NOT EXISTS `ai_boss_stand` (
  `map` int unsigned NOT NULL,
  `boss_entry` int unsigned NOT NULL,
  `anchor` int unsigned NOT NULL,
  `anchor_x` float NOT NULL,
  `anchor_y` float NOT NULL,
  `anchor_r` float NOT NULL,
  `role` varchar(8) NOT NULL,
  `window_spell` int unsigned NOT NULL DEFAULT 0,
  `aura` int unsigned NOT NULL DEFAULT 0,
  `side` tinyint unsigned NOT NULL DEFAULT 0,
  `seq` tinyint unsigned NOT NULL DEFAULT 0,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `dx` float NOT NULL DEFAULT 0,
  `dy` float NOT NULL DEFAULT 0,
  `slots` tinyint unsigned NOT NULL DEFAULT 5,
  `hold_ms` int unsigned NOT NULL DEFAULT 8000,
  `release_anchor` int unsigned NOT NULL DEFAULT 0,
  `window_signal` varchar(24) NOT NULL DEFAULT 'cast',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`map`, `boss_entry`, `anchor`, `role`, `window_spell`, `side`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `ai_boss_tactic`
(`map`,`boss_entry`,`kind`,`target_entry`,`spell_id`,`priority`,`param`,`comment`) VALUES
(389,11517,'behind',0,0,0,12,'Oggleflint Cleave 5532: non-tanks leave the front'),
(389,11517,'kill_first',11319,0,1,0,'Oggleflint: already engaged Ragefire Shaman first'),
(389,11517,'kill_first',11318,0,2,0,'Oggleflint: already engaged Ragefire Trogg next'),
(389,11518,'interrupt',0,20800,0,0,'Jergosh Immolate: learned available interrupts only'),
(389,11518,'kill_first',11322,0,1,0,'Jergosh: already engaged Cultists first'),
(389,11519,'kill_first',11322,0,1,0,'Bazzalan: already engaged Cultists first');

-- Point from the shipped navmesh-exported RFC boss step (leg 3, step 3).
-- Only the actual boss victim tank owns this point. Runtime checks still require
-- a complete native path and a valid floor; no fixed unchecked movement.
INSERT IGNORE INTO `ai_boss_stand`
(`map`,`boss_entry`,`anchor`,`anchor_x`,`anchor_y`,`anchor_r`,`role`,`x`,`y`,`z`,`slots`,`comment`) VALUES
(389,11517,1,-147.550,38.710,30,'tank',-147.550,38.710,-38.800,1,'RFC plan boss step; runtime navigation validation required');
