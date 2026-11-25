// Hired companions on ForeverCore: the owner of runtime/HireLifecycle.h (package P1 of the hire port). The ledger row
// is written once per hire and deleted with the character; a hire lives while its hirer is online plus the disconnect
// grace, and every end is a logoff and a character deletion, never a return to the pool.
//
// Deletion is the irreversible half, so it waits for the session to be gone and then re-reads the three durable facts
// the guard (runtime/policy/hire/HireDeletionPolicy.h) needs: the `tb_hire` row, an account proven to be a hire account
// (host/hire/TbHireAccounts.h), and `characters.account` still equal to the ledger's account. One character per world
// tick, as the pool reset does.
#include "runtime/HireLifecycle.h"

#include "host/hire/TbHireAccounts.h"
#include "host/hire/TbHireConfig.h"
#include "host/pool/TbPoolReset.h"

#include "runtime/policy/hire/HireDeletionPolicy.h"
#include "runtime/policy/hire/HireDeparturePolicy.h"
#include "runtime/policy/hire/HirePvpPolicy.h"

#include "TbBotMgr.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbSqlNames.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace
{
// A dismissed companion's session is expected to close on the next ticks. A session that refuses to is abandoned
// instead of polled forever: the character stays, its ledger row stays 'dismissed', and the next start recovers it.
constexpr time_t kDeletionAbandonAfterSec = 120;
// The core commits a new character asynchronously. A hire cancelled before that commit lands has a ledger row and no
// characters row yet: the row is not cleared until the creation has had time to appear, so no orphan is left behind.
constexpr time_t kCreationSettleSec = 60;
// A claimed hire that never appeared in the host's bot table (a creation or login that failed) is not a live hire;
// the provisioner normally cleans it up at once, this only bounds a leak when it does not.
constexpr time_t kNeverLiveAfterSec = 600;

char const* const kHireTable = "tb_hire";
char const* const kCreateTable =
    "CREATE TABLE IF NOT EXISTS `tb_hire` ("
    "`character_guid` bigint unsigned NOT NULL,"
    "`master_guid` bigint unsigned NOT NULL,"
    "`owner_account_id` int unsigned NOT NULL,"
    "`character_account_id` int unsigned NOT NULL,"
    "`gear_tier` tinyint unsigned NOT NULL DEFAULT '0',"
    "`state` varchar(16) NOT NULL DEFAULT 'active',"
    "`hired_at` bigint NOT NULL DEFAULT '0',"
    "`dismissed_at` bigint DEFAULT NULL,"
    "PRIMARY KEY (`character_guid`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

std::set<std::string> const& HireColumns()
{
    static std::set<std::string> const columns = {
        "character_guid", "master_guid", "owner_account_id", "character_account_id",
        "gear_tier", "state", "hired_at", "dismissed_at",
    };
    return columns;
}

// The table is shipped as a migration; the module also creates it when the characters-database user may, so an enabled
// server works without it (the same rule as the commander binding store).
bool LedgerReady()
{
    static bool createTried = false;
    if (!createTried)
    {
        createTried = true;
        if (!CharacterSchemaHasCompat(kHireTable, HireColumns()))
            CharacterExecuteGuardedCompat(kCreateTable);
    }
    return CharacterSchemaHasCompat(kHireTable, HireColumns());
}

QueryResult HireQuery(std::string const& sql)
{
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(sql, table, columns) || table != kHireTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: hire query refused (not a single-table SELECT of tb_hire): {}", sql);
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
        return QueryResult();
    return CharacterQueryGuardedCompat(sql);
}

bool HireWrite(std::string const& sql)
{
    char const* const writes[] = { "INSERT INTO `tb_hire`", "UPDATE `tb_hire`", "DELETE FROM `tb_hire`" };
    bool known = false;
    for (char const* prefix : writes)
        if (sql.compare(0, std::char_traits<char>::length(prefix), prefix) == 0)
            known = true;
    if (!known)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: hire statement refused (not a write to tb_hire): {}", sql);
        return false;
    }
    if (!LedgerReady())
        return false;
    CharacterExecuteGuardedCompat(sql);
    return true;
}

// A battleground raid temporarily replaces GetGroup(); hire ownership stays with the ordinary party.
Group* HireParty(Player* player)
{
    if (!player)
        return nullptr;
    Group* group = player->GetGroup();
    return group && group->isBGGroup() ? player->GetOriginalGroup() : group;
}

// In-memory facts about one claimed hire that the shared record has no field for.
struct Tracked
{
    time_t claimedAt = 0;
    bool live = false;
};
std::unordered_map<uint32_t, Tracked> g_tracked;

time_t GraceSeconds()
{
    return static_cast<time_t>(hire::DisconnectGraceSeconds());
}

} // namespace

HireLifecycle& HireLifecycle::Instance()
{
    static HireLifecycle instance;
    return instance;
}

bool HireLifecycle::Claim(ObjectGuid botGuid, ObjectGuid masterGuid, uint32_t ownerAccountId,
    uint32_t characterAccountId)
{
    if (botGuid.IsEmpty() || masterGuid.IsEmpty() || !ownerAccountId || !characterAccountId)
        return false;
    uint32_t const botGuidLow = static_cast<uint32_t>(botGuid.GetCounter());
    if (m_hired.find(botGuidLow) != m_hired.end())
        return false;
    if (!LedgerReady())
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the hire ledger tb_hire is missing; the hire is refused (apply forever/sql/characters/2026_11_25_01_tb_hire.sql)");
        return false;
    }

    // A character on anything but a proven hire account is never recorded (and so never deleted by this module).
    if (!hire::IsHireAccount(characterAccountId))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to record character {} on account {}: it is not a hire account",
            botGuidLow, characterAccountId);
        return false;
    }

    std::string const insert =
        "INSERT INTO `tb_hire` (`character_guid`, `master_guid`, `owner_account_id`, `character_account_id`, "
        "`gear_tier`, `state`, `hired_at`, `dismissed_at`) VALUES (" +
        std::to_string(botGuidLow) + ", " + std::to_string(masterGuid.GetCounter()) + ", " +
        std::to_string(ownerAccountId) + ", " + std::to_string(characterAccountId) + ", 0, 'active', " +
        std::to_string(static_cast<long long>(time(nullptr))) + ", NULL)";
    if (!HireWrite(insert))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: hire ledger write failed for character {} on account {}",
            botGuidLow, characterAccountId);
        return false;
    }

    QueryResult const row = HireQuery("SELECT `master_guid` FROM `tb_hire` WHERE `character_guid` = " +
        std::to_string(botGuidLow));
    if (!row || row->Fetch()[0].GetUInt64() != masterGuid.GetCounter())
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: hire ledger row for character {} could not be read back", botGuidLow);
        return false;
    }

    HiredRecord record;
    record.botGuid = botGuid;
    record.masterGuid = masterGuid;
    record.ownerAccountId = ownerAccountId;
    record.hireOrder = ++m_nextHireOrder;
    m_hired[botGuidLow] = record;
    g_tracked[botGuidLow] = Tracked{ time(nullptr), false };
    return true;
}

void HireLifecycle::Forget(ObjectGuid botGuid)
{
    uint32_t const low = static_cast<uint32_t>(botGuid.GetCounter());
    m_hired.erase(low);
    g_tracked.erase(low);
}

void HireLifecycle::DismissNow(ObjectGuid botGuid, char const* reason)
{
    auto it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    if (it == m_hired.end())
        return;
    HiredRecord const record = it->second;
    Dismiss(record, reason ? reason : "released", false);
}

bool HireLifecycle::DismissForRefund(ObjectGuid botGuid, char const* reason)
{
    auto it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    if (it == m_hired.end())
        return false;
    HiredRecord const record = it->second;
    return Dismiss(record, reason ? reason : "released", false);
}

bool HireLifecycle::EnsureDeletionIntent(ObjectGuid botGuid, char const* reason)
{
    uint32_t const low = static_cast<uint32_t>(botGuid.GetCounter());
    if (m_hired.find(low) != m_hired.end())
        return false;
    uint32_t characterAccountId = 0;
    if (LedgerRow(botGuid, characterAccountId))
        return QueueDeletion(low, characterAccountId, reason ? reason : "released");
    // No ledger row: the deletion already finished, or the row is gone. Only a character that is provably absent
    // counts; COUNT(*) answers one row, so a null answer is a read failure, not an absence.
    QueryResult const count = CharacterQueryGuardedCompat(
        "SELECT COUNT(*) FROM `characters` WHERE `guid` = " + std::to_string(low));
    return count && count->Fetch()[0].GetUInt64() == 0;
}

bool HireLifecycle::IsHired(ObjectGuid botGuid) const
{
    return m_hired.find(static_cast<uint32_t>(botGuid.GetCounter())) != m_hired.end();
}

ObjectGuid HireLifecycle::GetMaster(ObjectGuid botGuid) const
{
    auto const it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    return it == m_hired.end() ? ObjectGuid::Empty : it->second.masterGuid;
}

bool HireLifecycle::IsBoundByHirer(ObjectGuid botGuid) const
{
    auto const it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    return it != m_hired.end() && it->second.commandState == HireCommandState::Bound;
}

void HireLifecycle::MarkGrouped(ObjectGuid botGuid)
{
    auto it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    if (it != m_hired.end())
        it->second.everGrouped = true;
}

void HireLifecycle::SetGearTier(ObjectGuid botGuid, uint8_t tier)
{
    uint32_t const low = static_cast<uint32_t>(botGuid.GetCounter());
    auto it = m_hired.find(low);
    if (it == m_hired.end())
        return;
    it->second.gearTier = tier;
    HireWrite("UPDATE `tb_hire` SET `gear_tier` = " + std::to_string(uint32_t(tier)) +
        " WHERE `character_guid` = " + std::to_string(low));
}

uint8_t HireLifecycle::GearTier(ObjectGuid botGuid) const
{
    auto const it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    return it == m_hired.end() ? 0 : it->second.gearTier;
}

bool HireLifecycle::MasterOnline(HiredRecord const& record) const
{
    Player* master = ObjectAccessor::FindPlayer(record.masterGuid);
    return master && master->IsInWorld() && master->GetSession() && !master->GetSession()->IsBot();
}

bool HireLifecycle::MasterLeftGroup(HiredRecord const& record) const
{
    Player* master = ObjectAccessor::FindPlayer(record.masterGuid);
    Player* bot = ObjectAccessor::FindPlayer(record.botGuid);
    bool const masterOnline = MasterOnline(record) && !record.masterOfflineSince;
    // A hire out of world (mid-login, a teleport) counts as grouped: the runtime sweep owns that case, not this rule.
    bool const inSameGroup = !bot || !bot->IsInWorld() || !master ||
        (HireParty(bot) && HireParty(bot) == HireParty(master));
    return ShouldDismissHireOnGroupDeparture(true, masterOnline, record.everGrouped, inSameGroup,
        record.commandState);
}

bool HireLifecycle::BeginCommand(ObjectGuid botGuid, ObjectGuid commanderGuid)
{
    auto it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    if (it == m_hired.end() || commanderGuid.IsEmpty() || it->second.masterGuid != commanderGuid)
        return false;
    it->second.commandState = HireCommandState::Transition;
    return true;
}

void HireLifecycle::ConfirmCommand(ObjectGuid commanderGuid)
{
    for (auto& kv : m_hired)
        if (kv.second.masterGuid == commanderGuid && kv.second.commandState == HireCommandState::Transition &&
            !kv.second.regroupOnReturn)
            kv.second.commandState = HireCommandState::Bound;
}

void HireLifecycle::CancelCommand(ObjectGuid botGuid)
{
    auto it = m_hired.find(static_cast<uint32_t>(botGuid.GetCounter()));
    if (it != m_hired.end() && it->second.commandState == HireCommandState::Transition && !it->second.regroupOnReturn)
        it->second.commandState = HireCommandState::None;
}

uint32_t HireLifecycle::CommandedHireCount(ObjectGuid commanderGuid) const
{
    uint32_t count = 0;
    for (auto const& kv : m_hired)
        if (kv.second.masterGuid == commanderGuid && kv.second.commandState != HireCommandState::None)
            ++count;
    return count;
}

void HireLifecycle::KeepInstanceAfterPartyMove(Player* bot)
{
    // The hire is invited into its hirer's existing party during provisioning, and the commander binding that moves
    // it between parties is not wired on this host: the core's instance rules are left alone.
    (void)bot;
}

void HireLifecycle::DismissSurplusAfterBinding(ObjectGuid commanderGuid)
{
    if (commanderGuid.IsEmpty())
        return;
    std::vector<HiredRecord> owned;
    for (auto const& kv : m_hired)
        if (kv.second.masterGuid == commanderGuid)
            owned.push_back(kv.second);
    uint32_t const cap = hire::MaxCompanions();
    if (owned.size() <= cap)
        return;
    // Newest first, as a binding that ends leaves the most recent hire without a seat.
    std::sort(owned.begin(), owned.end(), [](HiredRecord const& a, HiredRecord const& b)
    {
        return a.hireOrder > b.hireOrder;
    });
    for (std::size_t i = 0; i < owned.size() - cap; ++i)
        DismissNow(owned[i].botGuid, "companion limit after a binding ended");
}

uint32_t HireLifecycle::EndCommand(ObjectGuid commanderGuid)
{
    std::vector<ObjectGuid> hires;
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        if (record.masterGuid != commanderGuid || record.commandState == HireCommandState::None)
            continue;
        record.commandState = HireCommandState::Transition;
        hires.push_back(record.botGuid);
    }
    if (hires.empty())
        return 0;

    DismissSurplusAfterBinding(commanderGuid);

    Player* master = ObjectAccessor::FindPlayer(commanderGuid);
    uint32_t handled = 0;
    for (ObjectGuid const& guid : hires)
    {
        auto it = m_hired.find(static_cast<uint32_t>(guid.GetCounter()));
        if (it == m_hired.end())
            continue;
        HiredRecord& record = it->second;
        Player* bot = ObjectAccessor::FindPlayer(guid);
        bool const live = bot && bot->IsInWorld();
        bool const masterOnline = master && MasterOnline(record) && !record.masterOfflineSince;
        bool keepTransition = false;
        switch (DecideHireBindingEnd(live, masterOnline, live && master && bot->IsInSameRaidWith(master), bot != nullptr))
        {
            case HireBindingEnd::AwaitWorld:
                record.regroupOnReturn = true;
                record.regroupDeadline = time(nullptr) + std::max<time_t>(60, GraceSeconds());
                keepTransition = true;
                ++handled;
                break;
            case HireBindingEnd::RejoinHirer:
                record.regroupOnReturn = true;
                Reunite(record, master);
                if (KeepAwaitingRegroup(bot->IsInSameRaidWith(master), false))
                {
                    record.regroupOnReturn = true;
                    record.regroupDeadline = time(nullptr) + std::max<time_t>(60, GraceSeconds());
                    keepTransition = true;
                }
                ++handled;
                break;
            case HireBindingEnd::ParkForLogin:
                record.regroupOnReturn = true;
                ++handled;
                break;
            case HireBindingEnd::Keep:
                ++handled;
                break;
            case HireBindingEnd::Leave:
                break;
        }
        if (!keepTransition)
            record.commandState = HireCommandState::None;
    }
    return handled;
}

void HireLifecycle::QueueDismissal(HiredRecord const& record, DismissalReason reason)
{
    for (PendingDismissal& pending : m_pendingDismissals)
    {
        if (pending.botGuid == record.botGuid && pending.masterGuid == record.masterGuid)
        {
            if (reason > pending.reason)
                pending.reason = reason;
            return;
        }
    }
    m_pendingDismissals.push_back({ record.botGuid, record.masterGuid, reason });
}

void HireLifecycle::DrainDismissals()
{
    std::vector<PendingDismissal> pending;
    pending.swap(m_pendingDismissals);
    for (PendingDismissal const& entry : pending)
    {
        auto it = m_hired.find(static_cast<uint32_t>(entry.botGuid.GetCounter()));
        if (it == m_hired.end() || it->second.masterGuid != entry.masterGuid)
            continue;
        HiredRecord const record = it->second;
        switch (entry.reason)
        {
            case DismissalReason::Removed:
                if (ShouldDismissHireOnOwnRemoval(record.commandState))
                    Dismiss(record, "removed from group", false);
                break;
            case DismissalReason::Disbanded:
                if (!record.masterOfflineSince && ShouldDismissHireOnDisband(record.commandState))
                    Dismiss(record, "group disbanded", false);
                break;
            case DismissalReason::MasterLeft:
                if (MasterLeftGroup(record))
                    Dismiss(record, "master left the group", false);
                break;
            case DismissalReason::EnteredBattleground:
                Dismiss(record, "entered a battleground", false);
                break;
        }
    }
}

void HireLifecycle::WarnMasterOnQueueEntry(Player* master)
{
    if (!master || !master->IsInWorld() || !master->GetSession())
        return;
    ChatHandler(master->GetSession()).SendSysMessage("Your hired companions are PvE-only and will be dismissed when you enter the battleground.");
}

void HireLifecycle::LeaveBattlegroundQueue(Player* bot)
{
    // The queue's leave action is a client packet on this core, not a module API; the battleground module owns bot
    // queues. A queued hire is dismissed as soon as it enters, which is what the PvE-only rule protects.
    (void)bot;
}

void HireLifecycle::SweepBattlegroundBoundary()
{
    bool const allowPvP = hire::AllowPvP();
    std::unordered_set<uint32_t> notified;
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        Player* master = ObjectAccessor::FindPlayer(record.masterGuid);
        Player* bot = ObjectAccessor::FindPlayer(record.botGuid);
        bool const masterInQueue = master && master->InBattlegroundQueue();
        bool const masterInside = master && master->InBattleground();
        bool const botQueued = bot && bot->InBattlegroundQueue();
        bool const botInside = bot && bot->InBattleground();
        if (ShouldDismissHireOnBattleground(allowPvP, masterInside, botInside))
        {
            QueueDismissal(record, DismissalReason::EnteredBattleground);
            if (master && master->IsInWorld() && master->GetSession() && masterInside &&
                notified.insert(static_cast<uint32_t>(record.masterGuid.GetCounter())).second)
                ChatHandler(master->GetSession()).SendSysMessage("Your hired companions were dismissed: they are PvE-only.");
            continue;
        }
        if (ShouldDequeueHireFromBgQueue(allowPvP, botQueued, botInside, masterInside))
            LeaveBattlegroundQueue(bot);
        if (!master)
            continue;
        if (ShouldWarnMasterOnBgQueue(allowPvP, masterInQueue, masterInside, record.bgQueueWarned))
        {
            record.bgQueueWarned = true;
            if (notified.insert(static_cast<uint32_t>(record.masterGuid.GetCounter())).second)
                WarnMasterOnQueueEntry(master);
            continue;
        }
        if (!masterInQueue)
            record.bgQueueWarned = false;
    }
}

bool HireLifecycle::Dismiss(HiredRecord const& record, char const* reason, bool removeFromGroup)
{
    uint32_t const botGuidLow = static_cast<uint32_t>(record.botGuid.GetCounter());
    if (!m_dismissing.insert(botGuidLow).second)
        return false;

    // Erased first: the group removal below raises the group hooks, and this bot is no longer an active hire.
    m_hired.erase(botGuidLow);
    g_tracked.erase(botGuidLow);

    if (Player* bot = ObjectAccessor::FindPlayer(record.botGuid))
        if (removeFromGroup && bot->GetGroup())
            bot->GetGroup()->RemoveMember(record.botGuid);

    if (BotMgr::Instance().IsBot(record.botGuid))
    {
        BotMgr::Instance().SetMaster(record.botGuid, ObjectGuid::Empty);
        BotMgr::Instance().Logout(record.botGuid, "hired companion dismissed");
    }

    uint32_t characterAccountId = 0;
    bool deletionDurable = false;
    if (LedgerRow(record.botGuid, characterAccountId))
        deletionDurable = QueueDeletion(botGuidLow, characterAccountId, reason ? reason : "released");
    else
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: hired bot {} has no hire ledger row; its character is left in place", botGuidLow);

    TC_LOG_INFO("tortoisebots", "TortoiseBots: hired bot {} dismissed ({}); logging off for deletion",
        botGuidLow, reason ? reason : "released");
    m_dismissing.erase(botGuidLow);
    return deletionDurable;
}

void HireLifecycle::OnGroupMemberRemoved(Group* group, ObjectGuid guid)
{
    if (!group || group->isBGGroup() || guid.IsEmpty())
        return;

    // The removed member is a hire: the hire ends (a bind or unbind step is the one exception).
    if (auto it = m_hired.find(static_cast<uint32_t>(guid.GetCounter())); it != m_hired.end())
    {
        HiredRecord const record = it->second;
        if (ShouldDismissHireOnOwnRemoval(record.commandState))
            QueueDismissal(record, DismissalReason::Removed);
        return;
    }

    // The removed member is the hirer: in a group that survives the departure nothing else would end the hire, so
    // the hires still in this group are queued. Only the hire registry is read; a pool bot or an alt is never in it.
    std::vector<HiredRecord> departing;
    for (auto const& kv : m_hired)
    {
        HiredRecord const& record = kv.second;
        if (record.masterGuid == guid && group->IsMember(record.botGuid) && MasterLeftGroup(record))
            departing.push_back(record);
    }
    for (HiredRecord const& record : departing)
        QueueDismissal(record, DismissalReason::MasterLeft);
}

void HireLifecycle::OnGroupDisband(Group* group)
{
    if (!group || group->isBGGroup())
        return;
    std::vector<HiredRecord> departing;
    for (auto const& kv : m_hired)
        if (group->IsMember(kv.second.botGuid))
            departing.push_back(kv.second);
    for (HiredRecord const& record : departing)
    {
        // A two-member party disbands when its master logs out; the disconnect grace owns that hire.
        if (record.masterOfflineSince)
            continue;
        if (!ShouldDismissHireOnDisband(record.commandState))
            continue;
        QueueDismissal(record, DismissalReason::Disbanded);
    }
}

void HireLifecycle::OnMasterLogin(Player* master)
{
    if (!master || !master->GetSession() || master->GetSession()->IsBot())
        return;
    ObjectGuid const masterGuid = master->GetGUID();
    std::vector<ObjectGuid> mine;
    std::vector<ObjectGuid> parked;
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        if (record.masterGuid != masterGuid)
            continue;
        record.masterOfflineSince = 0;
        record.bgQueueWarned = false;
        mine.push_back(record.botGuid);
        if (record.regroupOnReturn)
        {
            record.commandState = HireCommandState::Transition;
            parked.push_back(record.botGuid);
        }
    }
    for (ObjectGuid const& guid : mine)
    {
        auto it = m_hired.find(static_cast<uint32_t>(guid.GetCounter()));
        if (it != m_hired.end())
            Reunite(it->second, master);
    }
    for (ObjectGuid const& guid : parked)
    {
        auto it = m_hired.find(static_cast<uint32_t>(guid.GetCounter()));
        if (it != m_hired.end())
            it->second.commandState = HireCommandState::None;
    }
}

void HireLifecycle::OnMasterLogout(Player* master)
{
    if (!master || !master->GetSession() || master->GetSession()->IsBot())
        return;
    ObjectGuid const masterGuid = master->GetGUID();
    time_t const now = time(nullptr);
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        if (record.masterGuid != masterGuid)
            continue;
        if (!record.masterOfflineSince)
            record.masterOfflineSince = now;
        TC_LOG_INFO("tortoisebots", "TortoiseBots: master {} offline; hired bot {} guards for {}s",
            master->GetName(), static_cast<uint32_t>(record.botGuid.GetCounter()), GraceSeconds());
    }
}

void HireLifecycle::Reunite(HiredRecord& record, Player* master)
{
    if (!master || !master->IsInWorld())
        return;
    Player* bot = ObjectAccessor::FindPlayer(record.botGuid);
    if (!bot || !bot->IsInWorld() || !BotMgr::Instance().IsBot(record.botGuid))
        return;

    if (record.regroupOnReturn)
    {
        // A binding ended while the hire sat in another party: leave it so the hirer's invitation can be accepted.
        record.regroupOnReturn = false;
        if (bot->GetGroup() && !bot->IsInSameRaidWith(master))
            bot->GetGroup()->RemoveMember(record.botGuid);
    }

    if (!bot->IsInSameRaidWith(master))
    {
        Group* botGroup = bot->GetGroup();
        if (!botGroup)
        {
            BotMgr::Instance().GroupWith(record.botGuid, record.masterGuid);
        }
        else if (!botGroup->isBGGroup() && !botGroup->IsFull() && !master->GetGroup() &&
            botGroup->AddMember(master))
        {
            botGroup->ChangeLeader(master->GetGUID());
        }
    }

    if (bot->IsInSameRaidWith(master))
        record.everGrouped = true;
}

void HireLifecycle::SweepGracePeriod(time_t now)
{
    time_t const grace = GraceSeconds();
    std::vector<std::pair<HiredRecord, char const*>> ending;
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        uint32_t const low = static_cast<uint32_t>(record.botGuid.GetCounter());
        Tracked& tracked = g_tracked[low];

        if (BotMgr::Instance().IsBot(record.botGuid))
            tracked.live = true;
        else if (tracked.live)
        {
            ending.emplace_back(record, "runtime record gone");
            continue;
        }
        else if (now - tracked.claimedAt >= kNeverLiveAfterSec)
        {
            ending.emplace_back(record, "never came up");
            continue;
        }

        // Backstop for a master departure no group hook reported.
        if (MasterLeftGroup(record))
        {
            ending.emplace_back(record, "master left the group");
            continue;
        }
        if (!record.masterOfflineSince)
        {
            // Passive detection: the master object vanished without a logout hook (a crash, a killed session).
            if (!ObjectAccessor::FindPlayer(record.masterGuid))
                record.masterOfflineSince = now;
            continue;
        }
        if (MasterOnline(record))
        {
            record.masterOfflineSince = 0;
            continue;
        }
        if (now - record.masterOfflineSince < grace)
            continue;
        ending.emplace_back(record, "master grace expired");
    }
    for (auto const& entry : ending)
        if (IsHired(entry.first.botGuid))
            Dismiss(entry.first, entry.second);
}

bool HireLifecycle::LedgerRow(ObjectGuid botGuid, uint32_t& characterAccountId) const
{
    uint32_t const low = static_cast<uint32_t>(botGuid.GetCounter());
    QueryResult const row = HireQuery("SELECT `character_account_id` FROM `tb_hire` WHERE `character_guid` = " +
        std::to_string(low));
    if (!row)
        return false;
    characterAccountId = row->Fetch()[0].GetUInt32();
    return characterAccountId != 0;
}

bool HireLifecycle::QueueDeletion(uint32_t guidLow, uint32_t ledgerAccountId, char const* reason)
{
    // The ledger row is the only proof that this character was created by a hire. Without it nothing is deleted.
    if (!ledgerAccountId)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to delete character {} after '{}': no hire ledger row",
            guidLow, reason ? reason : "released");
        return false;
    }

    bool const queued = std::any_of(m_pendingDeletions.begin(), m_pendingDeletions.end(),
        [guidLow](PendingDeletion const& entry) { return entry.guidLow == guidLow; });
    if (!queued)
        m_pendingDeletions.push_back(PendingDeletion{ guidLow, reason ? reason : "released", time(nullptr), false });

    // The durable intent first: a crash between here and the deletion is resumed from this row at the next start.
    std::string const where = " WHERE `character_guid` = " + std::to_string(guidLow);
    bool const written = HireWrite("UPDATE `tb_hire` SET `state` = 'dismissed', `dismissed_at` = " +
        std::to_string(static_cast<long long>(time(nullptr))) + where);
    QueryResult const row = HireQuery("SELECT `state` FROM `tb_hire`" + where);
    bool const durable = written && row && row->Fetch()[0].GetString() == "dismissed";
    if (!durable)
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the 'dismissed' mark for character {} was not written", guidLow);
    else
        TC_LOG_INFO("tortoisebots", "TortoiseBots: hired character {} scheduled for deletion ({})", guidLow,
            reason ? reason : "released");
    return durable;
}

bool HireLifecycle::RecoverStaleHires()
{
    if (!LedgerReady())
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the hire ledger tb_hire is missing; stale hires cannot be recovered (apply forever/sql/characters/2026_11_25_01_tb_hire.sql)");
        return false;
    }
    QueryResult rows = HireQuery("SELECT `character_guid`, `character_account_id` FROM `tb_hire`");
    if (!rows)
    {
        // A null result is either "the ledger is empty" or a failed read; COUNT(*) tells them apart.
        QueryResult const count = CharacterQueryGuardedCompat("SELECT COUNT(*) FROM `tb_hire`");
        if (!count)
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: the hire ledger could not be read; stale hires are not recovered yet");
            return false;
        }
        return true;
    }

    uint32_t recovered = 0;
    do
    {
        Field* fields = rows->Fetch();
        uint32_t const guidLow = fields[0].GetUInt32();
        uint32_t const accountId = fields[1].GetUInt32();
        if (m_hired.find(guidLow) != m_hired.end())
            continue;
        QueueDeletion(guidLow, accountId, "server restart");
        ++recovered;
    } while (rows->NextRow());

    if (recovered)
        TC_LOG_INFO("tortoisebots", "TortoiseBots: {} stale hired companion(s) recovered for deletion after a restart", recovered);
    return true;
}

bool HireLifecycle::ProcessDeletion(PendingDeletion& entry, time_t now)
{
    // Snapshot: deleting a character removes it from its party, which re-enters the group hooks and can append while
    // this call is on the stack.
    uint32_t const guidLow = entry.guidLow;
    std::string const reason = entry.reason;
    time_t const queuedAt = entry.queuedAt;

    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (player && player->GetSession() && !player->GetSession()->IsBot())
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to delete dismissed hire {}: a network session owns it", guidLow);
        return true;
    }

    // A live character cannot be deleted. Ask for the logoff once per pass and wait; a session that never closes is
    // abandoned, and its ledger row makes the next start recover it.
    if (BotMgr::Instance().IsBot(guid) || player)
    {
        if (BotMgr::Instance().IsBot(guid))
            BotMgr::Instance().Logout(guid, "hired companion dismissed");
        if (now - queuedAt >= kDeletionAbandonAfterSec)
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: dismissed hire {} never went offline; leaving the character in place (it is deleted on the next server start)", guidLow);
            return true;
        }
        return false;
    }

    // Offline: revalidate every durable fact before touching anything.
    uint32_t ledgerAccountId = 0;
    int64_t hiredAt = 0;
    QueryResult const ledger = HireQuery("SELECT `character_account_id`, `hired_at` FROM `tb_hire` WHERE `character_guid` = " +
        std::to_string(guidLow));
    bool const hasLedger = ledger != nullptr;
    if (hasLedger)
    {
        Field* fields = ledger->Fetch();
        ledgerAccountId = fields[0].GetUInt32();
        hiredAt = fields[1].GetInt64();
    }

    QueryResult const character = CharacterQueryGuardedCompat(
        "SELECT `account` FROM `characters` WHERE `guid` = " + std::to_string(guidLow));
    bool const characterExists = character != nullptr;
    uint32_t const accountId = characterExists ? character->Fetch()[0].GetUInt32() : 0;

    if (!characterExists)
    {
        if (hasLedger && now - static_cast<time_t>(hiredAt) < kCreationSettleSec)
            return false; // the creation's commit may still land
        HireWrite("DELETE FROM `tb_hire` WHERE `character_guid` = " + std::to_string(guidLow));
        return true;
    }

    // A hire never auctions; a listing would survive the core's character deletion, so its presence refuses the delete.
    QueryResult const auctions = CharacterQueryGuardedCompat(
        "SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` = " + std::to_string(guidLow));
    if (!auctions)
    {
        if (now - queuedAt >= kDeletionAbandonAfterSec)
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: the auctions of dismissed hire {} could not be read; leaving the character in place", guidLow);
            return true;
        }
        return false;
    }
    if (auctions->Fetch()[0].GetUInt64() != 0)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to delete dismissed hire {}: it owns auction listings", guidLow);
        return true;
    }

    // Same rule for a guild it leads: the core's deletion removes the member row but leaves the guild without a leader.
    QueryResult const guilds = CharacterQueryGuardedCompat(
        "SELECT COUNT(*) FROM `guild` WHERE `leaderguid` = " + std::to_string(guidLow));
    if (!guilds || guilds->Fetch()[0].GetUInt64() != 0)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to delete dismissed hire {}: it leads a guild", guidLow);
        return true;
    }

    HireDeletionDecision const decision = DecideHireDeletion(hasLedger, hire::IsHireAccount(ledgerAccountId),
        accountId == ledgerAccountId);
    if (decision != HireDeletionDecision::Allowed)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: refusing to delete dismissed hire {}: {}", guidLow,
            HireDeletionDecisionName(decision));
        return true;
    }

    Player::DeleteFromDB(guid, accountId, true, true);
    HireWrite("DELETE FROM `tb_hire` WHERE `character_guid` = " + std::to_string(guidLow));
    TC_LOG_INFO("tortoisebots", "TortoiseBots: hired character {} deleted ({})", guidLow, reason);
    return true;
}

void HireLifecycle::SweepDeletions(time_t now)
{
    if (m_pendingDeletions.empty())
        return;
    // The pool reset deletes pool characters only, but it rebuilds whole accounts and settles auctions: it runs alone.
    if (pool::Reset::Active())
        return;
    for (std::size_t i = 0; i < m_pendingDeletions.size(); ++i)
    {
        if (!ProcessDeletion(m_pendingDeletions[i], now))
            continue;
        m_pendingDeletions.erase(m_pendingDeletions.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void HireLifecycle::Update(uint32_t diff)
{
    time_t const now = time(nullptr);
    DrainDismissals();
    SweepDeletions(now);

    m_updateElapsedMs += diff;
    m_bgElapsedMs += diff;
    if (m_bgElapsedMs >= 1000)
    {
        m_bgElapsedMs = 0;
        SweepBattlegroundBoundary();
    }
    if (m_updateElapsedMs < 5000)
        return;
    m_updateElapsedMs = 0;

    // No hire survives a restart: every ledger row a fresh process finds is stale and is deleted.
    if (!m_ledgerRecovered && (m_recoveryRetryAt == 0 || now >= m_recoveryRetryAt))
    {
        if (RecoverStaleHires())
            m_ledgerRecovered = true;
        else
            m_recoveryRetryAt = now + 60;
    }
    if (m_hired.empty())
        return;
    SweepGracePeriod(now);
}

} // namespace TortoiseBots
