-- Onyxia's Lair (map 249), walk 2n round 2: two healers on each P3 Hatcher post. Data only.
--
-- Walk 2n P3 (48 tank deaths): 23 to a Hatcher and 15 to a whelp, 15 of them at the two Hatcher posts, while the only
-- posts holding healers were the slot-0 post on Onyxia and the slot-1 post on Cindarion (`healers`, 20261113200000 and
-- 20261114100000). A Hatcher 50144 hits its tank with Rain of Fire 19717 and Flaming Cleave 33441 and each post's tank
-- died at its own spot, so slots 2 (south, health 0-41) and 3 (north, health 0-40) hold two healers within 30 yd each.
-- The reader takes any `hold_spot` post whose `healers` is above 0 under AiPlayerbot.DungeonRunTankPostHealers
-- (FindTankPostHealDuty, BossTacticsActions.cpp), so the mode stays hold_spot and the spots, the bands, the arrival
-- radius and every stand row are untouched.
--
-- Idempotent: the UPDATE names the two 50144 posts at `healers` 0, so a second run matches nothing. Never edit this
-- file once applied; add a newer migration.
--
-- Rollback: UPDATE `ai_boss_tactic` SET `healers` = 0 WHERE `map` = 249 AND `boss_entry` = 10184
-- AND `kind` = 'tank_post' AND `target_entry` = 50144;

UPDATE `ai_boss_tactic` SET `healers` = 2 WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 50144 AND `healers` = 0;
