-- Hire contracts: the six tier-unlock quests (100050-100055) at the capital <Mercenary Hire> recruiters, the
-- invisible credit creatures they count, and the quest relations that hand them out and take them back
-- (TbHireTiers.cpp, catalog in TbHireTiersPure.h; plan forever/docs/HIRE-PORT-PLAN.md).
--
-- Unlock = the quest is rewarded (HighestUnlockedTier reads character_queststatus_rewarded). Each quest is a boss
-- contract: one MONSTER objective per counter (Type 0, ObjectID = the credit creature, Amount = the bosses that
-- count); the module grants the credit per boss kill, and the gold due is the classic negative RewardMoney, which the
-- core requires and deducts at turn-in (Quest::GetClassicRequiredMoney, Player.cpp). The PrevQuestID chain makes the
-- tiers a ladder; tier 1 is open at 60.
--
-- TurtleBots' 90100-90106 and 95110-95121 ids are partly taken on Forever (data/waterys/id-ranges.md): the hire
-- contracts take 100050-100055 (100000-100013 are the world-buff contracts), credit creatures keep their plan ids
-- 1000080-1000091, of which only the eight counters that survive are
-- used (1000083/86/88/91 were the Lower Karazhan, Timbermaw Hold, Emerald Sanctum and Tower of Karazhan counters).
-- Turtle-only raids are not in the catalog, so neither are their bosses.
--
-- Capital recruiters (P3's entries): Stormwind 1000017, Ironforge 1000006, Darnassus 1000012, Orgrimmar 1000025,
-- Undercity 1000018, Thunder Bluff 1000019. This file only adds the relations; the templates come from P3's NPC file.
-- English only (no locale rows). Idempotent: every range is deleted before re-insertion.

DELETE FROM `creature_queststarter` WHERE `quest` BETWEEN 100050 AND 100055;
DELETE FROM `creature_questender` WHERE `quest` BETWEEN 100050 AND 100055;
DELETE FROM `quest_objectives` WHERE `QuestID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_template_addon` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_template_classic_level` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_classic_level` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_classic_money` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_offer_reward` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_request_items` WHERE `ID` BETWEEN 100050 AND 100055;
DELETE FROM `quest_template` WHERE `ID` BETWEEN 100050 AND 100055;

-- ============================ quests ============================

INSERT INTO `quest_template` (`ID`, `QuestType`, `SuggestedGroupNum`, `Flags`, `AllowableRaces`, `LogTitle`, `LogDescription`, `QuestDescription`, `AreaDescription`, `QuestCompletionLog`, `VerifiedBuild`) VALUES
(100050, 2, 5, 0, 18446744073709551615,
 'Proven Before the Raid',
 'Slay the dungeon lords of Blackrock Depths, Blackrock Spire, Scholomance, Stratholme and Dire Maul, then pay the guild''s due of 50 gold.',
 'So you want veterans in company kit, and you want them now. Fair enough. But no veteran signs with a commander who has not bled a little first.$B$BShow me the lords of Blackrock Depths, Blackrock Spire, Scholomance, Stratholme and Dire Maul dead at your hand, and pay the guild''s due of fifty gold. Then your next hires march in the company''s Pre-raid kit.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0),
(100051, 2, 10, 0, 18446744073709551615,
 'Proven in Zul''Gurub',
 'Slay the bosses of Zul''Gurub and the Ruins of Ahn''Qiraj, then pay the guild''s due of 100 gold.',
 'The Pre-raid kit earned you a hearing. The Zul''Gurub kit wants more: blood in Zul''Gurub and in the Ruins of Ahn''Qiraj.$B$BPut every one of those bosses down and pay the guild''s due of one hundred gold, and your hires dress for the jungle.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0),
(100052, 2, 10, 0, 18446744073709551615,
 'Proven in the Molten Core',
 'Slay the bosses of the Molten Core and Onyxia herself, then pay the guild''s due of 200 gold.',
 'Ragnaros does not care about your ambitions, and neither do his servants. Show the veterans the Molten Core bosses and Onyxia herself dead, and they will listen.$B$BThe due is two hundred gold. Hot work, $C, but the company''s Molten Core kit is worth it.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0),
(100053, 2, 15, 0, 18446744073709551615,
 'Proven in Blackwing Lair',
 'Slay the bosses of Blackwing Lair, then pay the guild''s due of 300 gold.',
 'Nefarian''s brood is the price of the Blackwing Lair kit, and the veterans will take nothing less.$B$BBreak the Lair''s bosses and pay three hundred gold for the guild''s due, and your hires march in the company''s Blackwing Lair kit.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0),
(100054, 2, 15, 0, 18446744073709551615,
 'Proven in Ahn''Qiraj',
 'Slay the bosses of the Temple of Ahn''Qiraj, then pay the guild''s due of 400 gold.',
 'The Temple of Ahn''Qiraj. C''Thun stirs behind its walls, and only proven killers come back out. The veterans want proof you are one of them.$B$BSilence the Temple''s bosses and pay the due of four hundred gold. Then your hires march in the company''s Ahn''Qiraj kit.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0),
(100055, 2, 20, 0, 18446744073709551615,
 'Proven in Naxxramas',
 'Slay the bosses of Naxxramas, then pay the guild''s due of 500 gold.',
 'Naxxramas floats over the Plaguelands, and Kel''Thuzad''s dead do not scare easily. Neither do our veterans, which is why they ask this of you.$B$BClear the necropolis and pay five hundred gold of the guild''s due. The Naxxramas kit will be yours to give.',
 'The company''s contracts are countersigned at a Mercenary Hire broker in a capital city.',
 'Return to a Mercenary Hire broker in a capital city.',
 0);

INSERT INTO `quest_template_addon` (`ID`, `PrevQuestID`) VALUES
(100050, 0),
(100051, 100050),
(100052, 100051),
(100053, 100052),
(100054, 100053),
(100055, 100054);

-- Level 60 contracts: the log shows them as level 60, they are offered from 60 and never expire.
INSERT INTO `quest_template_classic_level` (`ID`, `LevelType`, `QuestLevel`) VALUES
(100050, 2, 60), (100051, 2, 60), (100052, 2, 60), (100053, 2, 60), (100054, 2, 60), (100055, 2, 60);

INSERT INTO `quest_classic_level` (`ID`, `QuestLevel`, `MinLevel`, `MaxLevel`) VALUES
(100050, 60, 60, 0), (100051, 60, 60, 0), (100052, 60, 60, 0), (100053, 60, 60, 0), (100054, 60, 60, 0), (100055, 60, 60, 0);

-- Negative RewardMoney = the gold due: the core refuses to reward the quest without it and deducts it at turn-in.
INSERT INTO `quest_classic_money` (`ID`, `RewardMoney`, `RewardMoneyMaxLevel`) VALUES
(100050, -500000, 0),
(100051, -1000000, 0),
(100052, -2000000, 0),
(100053, -3000000, 0),
(100054, -4000000, 0),
(100055, -5000000, 0);

INSERT INTO `quest_offer_reward` (`ID`, `RewardText`) VALUES
(100050, 'That is the proof I wanted. Pre-raid kit is unlocked: choose it at the equipment step, or say "hire ... t1".'),
(100051, 'Zul''Gurub and the Ruins behind you, and the due paid. The Zul''Gurub kit is unlocked.'),
(100052, 'The Core quenched, Onyxia fallen, the due paid. The Molten Core kit is unlocked.'),
(100053, 'Nefarian''s brood broken and the due paid. Impressive. The Blackwing Lair kit is unlocked.'),
(100054, 'The Temple silenced and the due paid. Even the old hands are nodding. The Ahn''Qiraj kit is unlocked.'),
(100055, 'Naxxramas cleansed and the due paid. There is nothing finer: the Naxxramas kit is unlocked.');

INSERT INTO `quest_request_items` (`ID`, `CompletionText`) VALUES
(100050, 'The dungeon lords still stand, $N. Come back when they do not, with the due in hand.'),
(100051, 'The jungle bosses still live, $N. And the due is one hundred gold.'),
(100052, 'The Firelord still burns, $N. Two hundred gold, and every boss of the Core dead.'),
(100053, 'Nefarian still laughs, $N. Come back with his brood broken and the three hundred gold.'),
(100054, 'C''Thun still whispers, $N. Four hundred gold, and the Temple silent.'),
(100055, 'The necropolis still floats, $N. Five hundred gold, and every one of its bosses dead.');

-- One objective per counter (TbHireTiersPure.h kUnlockCounters): the credit creature, its boss count, and the label
-- the contracts page shows. StorageIndex is the counter slot inside its quest.
INSERT INTO `quest_objectives` (`ID`, `QuestID`, `Type`, `Order`, `StorageIndex`, `ObjectID`, `Amount`, `Flags`, `Visible`, `Description`, `VerifiedBuild`) VALUES
(10010000, 100050, 0, 0, 0, 1000080, 9, 0, 1, 'Dungeon lords slain', 0),
(10010001, 100051, 0, 0, 0, 1000081, 8, 0, 1, 'Zul''Gurub bosses slain', 0),
(10010002, 100051, 0, 1, 1, 1000082, 6, 0, 1, 'Ruins of Ahn''Qiraj bosses slain', 0),
(10010003, 100052, 0, 0, 0, 1000084, 10, 0, 1, 'Molten Core bosses slain', 0),
(10010004, 100052, 0, 1, 1, 1000085, 1, 0, 1, 'Onyxia slain', 0),
(10010005, 100053, 0, 0, 0, 1000087, 8, 0, 1, 'Blackwing Lair bosses slain', 0),
(10010006, 100054, 0, 0, 0, 1000089, 7, 0, 1, 'Temple of Ahn''Qiraj bosses slain', 0),
(10010007, 100055, 0, 0, 0, 1000090, 15, 0, 1, 'Naxxramas bosses slain', 0);

-- ============================ credit creatures ============================
-- Credit-only templates: never spawned, no gossip, no loot. Ids keep the plan's mapping so P3's NPC file can carry the
-- same rows; the invisible display is the one the world DB's own credit rows use.

DELETE FROM `creature_template` WHERE `entry` IN (1000080, 1000081, 1000082, 1000084, 1000085, 1000087, 1000089, 1000090);
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (1000080, 1000081, 1000082, 1000084, 1000085, 1000087, 1000089, 1000090);

INSERT INTO `creature_template` (`entry`, `KillCredit1`, `KillCredit2`, `name`, `femaleName`, `subname`, `TitleAlt`, `IconName`, `RequiredExpansion`, `VignetteID`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `Classification`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `unit_flags3`, `family`, `trainer_class`, `type`, `VehicleId`, `AIName`, `MovementType`, `ExperienceModifier`, `RacialLeader`, `movementId`, `WidgetSetID`, `WidgetSetUnitConditionID`, `RegenHealth`, `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `StringId`, `VerifiedBuild`) VALUES
(1000080, 0, 0, 'Hire Unlock Credit: Dungeons', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000081, 0, 0, 'Hire Unlock Credit: Zul''Gurub', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000082, 0, 0, 'Hire Unlock Credit: Ruins of Ahn''Qiraj', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000084, 0, 0, 'Hire Unlock Credit: Molten Core', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000085, 0, 0, 'Hire Unlock Credit: Onyxia', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000087, 0, 0, 'Hire Unlock Credit: Blackwing Lair', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000089, 0, 0, 'Hire Unlock Credit: Temple of Ahn''Qiraj', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0),
(1000090, 0, 0, 'Hire Unlock Credit: Naxxramas', '', '', NULL, NULL, 0, 0, 35, 0, 1, 1.14286, 1, 0, 0, 2000, 2000, 1, 1, 1, 0, 0, 0, 0, 0, 7, 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0, '', NULL, 0);

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(1000080, 0, 11686, 1, 0, 0),
(1000081, 0, 11686, 1, 0, 0),
(1000082, 0, 11686, 1, 0, 0),
(1000084, 0, 11686, 1, 0, 0),
(1000085, 0, 11686, 1, 0, 0),
(1000087, 0, 11686, 1, 0, 0),
(1000089, 0, 11686, 1, 0, 0),
(1000090, 0, 11686, 1, 0, 0);

-- ============================ quest givers ============================
-- All six contracts on all six capital recruiters (the company is neutral); start and turn-in at the same NPC.

INSERT INTO `creature_queststarter` (`id`, `quest`, `VerifiedBuild`) VALUES
(1000017, 100050, 0), (1000006, 100050, 0), (1000012, 100050, 0), (1000025, 100050, 0), (1000018, 100050, 0), (1000019, 100050, 0),
(1000017, 100051, 0), (1000006, 100051, 0), (1000012, 100051, 0), (1000025, 100051, 0), (1000018, 100051, 0), (1000019, 100051, 0),
(1000017, 100052, 0), (1000006, 100052, 0), (1000012, 100052, 0), (1000025, 100052, 0), (1000018, 100052, 0), (1000019, 100052, 0),
(1000017, 100053, 0), (1000006, 100053, 0), (1000012, 100053, 0), (1000025, 100053, 0), (1000018, 100053, 0), (1000019, 100053, 0),
(1000017, 100054, 0), (1000006, 100054, 0), (1000012, 100054, 0), (1000025, 100054, 0), (1000018, 100054, 0), (1000019, 100054, 0),
(1000017, 100055, 0), (1000006, 100055, 0), (1000012, 100055, 0), (1000025, 100055, 0), (1000018, 100055, 0), (1000019, 100055, 0);

INSERT INTO `creature_questender` (`id`, `quest`, `VerifiedBuild`) VALUES
(1000017, 100050, 0), (1000006, 100050, 0), (1000012, 100050, 0), (1000025, 100050, 0), (1000018, 100050, 0), (1000019, 100050, 0),
(1000017, 100051, 0), (1000006, 100051, 0), (1000012, 100051, 0), (1000025, 100051, 0), (1000018, 100051, 0), (1000019, 100051, 0),
(1000017, 100052, 0), (1000006, 100052, 0), (1000012, 100052, 0), (1000025, 100052, 0), (1000018, 100052, 0), (1000019, 100052, 0),
(1000017, 100053, 0), (1000006, 100053, 0), (1000012, 100053, 0), (1000025, 100053, 0), (1000018, 100053, 0), (1000019, 100053, 0),
(1000017, 100054, 0), (1000006, 100054, 0), (1000012, 100054, 0), (1000025, 100054, 0), (1000018, 100054, 0), (1000019, 100054, 0),
(1000017, 100055, 0), (1000006, 100055, 0), (1000012, 100055, 0), (1000025, 100055, 0), (1000018, 100055, 0), (1000019, 100055, 0);
