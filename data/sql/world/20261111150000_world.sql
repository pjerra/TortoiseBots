-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — park the walked Son of Hakkar under
-- crowd control and kill it late (r11-1). Walk 72's PRACTICE probe (task 2 of the park's practice stage) and:
--
--   The Sons are the fight's largest killer: 2359 of 5266 deaths (45 %, 9.4 a fight), and 123 of the 220 healer and
--   ranged deaths happen under the seek. A Son in the raid lives a median 37 s and dies with due_in a median 33 s, so
--   it has half a minute on the raid exactly where the raid stands stacked: 65 % of the Blood Siphon landings fall in
--   the 32-68 s stack after a siphon, 9.0 deaths a cast against 3.7 outside it.
--
--   Polymorph 12826 landed 3 of 3 on engaged Sons in the probe and held 41-43 s; the Son kept its full 16,786 HP while
--   polymorphed. A parked Son therefore costs no damage and no healer mana, and the release can be timed to the cycle.
--   The 40 s release lead is the first value: today a Son dies a median 25 s after it arrives, so a tank take from a
--   standing source plus the forced burn should land the death at due_in 15-24, with the forced burn at 15 s
--   (kSeekForceMs) as the net. The practice stage measures release -> death and the lead moves by a later migration.
--
--   6 s is the apply budget: a Polymorph is a 1.5 s cast, so 6 s is one cast, one resist and one retry. 2 re-applies:
--   a third break means something near the source keeps breaking it, and today's take and burn is cheaper.
--
--   AiPlayerbot.DungeonRunSeekPark (default 0) reads this row; off, the walk-out behaves as today.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_park' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'seek_park', 11357, 0, 2, 40, '', 6, 0, 0, 0, 0, 0, 'Hakkar: the walked Son of Hakkar is parked under Polymorph or Hibernate (up to 2 re-applies, 6 s to land) and released to a tank 40 s before the Blood Siphon');
