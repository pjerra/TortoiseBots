-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — the healers' top-up reserve, after walk
-- 75. The 85 % reserve and the 60 s lead paid off where they held: the 12 fights without a collapse kept 80.1 / 68.3 /
-- 47.1 % healer mana at 30 / 60 / 90 s against walk 74's 72.1 / 57.3 / 44.0, and the opening hold (0-30 s, raid health
-- 98.9 %) allowed about one non-tank per frame in the 70-90 % band with no death that a refused top-up could have
-- prevented. The later windows are the expensive half: after each siphon the raid sits at 83.6 % mean health (lowest
-- tenth 60.8), 3.5 non-tanks per frame wait in the 70-90 % band, and 40 % of the frames hold 2 or more under 70 %, so
-- the reserve was holding 3.5 hurt members while paying only about 3 mana points a window.
--
-- priority 2: the reserve is released while 2 or more living non-tanks of the group (the healer counted, tanks never)
-- are under AiPlayerbot.MediumHealth. One hurt member is routine and the medium tier heals it; two at once is raid-wide
-- damage. That reaches 34 % of the later windows' refusals and none of the opening's, costs about 1 mana point at
-- 120 s, and with no clock (the seek key off) it is the only release the reserve has. The 85 % reserve, the 60 s lead
-- and the exemption of tanks and Hakkar's current victim stay. Behind AiPlayerbot.DungeonRunHealReserve (default 0).
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'heal_reserve' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'heal_reserve', 0, 0, 2, 85, '', 60, 0, 0, 0, 0, 0, 'Hakkar: healers keep 85 % mana from top-ups outside the 60 s before each Blood Siphon; released while 2 or more non-tanks are under the medium-health line');
