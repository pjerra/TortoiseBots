#pragma once

#include "ObjectGuid.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

class Creature;
class GameObject;
class Item;
class Player;
struct VendorItem;

namespace tbhost
{
namespace town
{

enum class TrainerKind : uint32_t
{
    Class = 0,
    Talent = 1,
    Tradeskill = 2,
    Pet = 3
};

struct TrainerSpellView
{
    uint32_t spellId = 0;
    uint32_t moneyCost = 0;
    uint32_t reqSkillLine = 0;
    uint32_t reqSkillRank = 0;
    std::array<uint32_t, 3> reqAbility{};
    uint8_t reqLevel = 0;
};

struct MailView
{
    uint64_t messageId = 0;
    uint8_t messageType = 0;
    uint8_t stationery = 0;
    uint32_t sender = 0;
    std::string subject;
    std::string body;
    uint64_t money = 0;
    bool hasItems = false;
    time_t deliverTime = 0;
    uint32_t checked = 0;
    uint8_t state = 0;
    std::vector<std::pair<uint64_t, uint32_t>> items; // (item_guid, item_template)
};

// NPC interaction (passes UNIT_NPC_FLAG_2_NONE on ForeverCore).
Creature* NpcIfCanInteract(Player* bot, ObjectGuid const& guid, uint32_t npcFlags);

// Vendor sell.
bool HasVendorStock(Creature const* vendor);
bool SellItem(Player* bot, Creature* vendor, Item* item, uint32_t count, std::string* failReason = nullptr);

// Vendor buy.
bool CheckVendorItemAllowed(Player* bot, Creature const* vendor, VendorItem const& vItem, std::string* refusalReason = nullptr);
bool BuyItemFromVendorSlot(Player* bot, Creature* vendor, uint32_t vendorSlot, uint32_t itemId, uint32_t count, std::string* refusalReason = nullptr);
bool BuyBackItem(Player* bot, ObjectGuid const& vendorGuid, uint32_t slot);

// Repair.
uint32_t RepairAll(Player* bot, Creature* vendor, float discountMod);
uint8_t CalcInventoryDurabilityPct(Player const* bot);

// Trainer.
bool IsTrainer(Creature const* creature);
bool IsTrainerOf(Creature const* creature, Player const* bot);
TrainerKind GetTrainerKind(Creature const* creature);
std::vector<TrainerSpellView> GetTrainerSpells(Creature const* creature);
bool CanTeachTrainerSpell(Player* bot, Creature* creature, TrainerSpellView const& spell, float discountMod, std::string* refusalReason = nullptr);
bool TeachTrainerSpell(Player* bot, Creature* creature, uint32_t spellId, std::string* refusalReason = nullptr);

// Mail (read-only & interactable mailbox lookup).
GameObject* FindNearestMailbox(Player* bot, float maxDistYd = 0.0f);
std::vector<MailView> GetMails(Player* bot);
Item* GetMailItem(Player* bot, uint64_t itemGuid);
uint32_t GetUnreadMailCount(Player* bot);
bool TakeMailMoney(Player* bot, ObjectGuid const& mailboxGuid, uint64_t mailId, uint64_t money);
bool TakeMailItem(Player* bot, ObjectGuid const& mailboxGuid, uint64_t mailId, uint64_t attachId);
bool DeleteMail(Player* bot, uint64_t mailId);

// Structured W2 log line: [W2] <verb> bot=<name> npc=<entry> copper_before=<before> copper_after=<after> ...
void LogErrand(char const* verb, Player const* bot, Creature const* npc, uint64_t copperBefore, uint64_t copperAfter, std::string const& detail = {});

} // namespace town
} // namespace tbhost
