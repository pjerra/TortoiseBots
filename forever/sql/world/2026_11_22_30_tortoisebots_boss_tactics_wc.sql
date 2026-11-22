-- Shared five-man boss tactics, Wailing Caverns (map 43), issue #138.
-- Facts: forever/data/waterys/wc/wc-casts.csv (SmartAI casts in the world DB at core
-- b441bf74, cast times from DB2 70959). Only casts with a cast time are listed here;
-- the runtime still asks the core whether the cast can be interrupted right now
-- (PlayerbotAI::IsInterruptableSpellCasting) and whether the bot knows an interrupt.
-- Aim (hypeer's WC run 8: the tank died first; Deviate Shambler packs): stop the
-- sleeps on the tank, the heals that drag packs out and the big Lightning Bolts.
-- No stand or cone row: WC has no verified stand point or frontal cone yet.
-- Idempotent: INSERT IGNORE on the table's primary key.

INSERT IGNORE INTO `ai_boss_tactic`
(`map`,`boss_entry`,`kind`,`target_entry`,`spell_id`,`priority`,`param`,`comment`) VALUES
-- Druid of the Fang: 70 spawns, the caster in most WC trash packs.
(43,3840,'interrupt',0,8040,0,0,'Druid of the Fang Druid''s Slumber 2.5s: sleeps the tank'),
(43,3840,'interrupt',0,5187,0,0,'Druid of the Fang Healing Touch 2.5s'),
(43,3840,'interrupt',0,9532,0,0,'Druid of the Fang Lightning Bolt 3s'),
(43,3840,'kill_first',3840,0,1,0,'already engaged Druid of the Fang first: it heals and sleeps the pack''s tank'),
-- Deviate Shambler: 24 spawns, heals itself and its pack.
(43,5761,'interrupt',0,7948,0,0,'Deviate Shambler Wild Regeneration 3s'),
-- The four Fanglords and Skum, Verdan.
(43,3669,'interrupt',0,8040,0,0,'Lord Cobrahn Druid''s Slumber 2.5s'),
(43,3669,'interrupt',0,9532,0,0,'Lord Cobrahn Lightning Bolt 3s'),
(43,3670,'interrupt',0,700,0,0,'Lord Pythas Sleep 1.5s'),
(43,3670,'interrupt',0,5187,0,0,'Lord Pythas Healing Touch 2.5s'),
(43,3670,'interrupt',0,9532,0,0,'Lord Pythas Lightning Bolt 3s'),
(43,3671,'interrupt',0,700,0,0,'Lady Anacondra Sleep 1.5s'),
(43,3671,'interrupt',0,5187,0,0,'Lady Anacondra Healing Touch 2.5s'),
(43,3671,'interrupt',0,9532,0,0,'Lady Anacondra Lightning Bolt 3s'),
(43,3673,'interrupt',0,700,0,0,'Lord Serpentis Sleep 1.5s'),
(43,3673,'interrupt',0,6778,0,0,'Lord Serpentis Healing Touch 3.5s'),
(43,3673,'interrupt',0,9532,0,0,'Lord Serpentis Lightning Bolt 3s'),
(43,3674,'interrupt',0,6254,0,0,'Skum Chained Bolt 1.8s'),
(43,5775,'interrupt',0,8142,0,0,'Verdan the Everliving Grasping Vines 1s');
