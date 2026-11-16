-- Onyxia's Lair (map 249), walk 2n: the add crew takes the whelps a phase early, and a bigger crew.
--
-- Walk 2m: she sits at 56-60 % until +150 s after take-off (median 59 %), P2 runs 255-554 s (median 362) and the
-- kill_first row on Cindarion 50143 (20261109200100) sends every free DPS, ranged included, to him. Owner: "ranged
-- should do as much dps on onyxia to get into next phase." The crew the row names at 2e (melee, tanks, 2 AoE ranged)
-- only takes the whelps once he dies; mode `early` starts it while he lives and the boss flies
-- (AiPlayerbot.DungeonRunAddCrewEarly), and 3 AoE ranged give the pile more area damage than 2 (the healers' new stack
-- is where the whelps they pull end up, 20261116120000).
--
-- Idempotent: the UPDATE names exactly the (249, 10184) add_crew row on 50143 at the crew size it had before (2), so
-- a second run matches nothing. Never edit this file once applied; add a newer migration.
--
-- Rollback: UPDATE `ai_boss_tactic` SET `tactic_mode` = '', `priority` = 2 WHERE `map` = 249 AND `boss_entry` = 10184
-- AND `kind` = 'add_crew' AND `target_entry` = 50143;

UPDATE `ai_boss_tactic` SET `tactic_mode` = 'early', `priority` = 3, `comment` = 'Onyxia: the crew takes the whelps while Cindarion lives and she flies, and again once he dies; the other ranged stay on her (needs DungeonRunAddCrew and DungeonRunAddCrewEarly); walk 2n: 56-60 % until +150 s of P2' WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'add_crew' AND `target_entry` = 50143 AND `priority` = 2;
