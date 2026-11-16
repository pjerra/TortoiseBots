-- Onyxia's Lair (map 249), walk 2o: the early crew goes back to two AoE ranged, and the rest of the ranged stay on
-- Cindarion until he dies.
--
-- Walk 2n had 3 AoE ranged on the whelps while he lived: the non-crew ranged share on Cindarion fell to a mean 42-67 %
-- and their Cindarion casting to 24 % (2m 36 %), and about a quarter of the phase's damage loss was the crew itself
-- (`DungeonRunAddCrewBossWhileSwitch` = 0 keeps the other ranged on him, the split walk 2o measures). The healers'
-- resting stack (20261116120000) is where the whelps they pull end up, and two AoE ranged cover that pile; the third is
-- better spent on him.
--
-- Idempotent: the UPDATE names exactly the (249, 10184) add_crew row on 50143 at the crew size it had before (3), so a
-- second run matches nothing; the row's mode is not touched. Never edit this file once applied; add a newer migration.
--
-- Rollback: UPDATE `ai_boss_tactic` SET `priority` = 3 WHERE `map` = 249 AND `boss_entry` = 10184
-- AND `kind` = 'add_crew' AND `target_entry` = 50143 AND `priority` = 2;

UPDATE `ai_boss_tactic` SET `priority` = 2, `comment` = 'Onyxia: the crew takes the whelps while Cindarion lives and she flies, and again once he dies; the other ranged stay on him until then and on her after (needs DungeonRunAddCrew and DungeonRunAddCrewEarly); walk 2o: crew 2, the rest hold Cindarion' WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'add_crew' AND `target_entry` = 50143 AND `priority` = 3;
