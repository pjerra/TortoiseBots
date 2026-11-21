#pragma once

#include "ObjectGuid.h"

#include <cstdint>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Player;

namespace tbhost
{
namespace prof
{

struct SkillView
{
    uint32_t skillId = 0;
    uint16_t value = 0;
    uint16_t maxValue = 0;
    uint16_t step = 0;
    std::string name;
};

// Read bot's learned professions (primary + supported secondary, excluding fishing).
std::vector<SkillView> GetSkills(Player const* bot);
std::string FormatStateProf(Player const* bot);

// Learn a profession (or next rank) from a nearby tradeskill trainer (creature_trainer Type 2).
bool LearnProfession(Player* bot, uint32_t skillId, std::string& code, std::string& detail);

// Gather a specific node (or skinnable corpse) in sight using GatherNodePolicy + core spells / slice-9 object path.
bool OpenNode(Player* bot, ObjectGuid const& guid, float maxDistYd, std::string& code, std::string& detail);
bool GatherNearestInSight(Player* bot, float maxDistYd, std::string& code, std::string& detail);
bool LootOpenNode(Player* bot, ObjectGuid const& guid);

// Craft a recipe spell (up to `count` times) using CraftForSelfPolicy + HandleCastSpellOpcode.
bool Craft(Player* bot, uint32_t spellId, uint32_t count, std::string& code, std::string& detail);

// Multi-tick gather/craft errand lifecycle (ordered gather/craft, free-kit gather, and W9 guild orders).
bool StartGatherNearest(Player* bot, float maxDistYd, uint32_t orderSeq, std::string& stateOut, std::string& detail, uint32_t targetItemId = 0);
bool StartCraft(Player* bot, uint32_t spellId, uint32_t count, uint32_t orderSeq, std::string& stateOut, std::string& detail);
void TickErrands(Player* bot, uint32_t diffMs);
bool PollOrderErrand(Player const* bot, uint32_t orderSeq, std::string& outResult);
void CancelOrderErrand(Player const* bot);
bool HasActiveErrand(Player const* bot);
std::string ActiveErrandPhase(Player const* bot);

// Helpers for guild orders (W9) and AH trade-good reserve (W8).
bool HasGatherableNodeInSight(Player* bot, float maxDistYd, uint32_t targetItemId = 0);
uint32_t FindCraftSpellForItem(Player* bot, uint32_t itemId);
bool IsReagentForBotProfessions(Player const* bot, uint32_t itemId);
bool IsReagentForItem(uint32_t craftedItemId, uint32_t reagentItemId);

// Free-bot periodic gather tick (gated by TortoiseBots.World.Professions = 0, max 150 yd in current zone).
bool AdvanceFreeGatherTimer(uint64_t botGuidCounter, uint32_t diffMs);
void ClearFreeGatherTimer(uint64_t botGuidCounter);
void ClearBotErrands(ObjectGuid const& botGuid);
void TickFreeGather(Player* bot);

} // namespace prof
} // namespace tbhost
