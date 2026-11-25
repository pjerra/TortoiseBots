// Package P4 of the hire port (issue #298): the gear-tier sets and the contract unlocks, the core/engine half.
// The catalog (tiers, quests, credit creatures, bosses, fees) is pure: host/hire/TbHireTiersPure.h, tested by
// forever/tools/test_hire_tiers_pure.cpp. This file loads the sets, equips them, resolves the player's unlocks,
// renders the contracts gossip page and counts boss kills. TortoiseBots reference: host/HireRecruiterScript.cpp
// (contracts page), host/HireUnlockKillAdapter.cpp (kill credit), runtime/HireProvisionService.cpp (equip pass).
#include "host/hire/TbHireTiers.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/hire/TbHireTiersPure.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbCompat_S5.h"      // WorldSchemaHasCompat, FieldTextCompat

#include "CellImpl.h"
#include "Creature.h"
#include "DBCEnums.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "ItemDefines.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "TbBotMgr.h"
#include "WorldSession.h"

#include "compat/botpch_forever.h"

#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAIStorage.h"

#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace hire
{
namespace
{

// Straight to the core logger: the compat shim above replaces the legacy `sLog` the TC_LOG_ macros expand, so they do
// not compile in a file that includes it.
template <typename... Args>
void LogAt(char const* logger, LogLevel level, fmt::format_string<Args...> message, Args&&... args)
{
    Log::instance()->OutMessage(logger, level, message, std::forward<Args>(args)...);
}

// ---- tier sets ----

std::mutex g_gearMutex;
std::vector<TierGearRow> g_gearRows;

char const* const kGearTable = "tortoise_bots_hire_gear";
char const* const kGearSelect = "SELECT `class`, `spec`, `race`, `tier`, `slot`, `item_id` FROM `tortoise_bots_hire_gear`";

std::set<std::string> GearColumns()
{
    return { "class", "spec", "race", "tier", "slot", "item_id" };
}

// A copy of the rows of one set: the equip pass destroys and creates items, so it never holds the cache lock.
std::vector<TierGearRow> SetRows(uint8 classId, char const* spec, uint8 tier)
{
    std::vector<TierGearRow> rows;
    if (!spec)
        return rows;
    std::lock_guard<std::mutex> lock(g_gearMutex);
    for (TierGearRow const& row : g_gearRows)
        if (row.classId == classId && row.tier == tier && std::strcmp(row.spec, spec) == 0)
            rows.push_back(row);
    return rows;
}

bool HasSet(uint8 classId, char const* spec, uint8 tier)
{
    if (!spec)
        return false;
    std::lock_guard<std::mutex> lock(g_gearMutex);
    return HasTierSet(g_gearRows.data(), g_gearRows.size(), classId, spec, tier);
}

// ---- contract kill store (characters DB, TB's table) ----

char const* const kKillTable = "tortoise_bots_hire_unlock_kills";
char const* const kKillCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_hire_unlock_kills` ("
    "`guid` bigint unsigned NOT NULL,"
    "`boss_entry` int unsigned NOT NULL,"
    "`killed_at` int unsigned NOT NULL DEFAULT 0,"
    "PRIMARY KEY (`guid`, `boss_entry`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

std::mutex g_killMutex;
std::map<uint32, std::set<uint32>> g_kills;
bool g_killCreateTried = false;

// ForeverCore aborts the world on a statement that names a missing table or column, so every write checks first.
bool KillTableReady()
{
    if (!g_killCreateTried)
    {
        g_killCreateTried = true;
        CharacterExecuteGuardedCompat(kKillCreate);
    }
    std::set<std::string> const columns = { "guid", "boss_entry", "killed_at" };
    return CharacterSchemaHasCompat(kKillTable, columns);
}

void LoadKills(uint32 guidLow)
{
    std::set<uint32> kills;
    if (KillTableReady())
    {
        QueryResult rows = CharacterQueryGuardedCompat(
            "SELECT `boss_entry` FROM `tortoise_bots_hire_unlock_kills` WHERE `guid` = " + std::to_string(guidLow));
        if (rows)
            do
            {
                kills.insert(rows->Fetch()[0].GetUInt32());
            } while (rows->NextRow());
    }
    std::lock_guard<std::mutex> lock(g_killMutex);
    g_kills[guidLow] = std::move(kills);
}

void DropKills(uint32 guidLow)
{
    std::lock_guard<std::mutex> lock(g_killMutex);
    g_kills.erase(guidLow);
}

// Records the encounter once per character, ever; false when it was already there.
bool AddKill(uint32 guidLow, uint32 encounter)
{
    {
        std::lock_guard<std::mutex> lock(g_killMutex);
        if (!g_kills[guidLow].insert(encounter).second)
            return false;
    }
    if (KillTableReady())
        CharacterExecuteGuardedCompat("INSERT IGNORE INTO `tortoise_bots_hire_unlock_kills` (`guid`, `boss_entry`, `killed_at`) VALUES ("
            + std::to_string(guidLow) + ", " + std::to_string(encounter) + ", "
            + std::to_string(uint32(GameTime::GetGameTime())) + ")");
    return true;
}

void DeleteKills(uint32 guidLow)
{
    DropKills(guidLow);
    if (KillTableReady())
        CharacterExecuteGuardedCompat(
            "DELETE FROM `tortoise_bots_hire_unlock_kills` WHERE `guid` = " + std::to_string(guidLow));
}

std::vector<uint32> RecordedKills(uint32 guidLow)
{
    std::vector<uint32> out;
    std::lock_guard<std::mutex> lock(g_killMutex);
    auto const it = g_kills.find(guidLow);
    if (it != g_kills.end())
        out.assign(it->second.begin(), it->second.end());
    return out;
}

// ---- contract page helpers ----

uint32 GuidLow(Player* player)
{
    return uint32(player->GetGUID().GetCounter());
}

// The quest-log value of one counter: the objective whose credit entry matches, read through the quest slot.
uint8 CounterHave(Player* player, UnlockCounter const& counter)
{
    uint32 const questId = QuestForTier(counter.tier);
    Quest const* quest = questId ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
    if (!quest)
        return 0;
    for (QuestObjective const& objective : quest->GetObjectives())
        if (objective.Type == QUEST_OBJECTIVE_MONSTER && objective.ObjectID == int32(counter.creditEntry))
        {
            int32 const have = player->GetQuestObjectiveData(questId, objective.ID);
            return have <= 0 ? 0 : uint8(std::min<int32>(have, 255));
        }
    return 0;
}

void AddGossipLine(Player* player, std::string const& text, uint32 sender, uint32 action)
{
    GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
    menu.AddMenuItem(0, -1, GossipOptionNpc::None, text, 0, GossipOptionFlags::None, std::nullopt, 0, 0,
        false, 0, "", std::nullopt, std::nullopt, sender, action);
}

// The contracts page's "< Back" row (sender 512): action 0 is the recruiter's front page. The page's own rows carry
// this sender as their action and refresh the contracts page instead.
constexpr uint32 kContractsActionRoot = 0;

// Only this branch's quest list, so the contracts page and the world-buff page never show each other's quests.
void FilterContractQuests(Player* player, Creature* recruiter)
{
    player->PrepareQuestMenu(recruiter->GetGUID());
    QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
    std::vector<std::pair<uint32, uint8>> kept;
    for (uint8 i = 0; i < menu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& item = menu.GetItem(i);
        if (item.QuestId >= kQuestFirst && item.QuestId <= kQuestLast)
            kept.emplace_back(item.QuestId, item.QuestIcon);
    }
    menu.ClearMenu();
    for (auto const& keep : kept)
        menu.AddMenuItem(keep.first, keep.second);
}

void ShowContractsCounterMenu(Player* player, Creature* recruiter, uint32 creditEntry)
{
    for (UnlockCounter const& counter : kUnlockCounters)
    {
        if (counter.creditEntry != creditEntry)
            continue;
        uint32 const questId = QuestForTier(counter.tier);
        if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        {
            ShowContractsMenu(player, recruiter);
            return;
        }
        player->PlayerTalkClass->ClearMenus();
        std::vector<uint32> const recorded = RecordedKills(GuidLow(player));
        uint8 const have = CounterHave(player, counter);
        AddGossipLine(player, std::string(counter.label) + ": " + std::to_string(uint32(have)) + "/"
            + std::to_string(uint32(counter.bossCount)), kSenderContractsFirst + 2, kSenderContractsFirst + 2);
        uint32 shown = 0;
        for (uint8 i = 0; i < counter.bossCount && shown < 28; ++i)
        {
            if (std::find(recorded.begin(), recorded.end(), counter.bosses[i].entry) != recorded.end())
                continue;
            AddGossipLine(player, counter.bosses[i].name, kSenderContractsFirst + 2, kSenderContractsFirst + 2);
            ++shown;
        }
        AddGossipLine(player, "< Back", kSenderContractsFirst + 2, kSenderContractsFirst + 2);
        FilterContractQuests(player, recruiter);
        player->PlayerTalkClass->SendGossipMenu(recruiter->GetEntry(), recruiter->GetGUID());
        return;
    }
    ShowContractsMenu(player, recruiter);
}

// Syncs first so kills since the page was last opened show at once, then lists the incomplete counters of every
// open contract. A roster that would overflow one line opens as a sub-page naming the missing bosses.
void ShowContractsMenuImpl(Player* player, Creature* recruiter)
{
    SyncContracts(player);
    player->PlayerTalkClass->ClearMenus();
    std::vector<uint32> const recorded = RecordedKills(GuidLow(player));
    uint32 shown = 0;
    for (UnlockCounter const& counter : kUnlockCounters)
    {
        uint32 const questId = QuestForTier(counter.tier);
        if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;
        uint8 const have = CounterHave(player, counter);
        if (have >= counter.bossCount)
            continue;
        std::string line;
        if (CounterLine(counter, have, recorded.data(), recorded.size(), line))
            AddGossipLine(player, line, kSenderContractsFirst + 2, kSenderContractsFirst + 2);
        else
            AddGossipLine(player, line, kSenderContractsFirst + 1, counter.creditEntry);
        if (++shown >= 28)
            break;
    }
    if (!shown)
        AddGossipLine(player, "No contract in progress. Take one from this recruiter's quests.",
            kSenderContractsFirst + 2, kSenderContractsFirst + 2);
    AddGossipLine(player, "< Back", kSenderContractsFirst + 2, kContractsActionRoot);
    FilterContractQuests(player, recruiter);
    player->PlayerTalkClass->SendGossipMenu(recruiter->GetEntry(), recruiter->GetGUID());
}

uint32 RewardedMaskAccountWide(Player* player)
{
    uint32 mask = 0;
    WorldSession* session = player->GetSession();
    if (!session)
        return mask;
    uint32 const accountId = session->GetAccountId();
    if (!accountId)
        return mask;
    if (!CharacterSchemaHasCompat("character_queststatus_rewarded", { "guid", "quest" })
        || !CharacterSchemaHasCompat("characters", { "guid", "account" }))
        return mask;
    QueryResult rows = CharacterQueryGuardedCompat(
        "SELECT r.`quest` FROM `character_queststatus_rewarded` r INNER JOIN `characters` c ON c.`guid` = r.`guid`"
        " WHERE c.`account` = " + std::to_string(accountId) + " AND r.`quest` BETWEEN "
        + std::to_string(kQuestFirst) + " AND " + std::to_string(kQuestLast));
    if (rows)
        do
        {
            uint8 const tier = TierForQuest(rows->Fetch()[0].GetUInt32());
            if (tier >= 1 && tier <= kTierMax)
                mask |= 1u << (tier - 1);
        } while (rows->NextRow());
    return mask;
}

// All four Horsemen are one boss. The last one to die leaves his brothers' corpses in the same room; anything still
// alive near the corpse means this is not the last, so no credit yet.
struct HorsemanFilter
{
    Creature const* killed;

    bool operator()(Unit* unit) const
    {
        if (!unit || unit->GetGUID() == killed->GetGUID())
            return false;
        uint32 const entry = unit->GetEntry();
        for (uint32 member : kFourHorsemen)
            if (member == entry)
                return true;
        return false;
    }
};

bool HorsemenStillStanding(Creature* killed)
{
    std::list<Creature*> found;
    HorsemanFilter filter{ killed };
    Trinity::CreatureListSearcher<HorsemanFilter> searcher(killed, found, filter);
    Cell::VisitGridObjects(killed, searcher, 200.0f);
    for (Creature* creature : found)
        if (creature && creature->IsAlive())
            return true;
    return false;
}

void CreditKill(Player* member, uint32 encounter, UnlockCounter const& counter)
{
    if (!member)
        return;
    uint32 const questId = QuestForTier(counter.tier);
    if (!questId || member->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE
        || member->GetQuestRewardStatus(questId))
        return;
    if (!AddKill(GuidLow(member), encounter))
        return;
    // The core walks the open quests and bumps the objective whose creature entry matches.
    member->KilledMonsterCredit(counter.creditEntry);
}

class HireContractKillScript final : public PlayerScript
{
public:
    HireContractKillScript() : PlayerScript("tortoisebots_hire_contract_kills") { }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed || !sPlayerbotAIConfig.hireGearTiersEnabled)
            return;
        UnlockBoss const* boss = nullptr;
        UnlockCounter const* counter = CounterForKill(killed->GetEntry(), &boss);
        if (!counter || !boss)
            return;
        uint32 const encounter = EncounterKey(killed->GetEntry());
        if (IsGroupedEncounter(encounter) && HorsemenStillStanding(killed))
            return;

        // The core calls this hook once, for the player who landed the blow. A bot or a pet landing it would leave
        // the real party uncredited, so fan out to every real group member in reward distance (TB's rule).
        std::vector<CreditCandidate> roster;
        std::vector<std::pair<uint32, Player*>> byLow;
        byLow.emplace_back(GuidLow(killer), killer);
        if (Group* group = killer->GetGroup())
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                if (slot.guid == killer->GetGUID())
                    continue;
                Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
                if (!member || !member->IsInWorld())
                    continue;
                CreditCandidate candidate;
                candidate.guidLow = uint32(slot.guid.GetCounter());
                candidate.isBot = BotMgr::Instance().IsBot(slot.guid);
                candidate.inRewardDistance = member->IsAtGroupRewardDistance(killed);
                roster.push_back(candidate);
                byLow.emplace_back(candidate.guidLow, member);
            }
        std::vector<uint32> targets;
        CreditTargets(GuidLow(killer), roster.data(), roster.size(), targets);
        for (uint32 guidLow : targets)
        {
            Player* member = nullptr;
            for (auto const& pair : byLow)
                if (pair.first == guidLow)
                {
                    member = pair.second;
                    break;
                }
            if (!member || BotMgr::Instance().IsBot(member->GetGUID()))
                continue;
            CreditKill(member, encounter, *counter);
            uint32 const linkedEntry = LinkedCreditFor(killed->GetEntry());
            if (linkedEntry == 0 || linkedEntry == encounter)
                continue;
            UnlockBoss const* linkedBoss = nullptr;
            if (UnlockCounter const* linked = CounterForKill(linkedEntry, &linkedBoss))
                CreditKill(member, linkedEntry, *linked);
        }
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        if (!sPlayerbotAIConfig.hireGearTiersEnabled)
        {
            DropKills(GuidLow(player));
            return;
        }
        LoadKills(GuidLow(player));
        SyncContracts(player);
    }

    void OnLogout(Player* player) override
    {
        if (player)
            DropKills(GuidLow(player));
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        if (!guid || !guid.IsPlayer())
            return;
        DeleteKills(uint32(guid.GetCounter()));
    }
};

} // namespace

void LoadAtStartup()
{
    std::vector<TierGearRow> rows;
    if (WorldSchemaHasCompat(kGearTable, GearColumns()))
    {
        QueryResult result = WorldQueryCompat(kGearSelect);
        if (result)
            do
            {
                Field* fields = result->Fetch();
                TierGearRow row;
                row.classId = fields[0].GetUInt8();
                std::string const spec = FieldTextCompat(fields[1]);
                size_t const length = std::min(spec.size(), sizeof(row.spec) - 1);
                std::memcpy(row.spec, spec.data(), length);
                row.spec[length] = '\0';
                row.race = fields[2].GetUInt8();
                row.tier = fields[3].GetUInt8();
                row.slot = fields[4].GetUInt8();
                row.itemId = fields[5].GetUInt32();
                rows.push_back(row);
            } while (result->NextRow());
    }
    size_t const count = rows.size();
    {
        std::lock_guard<std::mutex> lock(g_gearMutex);
        g_gearRows.swap(rows);
    }
    if (count == 0)
        LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots: no hire gear sets loaded; every hire keeps its levelled kit");
    else
        LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots: loaded {} hire gear rows", count);
}

void RegisterScripts()
{
    new HireContractKillScript();
}

float TierCostMultiplier(uint8 gearTier)
{
    if (gearTier > kTierMax)
        gearTier = kTierMax;
    return sPlayerbotAIConfig.hireGearTierCostMult[gearTier];
}

uint8 HighestUnlockedTier(Player* hirer)
{
    uint8 const start = uint8(std::min<uint32>(sPlayerbotAIConfig.hireGearStartTier, kTierMax));
    if (!hirer)
        return start;
    uint32 mask = 0;
    if (sPlayerbotAIConfig.hireGearUnlockAccountWide)
        mask = RewardedMaskAccountWide(hirer);
    else
        for (uint8 tier = 1; tier <= kTierMax; ++tier)
            if (hirer->GetQuestRewardStatus(QuestForTier(tier)))
                mask |= 1u << (tier - 1);
    return HighestTierFromMask(start, mask);
}

bool EquipTierSet(Player* bot, uint8 tier, int specIndex)
{
    if (!bot || !TierSetEligible(bot->GetLevel(), sPlayerbotAIConfig.hireGearTiersEnabled, tier))
        return false;
    uint8 const classId = bot->GetClass();

    // The set's spec key: the menu row the hire asked for, else the bot's own spec tab (P2 sets it before the gear
    // pass), else the role's first menu row. The first key with rows at this tier wins.
    char const* key = specIndex >= 0 ? HireGearSpecKey(classId, specIndex) : nullptr;
    if (!key || !HasSet(classId, key, tier))
    {
        int8 const forcedTab = PlayerbotAIStorage::Instance().GetPlayerForcedSpecTab(bot->GetGUID());
        uint8 const role = uint8(AiFactory::GetPlayerRoles(bot));
        if (forcedTab >= 0 && forcedTab <= 2)
        {
            char const* tabKey = TierSpecKeyForTab(classId, forcedTab, role);
            if (tabKey && HasSet(classId, tabKey, tier))
                key = tabKey;
        }
        if (!key || !HasSet(classId, key, tier))
        {
            char const* roleKey = HireGearRoleFallbackKey(classId, role);
            if (roleKey && HasSet(classId, roleKey, tier))
                key = roleKey;
        }
    }
    if (!key || !HasSet(classId, key, tier))
        return false;

    std::vector<TierGearRow> const set = SetRows(classId, key, tier);
    for (TierGearRow const& row : set)
    {
        uint8 const slot = row.slot;
        if (slot >= EQUIPMENT_SLOT_END)
            continue;
        Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (row.itemId == 0)
        {
            // A row with item 0 clears the slot (a two-handed set's off hand).
            if (oldItem)
                bot->DestroyItem(oldItem->GetBagSlot(), oldItem->GetSlot(), true);
            continue;
        }
        if (oldItem && oldItem->GetEntry() == row.itemId)
            continue;
        ItemTemplate const* proto = sObjectMgr.GetItemTemplate(row.itemId);
        if (!proto)
            continue;
        if (slot == EQUIPMENT_SLOT_MAINHAND && proto->GetInventoryType() == INVTYPE_2HWEAPON)
            if (Item* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                bot->DestroyItem(offhand->GetBagSlot(), offhand->GetSlot(), true);
        uint16 dest = 0;
        if (bot->CanEquipNewItem(slot, dest, row.itemId, oldItem != nullptr) != EQUIP_ERR_OK)
            continue;
        if (oldItem)
            bot->DestroyItem(oldItem->GetBagSlot(), oldItem->GetSlot(), true);
        bot->EquipNewItem(dest, row.itemId, ItemContext::NONE, true);
    }
    return true;
}

std::string TierName(uint8 tier)
{
    char const* name = TierDisplayName(tier);
    return name ? std::string(name) : std::string();
}

bool ContractsVisible(Player* player, Creature* recruiter)
{
    if (!player || !recruiter)
        return false;
    return ContractsVisibleFor(sPlayerbotAIConfig.hireGearTiersEnabled, IsCapitalRecruiter(recruiter->GetEntry()),
        BotMgr::Instance().IsBot(player->GetGUID()), player->GetLevel());
}

void ShowContractsMenu(Player* player, Creature* recruiter)
{
    if (!player || !recruiter)
        return;
    ShowContractsMenuImpl(player, recruiter);
}

bool OnContractsSelect(Player* player, Creature* recruiter, uint32 sender, uint32 action)
{
    if (!player || !recruiter || !ContractsVisible(player, recruiter))
        return false;
    if (sender == kSenderContractsFirst)
    {
        ShowContractsMenu(player, recruiter);
        return true;
    }
    if (sender == kSenderContractsFirst + 1)
    {
        ShowContractsCounterMenu(player, recruiter, action);
        return true;
    }
    if (sender == kSenderContractsFirst + 2)
    {
        if (action == kContractsActionRoot)
            recruiter->AI()->OnGossipHello(player);
        else
            ShowContractsMenu(player, recruiter);
        return true;
    }
    return false;
}

void SyncContracts(Player* player)
{
    if (!player || !sPlayerbotAIConfig.hireGearTiersEnabled)
        return;
    if (BotMgr::Instance().IsBot(player->GetGUID()))
        return;
    std::vector<uint32> const recorded = RecordedKills(GuidLow(player));
    if (recorded.empty())
        return;
    for (UnlockCounter const& counter : kUnlockCounters)
    {
        uint32 const questId = QuestForTier(counter.tier);
        if (!questId || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;
        uint8 const current = CounterHave(player, counter);
        uint8 const target = CounterTarget(counter, recorded.data(), recorded.size(), current);
        for (uint8 i = current; i < target; ++i)
            player->KilledMonsterCredit(counter.creditEntry);
    }
}

} // namespace hire
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace hire
{

void LoadAtStartup() { }
void RegisterScripts() { }
float TierCostMultiplier(uint8) { return 1.0f; }
uint8 HighestUnlockedTier(Player*) { return 0; }
bool EquipTierSet(Player*, uint8, int) { return false; }
std::string TierName(uint8) { return std::string(); }
bool ContractsVisible(Player*, Creature*) { return false; }
void ShowContractsMenu(Player*, Creature*) { }
bool OnContractsSelect(Player*, Creature*, uint32, uint32) { return false; }
void SyncContracts(Player*) { }

} // namespace hire
} // namespace TortoiseBots

#endif
