-- Onyxia's Lair (map 249, the 40-man raid): the air_target row (Onyxia kill plan, code item C1).
--
-- Onyxia's phase 2 (below 65 % health, core boss_onyxia.cpp) stops her at -57.75 -215.61, lifts her off with
-- Hover 17131 and flies her to one of 8 hover points about 20 yd above the arena floor. She is NOT flagged
-- IsEvadeBecauseTargetNotReachable on this core: that timer only counts while her movement is a chase
-- (Creature.cpp:916-933), and phase 2 switches combat movement off and flies her by point moves (boss_onyxia.cpp
-- ~576-609). What lost her: "current target" dropped her past the 60-yd sight ceiling (her hover points sit far from
-- the raid), the fight strategy handed "shoot" (a wand) to every bot, and the casters' reach chased her flight
-- height, which no floor path reaches; the raid's damage collapsed between
-- 65 % and 40 % (walks 1, 4 and 5 wiped at 61-62 %).
--
-- air_target (BossTacticsPolicy.h AirTargetKeep) keeps a boss a target for the ranged bots while it wears the
-- row's aura and is not evading: out to the 150-yd react distance, with the casters' own rotation instead of the
-- wand, and the caster's "reach spell" walks to the floor under her instead of chasing her flight height.
-- The aura is Hover 17131 (spell_template, cast by boss_onyxia.cpp on lift-off and removed on landing), the same
-- aura the module's Onyxia fight triggers already read (OnyxiasLairDungeonTriggers.h, OnyxiaAirborneTrigger).
--
-- Melee bots and tanks (PlayerbotAI::IsRanged false) are not covered: they keep the base target values and reach.
-- The row only acts for a bot whose "boss tactics" strategy is armed (AiPlayerbot.DungeonTactics = 1, or a bound
-- commander group); every other bot plays as before.
--
-- Idempotent: this row's key only.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'air_target' AND `target_entry` = 0 AND `spell_id` = 17131;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(249, 10184, 'air_target', 0, 17131, 0, 0, 'Onyxia: air phase, Hover 17131 marks the hover (boss_onyxia.cpp)');
