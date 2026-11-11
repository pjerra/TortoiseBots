-- Onyxia's Lair (map 249), walk 2f: no threat cap on her while she is airborne and no tank can reach her.
--
-- Walk 2e a-1: airborne Onyxia vetoed the casters' nukes through the threat cap (248 refusals with no reason at all,
-- nukes 19 % of caster frames, 2.5 %/min on her). Her tanks hold no threat on her in the air, so every damage dealer
-- read 100 % ("the tank has no threat: hold"). With AiPlayerbot.DungeonRunThreatFree = 1 a non-tank reads 0 there;
-- the row ships only with this kind in the loader (needs AiPlayerbot.DungeonRunThreatFree = 1).
--
-- Idempotent: the DELETE removes only the (249, 10184) threat_free row. Never edit this file once applied;
-- add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'threat_free';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'threat_free', 0, 0, 0, 10, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia hovering: no tank can reach her, so non-tanks read no threat cap on her while she stands >= 10 yd above them (needs AiPlayerbot.DungeonRunThreatFree); her threat list is wiped on landing; walk 2e a-1: 248 reasonless nuke refusals, 2.5 %/min');
