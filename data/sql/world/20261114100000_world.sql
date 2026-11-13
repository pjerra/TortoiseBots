-- Onyxia's Lair (map 249), walk 2j: two data-only changes, P and W. No new C++ and no new key.
--
-- P: the slot-1 post on Cindarion holds two healers. Walk 2i (19 counted pulls): the slot-1 tank died while Cindarion
-- 50143 lived in 17 of 19 pulls, a median +122 s after he appeared (50-230 s), 89 % of his last-20 s damage from
-- Cindarion's melee and the Claw Rend bleed. In his last 30 s he was healed for a median 340 heal/s against 507
-- damage/s (0.65) while 21 % of the healers' output went to him and the rest to Fireball victims; the healers were
-- alive (9 of 10) and in range (6 within 40 yd) but out of mana only 4.5 % of the time. The row's `healers` = 2 puts
-- two of them on him (`ai_boss_tactic.healers`, 20261113200000). The reader covers any `hold_spot` row with
-- `healers > 0` under AiPlayerbot.DungeonRunTankPostHealers (already 1), with no slot or phase test
-- (BossTacticsActions.cpp:12622), and the seat pick still outranks the post's leash. `spell_id` stays 0, so this row
-- has no defensive-cooldown window.
--
-- W: no DPS on a whelp near a living Cindarion. Walk 2i: within 40 yd of him 28 % of the melee frames and 12 % of the
-- ranged frames were on whelps that attack nobody, and his loss over +15..+90 s tracked the DPS frames on him
-- (rho 0.80) and ran against the frames on whelps (rho -0.90). Cindarion needs about 300 s at that rate; the slot-1
-- tank lasts about 120 s. The row spares entry 11262 (Onyxian Whelp) while his blocker 50143 fights within the
-- kind's 50 yd, budget 0 ("not one more"): a free DPS already on a whelp takes the `kill_first` pick (Cindarion), and
-- the `add_crew` row (20261111160000) takes the whelps over once he dies. The kind is read straight from the table,
-- so it carries no health gate and no mode word.
--
-- Idempotent: the UPDATE names exactly the (249, 10184) slot-1 tank_post row on 50143 in the 41-65 band, and the
-- DELETE exactly the one (249, 11262) hold_kill row on 50143 that this file inserts. `healers` exists since
-- 20261113200000. Never edit this file once applied; add a newer migration.

UPDATE `ai_boss_tactic` SET `healers` = 2, `comment` = 'Onyxia 41-65 %: slot 1 holds Cindarion; 2 healers within 30 yd heal only this tank (walk 2i: the slot-1 tank died while Cindarion lived in 17 of 19 pulls, a median +122 s after he appeared; needs DungeonRunTankPost and DungeonRunTankPostHealers)' WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 50143 AND `priority` = 1 AND `hp_min` = 41 AND `hp_max` = 65;

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 11262 AND `kind` = 'hold_kill' AND `target_entry` = 50143;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 11262, 'hold_kill', 50143, 0, 0, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia P2: no DPS on a whelp 11262 within 50 yd while Cindarion 50143 lives (budget 0); a held dps takes his kill_first, and add_crew takes the whelps once he dies (walk 2i: whelps drew 33 % of the DPS near him, his loss 21 %/min against a 32 goal)');
