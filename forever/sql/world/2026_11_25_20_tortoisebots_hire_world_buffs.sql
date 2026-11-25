-- TortoiseBots on ForeverCore: the world-buff unlock quests sold at the six capital <Mercenary Hire> recruiters (issue
-- #298, package P5).
--
-- Model: a player earns the blessing once the real way, takes the matching unlock quest at a capital recruiter,
-- turns it in for a processing fee, and from then on can buy the blessing for gold. Quests 100000-100013 are one per
-- purchase per faction; contracts own 100050+. The fee is the classic negative `RewardMoney` (VMaNGOS RewOrReqMoney):
-- 200 gold for the three raid unlocks (Rally, Rend, Zandalar), 100 gold for the rest. Quest objectives 100000000-100000013
-- are reserved for these rows (the ID space >= 100000000 is free).
--
-- Credit-only creatures live in the recruiter NPC migration: 1000070 = Onyxia or Nefarian (Rally), 1000071 = one
-- opposite-faction kill in Silithus (five for the quest). The aura unlocks (Dire Maul, Sayge, Songflower) have no
-- objective: `tortoisebots_hire_worldbuff_aura` completes them when the player really receives the aura.
--
-- Idempotent: the owned rows are deleted before insertion. SECTION A targets the world database. SECTION B is the same
-- world database's spell_script_names table (the names bind the aura-credit script to the buff spells).
--
-- Faction masks: Alliance races 1,3,4,7 = 77; Horde races 2,5,6,8 = 178. Quests are level 60 only (quest_classic_level).

-- ============================ quests ============================

DELETE FROM `quest_template` WHERE `ID` BETWEEN 100000 AND 100013;
DELETE FROM `quest_objectives` WHERE `ID` BETWEEN 100000000 AND 100000013;
DELETE FROM `quest_offer_reward` WHERE `ID` BETWEEN 100000 AND 100013;
DELETE FROM `quest_classic_level` WHERE `ID` BETWEEN 100000 AND 100013;
DELETE FROM `quest_template_classic_level` WHERE `ID` BETWEEN 100000 AND 100013;
DELETE FROM `quest_classic_money` WHERE `ID` BETWEEN 100000 AND 100013;
DELETE FROM `creature_queststarter` WHERE `quest` BETWEEN 100000 AND 100013;
DELETE FROM `creature_questender` WHERE `quest` BETWEEN 100000 AND 100013;

INSERT INTO `quest_template`
(`ID`, `QuestType`, `QuestSortID`, `QuestInfoID`, `SuggestedGroupNum`, `RewardXPDifficulty`, `RewardMoneyDifficulty`,
 `Flags`, `AllowableRaces`, `LogTitle`, `LogDescription`, `QuestDescription`, `AreaDescription`, `QuestCompletionLog`)
VALUES
(100000, 2, 0, 0, 0, 0, 0, 0, 77,
 'Rallying Cry of the Dragonslayer',
 'Earn the Rallying Cry on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Onyxia or Nefarian.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100001, 2, 0, 0, 0, 0, 0, 0, 178,
 'Rallying Cry of the Dragonslayer',
 'Earn the Rallying Cry on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Onyxia or Nefarian.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100002, 2, 0, 0, 0, 0, 0, 0, 77,
 'Might of Stormwind',
 'Carry the Might of Stormwind once on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Rend Blackhand.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100003, 2, 0, 0, 0, 0, 0, 0, 178,
 'Warchief''s Blessing',
 'Carry the Warchief''s Blessing once on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Rend Blackhand.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100004, 2, 0, 0, 0, 0, 0, 0, 77,
 'Spirit of Zandalar',
 'Carry the Spirit of Zandalar once on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Hakkar the Soulflayer.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100005, 2, 0, 0, 0, 0, 0, 0, 178,
 'Spirit of Zandalar',
 'Carry the Spirit of Zandalar once on your own, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Slay Hakkar the Soulflayer.',
 'Return to a Mercenary Hire broker in a capital city and pay the 200 gold processing fee.'),
(100006, 2, 0, 0, 0, 0, 0, 0, 77,
 'Dire Maul Tribute',
 'Receive a Dire Maul tribute blessing once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive any of the three Dire Maul tribute blessings.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100007, 2, 0, 0, 0, 0, 0, 0, 178,
 'Dire Maul Tribute',
 'Receive a Dire Maul tribute blessing once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive any of the three Dire Maul tribute blessings.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100008, 2, 0, 0, 0, 0, 0, 0, 77,
 'Sayge''s Dark Fortune',
 'Receive one of Sayge''s fortunes once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive any of Sayge''s eight dark fortunes.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100009, 2, 0, 0, 0, 0, 0, 0, 178,
 'Sayge''s Dark Fortune',
 'Receive one of Sayge''s fortunes once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive any of Sayge''s eight dark fortunes.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100010, 2, 0, 0, 0, 0, 0, 0, 77,
 'Songflower Serenade',
 'Touch a corrupted Songflower once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive the Songflower Serenade.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100011, 2, 0, 0, 0, 0, 0, 0, 178,
 'Songflower Serenade',
 'Touch a corrupted Songflower once, then have a Mercenary Hire broker enter it in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Receive the Songflower Serenade.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100012, 2, 0, 0, 0, 0, 0, 0, 77,
 'Traces of Silithyst',
 'Fight for Silithus five times, then have a Mercenary Hire broker enter the blessing in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Defeat 5 Horde players in Silithus.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.'),
(100013, 2, 0, 0, 0, 0, 0, 0, 178,
 'Traces of Silithyst',
 'Fight for Silithus five times, then have a Mercenary Hire broker enter the blessing in the ledger.',
 'The brokers sell only blessings they have seen you carry yourself. Bring proof, and the blessing goes in the ledger.',
 'Defeat 5 Alliance players in Silithus.',
 'Return to a Mercenary Hire broker in a capital city and pay the 100 gold processing fee.');

INSERT INTO `quest_offer_reward` (`ID`, `RewardText`) VALUES
(100000, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100001, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100002, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100003, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100004, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100005, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100006, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100007, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100008, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100009, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100010, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100011, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100012, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.'),
(100013, 'The broker enters the blessing in the ledger. From now on the criers answer your coin.');

-- Objectives: MONSTER credit on the credit-only creatures. Rend and Hakkar are the bosses themselves (the core shares
-- the kill with the group); Onyxia/Nefarian share one credit because the quest accepts either.
INSERT INTO `quest_objectives`
(`ID`, `QuestID`, `Type`, `Order`, `StorageIndex`, `ObjectID`, `Amount`, `Flags`, `Visible`)
VALUES
(100000000, 100000, 0, 0, 0, 1000070, 1, 0, 1),
(100000001, 100001, 0, 0, 0, 1000070, 1, 0, 1),
(100000002, 100002, 0, 0, 0, 10429, 1, 0, 1),
(100000003, 100003, 0, 0, 0, 10429, 1, 0, 1),
(100000004, 100004, 0, 0, 0, 14834, 1, 0, 1),
(100000005, 100005, 0, 0, 0, 14834, 1, 0, 1),
(100000012, 100012, 0, 0, 0, 1000071, 5, 0, 1),
(100000013, 100013, 0, 0, 0, 1000071, 5, 0, 1);

-- Classic 1.60 quest metadata (the world-buff quests are level 60 and cost money at turn-in).
INSERT INTO `quest_classic_level` (`ID`, `QuestLevel`, `MinLevel`, `MaxLevel`) VALUES
(100000, 60, 60, 0), (100001, 60, 60, 0), (100002, 60, 60, 0), (100003, 60, 60, 0),
(100004, 60, 60, 0), (100005, 60, 60, 0), (100006, 60, 60, 0), (100007, 60, 60, 0),
(100008, 60, 60, 0), (100009, 60, 60, 0), (100010, 60, 60, 0), (100011, 60, 60, 0),
(100012, 60, 60, 0), (100013, 60, 60, 0);

INSERT INTO `quest_template_classic_level` (`ID`, `LevelType`, `QuestLevel`) VALUES
(100000, 2, 60), (100001, 2, 60), (100002, 2, 60), (100003, 2, 60),
(100004, 2, 60), (100005, 2, 60), (100006, 2, 60), (100007, 2, 60),
(100008, 2, 60), (100009, 2, 60), (100010, 2, 60), (100011, 2, 60),
(100012, 2, 60), (100013, 2, 60);

-- Turn-in fee: 200 gold for the raid unlocks (Rally, Rend, Zandalar), 100 gold for the rest.
INSERT INTO `quest_classic_money` (`ID`, `RewardMoney`, `RewardMoneyMaxLevel`) VALUES
(100000, -2000000, 0), (100001, -2000000, 0), (100002, -2000000, 0), (100003, -2000000, 0),
(100004, -2000000, 0), (100005, -2000000, 0),
(100006, -1000000, 0), (100007, -1000000, 0), (100008, -1000000, 0), (100009, -1000000, 0),
(100010, -1000000, 0), (100011, -1000000, 0), (100012, -1000000, 0), (100013, -1000000, 0);

-- ============================ capital recruiters: take and turn in ============================

INSERT INTO `creature_queststarter` (`id`, `quest`, `VerifiedBuild`)
SELECT c.entry, q.ID, 0
FROM (SELECT 1000006 AS entry UNION ALL SELECT 1000012 UNION ALL SELECT 1000017
      UNION ALL SELECT 1000018 UNION ALL SELECT 1000019 UNION ALL SELECT 1000025) AS c
CROSS JOIN `quest_template` q
WHERE q.ID BETWEEN 100000 AND 100013;

INSERT INTO `creature_questender` (`id`, `quest`, `VerifiedBuild`)
SELECT c.entry, q.ID, 0
FROM (SELECT 1000006 AS entry UNION ALL SELECT 1000012 UNION ALL SELECT 1000017
      UNION ALL SELECT 1000018 UNION ALL SELECT 1000019 UNION ALL SELECT 1000025) AS c
CROSS JOIN `quest_template` q
WHERE q.ID BETWEEN 100000 AND 100013;

-- ============================ SECTION B: spell_script_names (world database) ============================
-- Binds the module's aura-credit script to the buffs earned the real way: Dire Maul tribute (22817 is not sold because it
-- is a dummy aura here, but a real run still earns the unlock), the eight Sayge fortunes, Songflower.

DELETE FROM `spell_script_names` WHERE `ScriptName` = 'tortoisebots_hire_worldbuff_aura';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(22817, 'tortoisebots_hire_worldbuff_aura'),
(22818, 'tortoisebots_hire_worldbuff_aura'),
(22820, 'tortoisebots_hire_worldbuff_aura'),
(23735, 'tortoisebots_hire_worldbuff_aura'),
(23736, 'tortoisebots_hire_worldbuff_aura'),
(23737, 'tortoisebots_hire_worldbuff_aura'),
(23738, 'tortoisebots_hire_worldbuff_aura'),
(23766, 'tortoisebots_hire_worldbuff_aura'),
(23767, 'tortoisebots_hire_worldbuff_aura'),
(23768, 'tortoisebots_hire_worldbuff_aura'),
(23769, 'tortoisebots_hire_worldbuff_aura'),
(15366, 'tortoisebots_hire_worldbuff_aura');
