// The resistance-gear swap around the raid pull (issue #20 round 1): see host/TbResistGear.h.
//
// Nothing here creates, sells or destroys an item: `Apply` and `Restore` both move pieces with the core's
// Player::SwapItem (in place: the piece leaving the slot lands in the bag slot the incoming one came from), so the
// bags' free space does not matter and the player's kit survives a hold untouched.
//
// Compiled with and without TORTOISEBOTS_SLICE1: the config, the entry index, the holds and `Holding` answer in both
// builds (the auto-equip gate asks `Holding`), while the tank/pin questions need the engine's role rules and are
// asked only in the engine build.
#include "host/TbResistGear.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"   // the core headers, then the shim: every core include after this one is a no-op
#endif

#include "Bag.h"
#include "Config.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSpellEntry.h"
#include "host/data/TbResistData.gen.h"
#include "TbBotMgr.h"
#include "ai/playerbot/policy/raid/ResistGearPolicy.h"
#include "ai/playerbot/policy/raid/ResistHoldPolicy.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "PlayerbotAIStorage.h"
#endif

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace resistgear
{
namespace
{
// Logging goes straight to the core's logger: in the engine build the compat shim has already replaced the legacy `sLog`
// with its printf proxy, so the TC_LOG_ macros do not compile there (host/TbEngineBrain.cpp, same reason), and this file
// is compiled with and without the engine.
template <typename... Args>
void LogAt(char const* logger, LogLevel level, fmt::format_string<Args...> message, Args&&... args)
{
    Log::instance()->OutMessage(logger, level, message, std::forward<Args>(args)...);
}

struct Config
{
    bool enabled = false;
    uint32 range = 60;
    uint32 releaseSeconds = 15;
};
Config config;

// One piece this module put on, and the bot's own piece it replaced (an empty guid for a slot that was empty).
struct HeldPiece
{
    uint8 slot;
    ObjectGuid own;
    ObjectGuid resist;
};

// What a bot is holding: the school and the pieces still to put back.
struct Hold
{
    int school = 0;
    std::vector<HeldPiece> pieces;
    uint32 attempts = 0;      // restore passes that left pieces behind (the first are fast, then one a minute)
    uint64 nextRetryMs = 0;   // the world clock when the tick may try again
    bool backoffLogged = false;
    ai::HoldState state = ai::HoldState::Held;   // HELD: the set is on; RELEASING: a restore was asked for
};

std::map<ObjectGuid, Hold> holds;
uint64 clockMs = 0;            // the world tick's accumulated milliseconds
uint64 lastSweepMs = 0;
uint64 lastStoredScanMs = 0;

// The characters store (forever/sql/characters/2026_11_24_11_tb_resist_hold.sql): one row per held piece, so a crash
// or a logout that could not put everything back is restored on the next login. Read and written through the schema
// guard (compat/TbCompat_CharStore.h): a missing table degrades to a memory-only hold with one warning.
char const* const kHoldTable = "tortoise_bots_resist_hold";
char const* const kHoldCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_resist_hold` ("
    "`guid` bigint unsigned NOT NULL,"
    "`slot` tinyint unsigned NOT NULL,"
    "`school` tinyint unsigned NOT NULL,"
    "`own_guid` bigint unsigned NOT NULL DEFAULT '0',"
    "`resist_guid` bigint unsigned NOT NULL,"
    "`releasing` tinyint unsigned NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`guid`, `slot`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
bool storeProbed = false;    // the existence/column question was asked (once, with the feature's state at that moment)
bool storeCreated = false;   // this process ran the CREATE (only with the feature on)
bool storeAvailable = false;
bool storeWarned = false;

// The rows read at startup whose bots have not logged in yet. A login takes its hold from here and never asks the
// database, and the scan that hands one to a bot already in the world reads only this map.
std::map<uint64, Hold> storedHolds;
// Holds a logout had to leave in the store (written after startup, so not in the startup read): kept for the next login
// of the same character, which is the only reader (a dying Player is never handed its own hold back).
std::map<uint64, Hold> leftHolds;

// The boss entry -> school index (BuildIndex) and whether it was built.
std::unordered_map<uint32, int> schoolByEntry;
bool indexBuilt = false;

int32 SchoolResistance(Item* item, int school)
{
    if (!item)
        return 0;
    ItemPrototype const* proto = tbcompat::ItemProto(item->GetEntry());
    if (!proto)
        return 0;
    switch (school)
    {
        case 1: return proto->HolyRes;
        case 2: return proto->FireRes;
        case 3: return proto->NatureRes;
        case 4: return proto->FrostRes;
        case 5: return proto->ShadowRes;
        case 6: return proto->ArcaneRes;
        default: return 0;
    }
}

// The equipment position of a slot (bag 0 = the equipment the player wears).
uint16 EquipPos(uint8 slot)
{
    return uint16(INVENTORY_SLOT_BAG_0) << 8 | uint16(slot);
}

// The first empty bag position (a backpack row, then the four worn bags): where a resist piece goes back when the slot it
// filled was empty before the swap, so nothing has to be recorded beyond the held tuple the spec names.
bool FindFreeBagPos(Player* bot, uint16& out)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            out = uint16(INVENTORY_SLOT_BAG_0) << 8 | uint16(slot);
            return true;
        }
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
            if (!bag->GetItemByPos(uint8(slot)))
            {
                out = uint16(bagSlot) << 8 | uint16(slot);
                return true;
            }
    }
    return false;
}

// Every item in the bot's bags: the backpack rows, then the four worn bags. The bank, the keyring, a trade and the
// child-equipment slots are not "in the bags" for a swap (the same set `.tb removeitem` walks).
template<typename Fn>
void ForEachBagItem(Player* bot, Fn&& fn)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        fn(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
            fn(bag->GetItemByPos(slot));
    }
}

// The same walk as a lookup: the piece only if it is in the bags (the bank is not a swap source).
Item* FindBagItem(Player* bot, ObjectGuid const& guid)
{
    Item* found = nullptr;
    ForEachBagItem(bot,
        [&](Item* item)
        {
            if (!found && item && item->GetGUID() == guid)
                found = item;
        });
    return found;
}

// The bot is in a group that holds a REAL player (a bot-only raid is not the case this feature is for).
bool GroupHasRealPlayer(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    // A member that is not a host bot is a real player (it stays one while offline: the slot's guid is enough).
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.guid != bot->GetGUID() && !BotMgr::Instance().IsBot(slot.guid))
            return true;
    return false;
}

// A worn piece the bot's keep/equip order pins stays: the ForceItemUsage value is the same one ItemUsageValue reads
// for an item (FORCE_USAGE_KEEP = "keep", FORCE_USAGE_EQUIP = "equip"). Without an engine there are no orders.
bool IsPinned(Player* bot, uint32 itemId)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context || !context->HasValue("force item usage", int32(itemId)))
        return false;
    ai::ForceItemUsage const usage = context->GetValue<ai::ForceItemUsage>("force item usage", int32(itemId))->Get();
    return usage == ai::ForceItemUsage::FORCE_USAGE_KEEP || usage == ai::ForceItemUsage::FORCE_USAGE_EQUIP;
#else
    (void)bot;
    (void)itemId;
    return false;
#endif
}

// The tank question is the engine's (PlayerbotAI::IsTank: the role rules of the role system). Without the engine
// nobody can tell, and the armour slots are the ones with something to lose: answer "tank" so nothing is taken off.
bool IsTank(Player* bot)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    return PlayerbotAI::IsTank(bot);
#else
    (void)bot;
    return true;
#endif
}

// ---- the characters store and the recovery path (issue #20 round 1, review item 2) ----

ObjectGuid PlayerGuid(uint64 key)
{
    return ObjectGuid::Create<HighGuid::Player>(key);
}

ObjectGuid ItemGuid(uint64 key)
{
    return ObjectGuid::Create<HighGuid::Item>(key);
}

Player* ConnectedPlayer(ObjectGuid const& guid)
{
    return ObjectAccessor::FindConnectedPlayer(guid);
}

// ForeverCore aborts the world on a statement naming a missing table or column (MySQLConnection.cpp), so the hold
// table is probed before any statement names it. Only a caller that may write (the feature is on) runs the CREATE, and
// only once; with the feature off nothing but the existence question is asked, so a server that never used the feature
// pays one probe at startup and nothing at any login. A missing store keeps the hold in memory (one warning).
bool EnsureStore(bool mayCreate)
{
    if (!storeProbed || (mayCreate && !storeCreated))
    {
        if (mayCreate)
        {
            CharacterExecuteGuardedCompat(kHoldCreate);
            storeCreated = true;
        }
        static std::set<std::string> const columns = { "guid", "slot", "school", "own_guid", "resist_guid", "releasing" };
        storeAvailable = CharacterSchemaHasCompat(kHoldTable, columns);
        storeProbed = true;
        if (!storeAvailable && mayCreate && !storeWarned)
        {
            storeWarned = true;
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots: `{}` is missing or lacks columns; a resist-gear hold is kept in memory only. Apply forever/sql/characters/2026_11_24_11_tb_resist_hold.sql and forever/sql/characters/2026_11_25_00_tb_resist_hold_state.sql.", kHoldTable);
        }
    }
    return storeAvailable;
}

// The stored pieces of one character (empty when the store has none or cannot answer).
Hold ReadStoredHold(uint64 playerKey)
{
    Hold hold;
    if (!playerKey || !EnsureStore(config.enabled))
        return hold;
    QueryResult result = CharacterQueryGuardedCompat("SELECT `slot`, `school`, `own_guid`, `resist_guid`, `releasing` FROM `"
        + std::string(kHoldTable) + "` WHERE `guid` = " + std::to_string(playerKey) + " ORDER BY `slot`");
    if (!result)
        return hold;
    do
    {
        Field* fields = result->Fetch();
        uint8 const slot = fields[0].GetUInt8();
        hold.school = fields[1].GetUInt8();
        uint64 const own = fields[2].GetUInt64();
        uint64 const resist = fields[3].GetUInt64();
        if (!resist)
            continue;
        hold.state = fields[4].GetUInt8() ? ai::HoldState::Releasing : ai::HoldState::Held;
        hold.pieces.push_back(HeldPiece{ slot, own ? ItemGuid(own) : ObjectGuid(), ItemGuid(resist) });
    } while (result->NextRow());
    return hold;
}

// One row per held piece: what `Apply` put on, so a crash or a restart can put the same pieces back. The hold's state
// is part of the row, so a restart knows whether the set is still meant to be on or a release was in flight. False when
// the rows are not in the database afterwards (no store, or a write that did not land): the caller must not swap then,
// because a resist piece on a bot with no record of it is a piece nothing puts back.
bool PersistHold(uint64 playerKey, Hold const& hold)
{
    if (hold.pieces.empty() || !EnsureStore(true))
        return false;
    int const releasing = hold.state == ai::HoldState::Releasing ? 1 : 0;
    std::vector<std::string> statements;
    statements.push_back("DELETE FROM `" + std::string(kHoldTable) + "` WHERE `guid` = " + std::to_string(playerKey));
    std::string insert = "INSERT INTO `" + std::string(kHoldTable) + "` (`guid`, `slot`, `school`, `own_guid`, `resist_guid`, `releasing`) VALUES ";
    bool first = true;
    for (HeldPiece const& piece : hold.pieces)
    {
        insert += first ? "(" : ",(";
        insert += std::to_string(playerKey) + "," + std::to_string(piece.slot) + "," + std::to_string(hold.school) + ","
            + std::to_string(piece.own.IsEmpty() ? 0 : piece.own.GetCounter()) + "," + std::to_string(piece.resist.GetCounter())
            + "," + std::to_string(releasing) + ")";
        first = false;
    }
    statements.push_back(insert);
    CharacterTransactGuardedCompat(statements);

    // A one-way transaction cannot answer, so the rows are read back (the commander binding store does the same): the
    // swap happens only after the database really holds every piece it is about to put on.
    Hold const stored = ReadStoredHold(playerKey);
    if (stored.pieces.size() != hold.pieces.size())
        return false;
    for (HeldPiece const& piece : hold.pieces)
        if (std::none_of(stored.pieces.begin(), stored.pieces.end(), [&](HeldPiece const& row)
            {
                return row.slot == piece.slot && row.resist == piece.resist;
            }))
            return false;
    return true;
}

// The rows now say a restore was asked for; the next world start finishes it instead of treating the set as held.
void PersistReleasing(uint64 playerKey)
{
    if (!playerKey || !EnsureStore(config.enabled))
        return;
    CharacterExecuteGuardedCompat("UPDATE `" + std::string(kHoldTable) + "` SET `releasing` = 1 WHERE `guid` = " + std::to_string(playerKey));
}

// A restore was asked for this hold: that is the one state the world tick retries, and the durable row keeps it.
void MarkReleasing(uint64 playerKey, Hold& hold)
{
    if (hold.state == ai::HoldState::Releasing)
        return;
    hold.state = ai::HoldState::Releasing;
    PersistReleasing(playerKey);
}

// Forgets the given slots (the pieces that went back or that the slot no longer holds); no slots means the whole hold.
void ForgetPieces(uint64 playerKey, std::vector<uint8> const& slots)
{
    if (!playerKey || !EnsureStore(config.enabled))
        return;
    std::string sql = "DELETE FROM `" + std::string(kHoldTable) + "` WHERE `guid` = " + std::to_string(playerKey);
    if (!slots.empty())
    {
        sql += " AND `slot` IN (";
        for (size_t i = 0; i < slots.size(); ++i)
            sql += (i ? "," : "") + std::to_string(slots[i]);
        sql += ")";
    }
    CharacterExecuteGuardedCompat(sql);
}

// Every row at startup (one query, O(rows)): the bot may already be in the world when the module starts, and a login
// takes its own hold from this map instead of asking again.
void LoadStoredHolds()
{
    storedHolds.clear();
    if (!EnsureStore(config.enabled))
        return;
    QueryResult result = CharacterQueryGuardedCompat("SELECT `guid`, `slot`, `school`, `own_guid`, `resist_guid`, `releasing` FROM `"
        + std::string(kHoldTable) + "` ORDER BY `guid`, `slot`");
    if (!result)
        return;
    uint32 rows = 0;
    do
    {
        Field* fields = result->Fetch();
        uint64 const key = fields[0].GetUInt64();
        uint8 const slot = fields[1].GetUInt8();
        uint8 const school = fields[2].GetUInt8();
        uint64 const own = fields[3].GetUInt64();
        uint64 const resist = fields[4].GetUInt64();
        if (!key || !resist)
            continue;
        Hold& hold = storedHolds[key];
        hold.school = school;
        hold.state = fields[5].GetUInt8() ? ai::HoldState::Releasing : ai::HoldState::Held;
        hold.pieces.push_back(HeldPiece{ slot, own ? ItemGuid(own) : ObjectGuid(), ItemGuid(resist) });
        ++rows;
    } while (result->NextRow());
    if (rows)
        LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots resist gear: loaded {} stored piece(s) in {} hold(s)", rows, uint32(storedHolds.size()));
}

// Gives a stored hold to a bot that is in the world now and starts the restore.
void RecoverHold(Player* bot, Hold hold, char const* why)
{
    if (!bot || hold.pieces.empty() || holds.find(bot->GetGUID()) != holds.end())
        return;
    // A stored hold is recovered to be put back, so it is RELEASING from here on (also in the row, before the first pass).
    MarkReleasing(bot->GetGUID().GetCounter(), hold);
    hold.attempts = 0;
    hold.nextRetryMs = clockMs + uint64(ai::kRetrySeconds) * 1000;
    int const school = hold.school;
    uint32 const pieces = uint32(hold.pieces.size());
    holds.emplace(bot->GetGUID(), std::move(hold));
    LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots resist gear: {} recovered a stored {} set ({} piece(s))", bot->GetName(), SchoolWord(school), pieces);
    Restore(bot, why);
}

// A module start with bots already in the world: every stored hold whose bot is online is restored now.
void RecoverStoredForOnline()
{
    for (auto it = storedHolds.begin(); it != storedHolds.end();)
    {
        Player* bot = ConnectedPlayer(PlayerGuid(it->first));
        if (!bot || !BotMgr::Instance().IsBot(bot->GetGUID()))
        {
            ++it;
            continue;
        }
        if (holds.find(bot->GetGUID()) != holds.end())
        {
            // A login already adopted this hold (or one was taken since the load): the live hold and the store are the
            // authority, the startup snapshot is stale.
            it = storedHolds.erase(it);
            continue;
        }
        Hold hold = std::move(it->second);
        it = storedHolds.erase(it);
        RecoverHold(bot, std::move(hold), "startup");
    }
}

// TortoiseBots.RaidResistGear.Enable went to 0 while a set is held: put it back now for every bot that can take it
// (alive, out of combat) and keep the hold for every piece that did not move. Only a pass that really brings the last
// piece back clears a hold (Restore does), so switching the key off never strands a resist piece: the world tick keeps
// trying with the backoff, and the store restores the set at the next login.
void ReleaseAll(char const* why)
{
    std::vector<ObjectGuid> keys;
    keys.reserve(holds.size());
    for (auto const& entry : holds)
        keys.push_back(entry.first);
    for (ObjectGuid const& guid : keys)
    {
        auto it = holds.find(guid);
        if (it == holds.end())
            continue;
        // Even a bot that cannot take its pieces back yet: the state is durable, so a restart finishes the release.
        MarkReleasing(guid.GetCounter(), it->second);
        Player* bot = ConnectedPlayer(guid);
        if (bot && ai::ShouldRetryRestore(bot->IsAlive(), bot->IsInCombat()))
            Restore(bot, why);
        it = holds.find(guid);
        if (it == holds.end())
            continue;
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the feature is off; {} piece(s) stay on and are put back as soon as the bot can wear them again", bot ? bot->GetName() : "?", uint32(it->second.pieces.size()));
    }
}
} // namespace

void LoadConfig()
{
    bool const wasEnabled = config.enabled;
    config.enabled = sConfigMgr->GetIntDefault("TortoiseBots.RaidResistGear.Enable", 0) != 0;
    config.range = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.RaidResistGear.Range", 60), 5, 200));
    config.releaseSeconds = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.RaidResistGear.ReleaseSeconds", 15), 0, 600));
    // A reload is when an operator may have applied the migration the startup probe found missing: ask the store once
    // more (the answer is otherwise remembered; it is never re-asked by a call).
    storeProbed = false;
    storeCreated = false;
    // The index depends only on the data file, never on a config value: a reload does not rebuild it.
    if (ai::ReleaseOnDisable(wasEnabled, config.enabled, !holds.empty()))
    {
        LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots resist gear: the feature was switched off; putting {} held set(s) back", uint32(holds.size()));
        ReleaseAll("disable");
    }
}

bool Enabled()
{
    return config.enabled;
}

uint32 Range()
{
    return config.range;
}

uint32 ReleaseSeconds()
{
    return config.releaseSeconds;
}

void BuildIndex()
{
    schoolByEntry.clear();
    indexBuilt = false;
    // The rows are checked against the spell mirror, which answers nothing until its store is ready and builds nothing
    // while the module is off. Leave the index unbuilt then: SchoolFor tries again once the mirror answers (a world that
    // started with TortoiseBots.Enable = 0 must not cache an empty table for good).
    if (tbcompat::MaxSpellId() == 0)
    {
        LogAt("server.loading", LOG_LEVEL_INFO, "TortoiseBots: resist gear index left unbuilt (the spell store is not ready); the first lookup builds it");
        return;
    }
    for (size_t i = 0; i < tbdata::resist::kResistRowCount; ++i)
    {
        tbdata::resist::BossResistRow const& row = tbdata::resist::kBossResist[i];
        SpellEntry const* spell = tbcompat::SpellProto(row.evidenceSpell);
        if (!spell)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: resist gear row {} ({}) dropped: evidence spell {} is not in the spell store", row.entry, row.name, row.evidenceSpell);
            continue;
        }
        if (spell->School != row.school)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: resist gear row {} ({}) dropped: evidence spell {} is school {}, the row says {}", row.entry, row.name, row.evidenceSpell, spell->School, int(row.school));
            continue;
        }
        if (row.school < 1 || row.school > 6)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: resist gear row {} ({}) dropped: school {} is not a spell school", row.entry, row.name, int(row.school));
            continue;
        }
        schoolByEntry[row.entry] = row.school;
    }
    indexBuilt = true;
    LogAt("server.loading", LOG_LEVEL_INFO, "TortoiseBots: resist gear index built ({} bosses of {} rows, range {} yd, release {} s)", schoolByEntry.size(), tbdata::resist::kResistRowCount, config.range, config.releaseSeconds);
}

int SchoolFor(uint32 entry)
{
    if (!indexBuilt)
    {
        // BuildIndex compares every row with the spell mirror, which answers nothing until its store is ready (and nothing
        // is built while the module is off: TortoiseBots.Enable read at boot). A world started with the module off would
        // otherwise cache an empty index for good, so the index waits for the mirror; MaxSpellId is that readiness probe.
        if (tbcompat::MaxSpellId() == 0)
            return 0;
        BuildIndex();
    }
    auto const it = schoolByEntry.find(entry);
    return it == schoolByEntry.end() ? 0 : it->second;
}

char const* SchoolWord(int school)
{
    switch (school)
    {
        case 1: return "holy";
        case 2: return "fire";
        case 3: return "nature";
        case 4: return "frost";
        case 5: return "shadow";
        case 6: return "arcane";
        default: return "";
    }
}

bool Apply(Player* bot, int school)
{
    if (!config.enabled || !bot || school < 1 || school > 6)
        return false;
    if (!bot->IsAlive() || bot->IsInCombat() || Holding(bot))
        return false;
    if (!bot->GetMap() || !bot->GetMap()->IsRaid())
        return false;
    if (!BotMgr::Instance().IsBot(bot->GetGUID()) || !GroupHasRealPlayer(bot))
        return false;

    bool const tank = IsTank(bot);

    // The plans carry a 64-bit identity per piece (the pure policy is core-free and must not see an ObjectGuid); these two
    // maps are that identity, for this call only: a guid is one token however many slots it may fill (a ring is listed for
    // both fingers), so the plan can never use one piece twice.
    std::map<ObjectGuid, uint64> tokenOf;
    std::map<uint64, ObjectGuid> guidOf;
    uint64 nextToken = 0;
    auto token = [&](ObjectGuid const& guid) -> uint64
    {
        auto const it = tokenOf.find(guid);
        if (it != tokenOf.end())
            return it->second;
        uint64 const value = ++nextToken;
        tokenOf.emplace(guid, value);
        guidOf.emplace(value, guid);
        return value;
    };

    std::vector<ai::Piece> worn;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        ai::Piece piece;
        piece.guid = token(item->GetGUID());
        piece.slot = slot;
        piece.res = SchoolResistance(item, school);
        piece.pinned = IsPinned(bot, item->GetEntry());
        worn.push_back(piece);
    }

    std::vector<ai::Piece> bag;
    ForEachBagItem(bot,
        [&](Item* item)
        {
            if (!item)
                return;
            int32 const res = SchoolResistance(item, school);
            if (res <= 0)
                return;
            // The slots this item may fill: the core's own answer, plus the other finger for a ring (a ring fits
            // either hand and the plan must be able to use two of them).
            std::vector<uint8> slots;
            uint8 const primary = bot->FindEquipSlot(item, NULL_SLOT, true);
            if (primary < EQUIPMENT_SLOT_END)
                slots.push_back(primary);
            if (item->GetTemplate() && item->GetTemplate()->GetInventoryType() == INVTYPE_FINGER)
                for (uint8 finger : { ai::kSlotFinger1, ai::kSlotFinger2 })
                    if (std::find(slots.begin(), slots.end(), finger) == slots.end())
                        slots.push_back(finger);
            for (uint8 slot : slots)
            {
                if (!ai::SlotAllowed(slot, tank))
                    continue;
                // The core's check for THIS bot in THAT slot, with the worn piece still in place (swap = true).
                uint16 dest = 0;
                if (bot->CanEquipItem(slot, dest, item, true) != EQUIP_ERR_OK)
                    continue;
                ai::Piece piece;
                piece.guid = token(item->GetGUID());
                piece.slot = slot;
                piece.res = res;
                bag.push_back(piece);
            }
        });

    std::vector<ai::Swap> const swaps = ai::PlanSwaps(school, tank, worn, bag);
    if (swaps.empty())
        return false;

    // The plan as the store records it: the own piece the plan replaces (empty for a slot that was empty).
    std::vector<HeldPiece> planned;
    for (ai::Swap const& swap : swaps)
    {
        auto const resistIt = guidOf.find(swap.resistGuid);
        if (resistIt == guidOf.end())
            continue;
        ObjectGuid own;
        if (swap.wornGuid != 0)
        {
            auto const ownIt = guidOf.find(swap.wornGuid);
            if (ownIt != guidOf.end())
                own = ownIt->second;
        }
        planned.push_back(HeldPiece{ swap.slot, own, resistIt->second });
    }
    if (planned.empty())
        return false;

    // Record the intended set BEFORE any swap, and only swap once the record is really in the database: a crash halfway
    // then leaves rows the next login reconciles (a slot that did not get its piece is stale and forgotten), and a bot
    // never wears a resist piece the store has no row for. No recovery record, no swap.
    uint64 const key = bot->GetGUID().GetCounter();
    if (!PersistHold(key, Hold{ school, planned }))
    {
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the {} set's recovery record could not be written, so no piece was swapped", bot->GetName(), SchoolWord(school));
        return false;
    }

    std::vector<HeldPiece> pieces;
    std::vector<uint8> failed;
    for (HeldPiece const& piece : planned)
    {
        Item* resist = bot->GetItemByGuid(piece.resist);
        uint16 const src = resist ? resist->GetPos() : 0;
        uint16 const dst = EquipPos(piece.slot);
        if (!resist || src == dst)
        {
            failed.push_back(piece.slot);
            continue;
        }
        bot->SwapItem(src, dst);
        Item* now = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, piece.slot);
        if (!now || now->GetGUID() != piece.resist)
        {
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the core refused to swap a {} piece into slot {}", bot->GetName(), SchoolWord(school), uint32(piece.slot));
            failed.push_back(piece.slot);
            continue;
        }
        pieces.push_back(piece);
    }
    if (pieces.empty())
    {
        ForgetPieces(key, {});
        return false;
    }
    if (!failed.empty())
        ForgetPieces(key, failed);

    uint32 const pieceCount = uint32(pieces.size());
    LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots resist gear: {} put on {} {} piece(s)", bot->GetName(), pieceCount, SchoolWord(school));
    // HELD from here on: the set stays on through the pull and the world tick has nothing to retry until a release asks.
    holds.emplace(bot->GetGUID(), Hold{ school, std::move(pieces) });
    storedHolds.erase(key);   // a stale snapshot row for this bot is superseded by the hold just written
    leftHolds.erase(key);
    return true;
}

bool Restore(Player* bot, char const* why)
{
    if (!bot)
        return false;
    auto it = holds.find(bot->GetGUID());
    if (it == holds.end())
        return false;

    uint64 const key = bot->GetGUID().GetCounter();
    // The restore was asked for: the hold is RELEASING (and the row says so) from now on, so the tick retries a pass
    // that could not finish and a restart finishes it. A plain Held hold is never touched by the tick.
    MarkReleasing(key, it->second);

    std::string const reason = why ? why : "restore";
    uint32 const pieces = uint32(it->second.pieces.size());
    // A dead bot's hold is not touched: every piece stays recorded (and stored) for a later, living retry. A logout is
    // the one caller whose Player is about to go, so the memory entry moves to the startup snapshot, from which the
    // next login restores it without asking the database.
    if (!ai::MayRestoreHold(bot->IsAlive()))
    {
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} could not put its own pieces back ({}): it is dead, {} piece(s) stay on and the hold is kept", bot->GetName(), reason, pieces);
        if (reason == "logout")
        {
            leftHolds[key] = std::move(it->second);
            holds.erase(it);
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} left the world with {} piece(s) still on; the store restores them at the next login", bot->GetName(), pieces);
        }
        return false;
    }

    std::vector<HeldPiece> remaining;
    std::vector<uint8> moved;      // slots whose piece went back or whose row is stale: forgotten from the store
    uint32 restored = 0;
    for (HeldPiece const& piece : it->second.pieces)
    {
        Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, piece.slot);
        bool const slotHoldsResist = worn && worn->GetGUID() == piece.resist;
        bool const ownWasEmpty = piece.own.IsEmpty();
        // Only a piece in the bags can be swapped back in. The bank (and a trade or the mail) is not a swap source, so a
        // piece parked there is reported and held: the hold keeps it for the day it returns to the bags.
        Item* own = ownWasEmpty ? nullptr : FindBagItem(bot, piece.own);
        bool const ownOutsideBags = !own && !ownWasEmpty && bot->GetItemByGuid(piece.own) != nullptr;
        uint16 dest = 0;
        bool const canEquipOwn = own && bot->CanEquipItem(piece.slot, dest, own, true) == EQUIP_ERR_OK;
        // The slot was empty when the set went on: the resist piece comes off into the first free bag position (the bag
        // slot it came from was freed by the swap and is normally still free).
        uint16 freePos = 0;
        bool const freeBagPos = slotHoldsResist && ownWasEmpty && FindFreeBagPos(bot, freePos);

        if (slotHoldsResist && ownOutsideBags)
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}'s own piece of slot {} is not in the bags (it may be in the bank), so it cannot be swapped back ({}); the resist piece stays and the hold is kept", bot->GetName(), uint32(piece.slot), reason);
        else if (slotHoldsResist && !ownWasEmpty && !own)
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}'s own piece of slot {} is no longer in the bags ({}); the resist piece stays", bot->GetName(), uint32(piece.slot), reason);
        else if (slotHoldsResist && !ownWasEmpty && own && !canEquipOwn)
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} cannot wear its own piece of slot {} again ({}); the resist piece stays", bot->GetName(), uint32(piece.slot), reason);
        else if (slotHoldsResist && ownWasEmpty && !freeBagPos)
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} has no free bag slot to take the resist piece off slot {} ({}); it stays on", bot->GetName(), uint32(piece.slot), reason);

        switch (ai::PlanPieceBack(slotHoldsResist, ownWasEmpty, own != nullptr, canEquipOwn, freeBagPos))
        {
            case ai::PieceBack::DropStale:
                // Nothing this hold put on is in the slot any more: forget the piece rather than retry it forever.
                LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} slot {} no longer holds the resist piece ({}); forgetting it", bot->GetName(), uint32(piece.slot), reason);
                moved.push_back(piece.slot);
                break;
            case ai::PieceBack::BagResist:
                bot->SwapItem(EquipPos(piece.slot), freePos);
                if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, piece.slot))
                {
                    ++restored;
                    moved.push_back(piece.slot);
                }
                else
                {
                    LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the core refused to take the resist piece off slot {} ({})", bot->GetName(), uint32(piece.slot), reason);
                    remaining.push_back(piece);
                }
                break;
            case ai::PieceBack::SwapOwn:
                bot->SwapItem(own->GetPos(), EquipPos(piece.slot));
                if (Item* now = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, piece.slot); now && now->GetGUID() == piece.own)
                {
                    ++restored;
                    moved.push_back(piece.slot);
                }
                else
                {
                    LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the core refused to put the own piece of slot {} back ({})", bot->GetName(), uint32(piece.slot), reason);
                    remaining.push_back(piece);
                }
                break;
            case ai::PieceBack::Hold:
                remaining.push_back(piece);
                break;
        }
    }

    it->second.pieces = std::move(remaining);
    if (!ai::HoldHasPieces(it->second.pieces.size()))
    {
        // Every piece accounted for: one statement clears the hold's rows (a stale row cannot survive it).
        holds.erase(it);
        storedHolds.erase(key);
        leftHolds.erase(key);
        ForgetPieces(key, {});
    }
    else
    {
        if (!moved.empty())
            ForgetPieces(key, moved);
        ++it->second.attempts;
        it->second.nextRetryMs = clockMs + uint64(ai::RetryDelaySeconds(it->second.attempts)) * 1000;
        if (reason == "logout")
        {
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} put back {}/{} piece(s) ({}); the rest stays in the store for the next login", bot->GetName(), restored, pieces, reason);
            leftHolds[key] = std::move(it->second);
            holds.erase(it);
            return restored > 0;
        }
        if (it->second.attempts == ai::kMaxRestoreAttempts && !it->second.backoffLogged)
        {
            it->second.backoffLogged = true;
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} put back {}/{} piece(s) ({}); the fast retries are spent, so the rest is retried every {} s until it can move", bot->GetName(), restored, pieces, reason, ai::kBackoffRetrySeconds);
        }
        else
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} put back {}/{} piece(s) ({}); the rest stays held", bot->GetName(), restored, pieces, reason);
        return restored > 0;
    }
    if (restored == pieces)
        LogAt("tortoisebots", LOG_LEVEL_INFO, "TortoiseBots resist gear: {} put back {} piece(s) ({})", bot->GetName(), restored, reason);
    else
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {} put back {}/{} piece(s) ({}); the rest were no longer there", bot->GetName(), restored, pieces, reason);
    return restored > 0;
}

// The module start (OnWorldInitialized): one existence question, then one read of every stored hold (only when the
// table is really there), then a restore for a bot already in the world. With the feature off a server that never used
// it stops after the question: no CREATE, no read, no warning.
void LoadAtStartup()
{
    if (!BotMgr::Instance().Enabled())
        return;
    if (!EnsureStore(config.enabled))
        return;
    LoadStoredHolds();
    RecoverStoredForOnline();
}

// A bot entered the world: put back a stored set (a crash, or a logout that could not finish) when no hold is in memory.
// The startup snapshot and the holds left by a logout answer, never a query: a server with the feature off pays nothing
// at any login.
void RecoverLogin(Player* bot)
{
    if (!bot || !BotMgr::Instance().IsBot(bot->GetGUID()))
        return;
    if (Holding(bot))
        return;
    uint64 const key = bot->GetGUID().GetCounter();
    auto stored = storedHolds.find(key);
    auto left = leftHolds.find(key);
    if (stored == storedHolds.end() && left == leftHolds.end())
        return;
    Hold hold = stored != storedHolds.end() ? std::move(stored->second) : std::move(left->second);
    storedHolds.erase(key);
    leftHolds.erase(key);
    if (!ai::RecoverOnLogin(!hold.pieces.empty(), false))
        return;
    RecoverHold(bot, std::move(hold), "login");
}

// The world tick: a stored hold whose bot has come online since the module start, and the retry of an unfinished
// restore (one sweep a second; the first passes kRetrySeconds apart, then kBackoffRetrySeconds apart, never stopping
// while the bot lives out of combat).
void Update(uint32 diff)
{
    clockMs += diff;
    if (!storedHolds.empty() && clockMs - lastStoredScanMs >= uint64(ai::kRetrySeconds) * 1000)
    {
        lastStoredScanMs = clockMs;
        RecoverStoredForOnline();
    }
    if (holds.empty() || clockMs - lastSweepMs < 1000)
        return;
    lastSweepMs = clockMs;

    // Only a restore that was asked for is retried. A hold that is merely HELD (the set is on for the pull) is never
    // touched here, so the tick cannot put the bot's own pieces back five seconds after it armed.
    std::vector<ObjectGuid> due;
    for (auto const& entry : holds)
        if (ai::ShouldRetryHold(entry.second.state) && entry.second.nextRetryMs <= clockMs)
            due.push_back(entry.first);

    for (ObjectGuid const& guid : due)
    {
        auto it = holds.find(guid);
        if (it == holds.end())
            continue;
        Player* bot = ConnectedPlayer(guid);
        if (!bot)
        {
            it->second.nextRetryMs = clockMs + uint64(ai::kRetrySeconds) * 1000;
            continue;
        }
        if (!ai::ShouldRetryRestore(bot->IsAlive(), bot->IsInCombat()))
        {
            // Dead or fighting: the hold waits for a living pass out of combat (and never stops waiting).
            it->second.nextRetryMs = clockMs + uint64(ai::RetryDelaySeconds(it->second.attempts)) * 1000;
            continue;
        }
        Restore(bot, "retry");
    }
}

bool Holding(Player* bot)
{
    return bot && holds.find(bot->GetGUID()) != holds.end();
}

int HeldSchool(Player* bot)
{
    if (!bot)
        return 0;
    auto const it = holds.find(bot->GetGUID());
    return it == holds.end() ? 0 : it->second.school;
}

uint32 HeldCount(Player* bot)
{
    if (!bot)
        return 0;
    auto const it = holds.find(bot->GetGUID());
    return it == holds.end() ? 0 : uint32(it->second.pieces.size());
}

#if defined(TB_RESIST_GEAR_TEST)
void ResetForTest()
{
    holds.clear();
    storedHolds.clear();
    leftHolds.clear();
    storeProbed = false;
    storeCreated = false;
    storeAvailable = false;
    storeWarned = false;
    clockMs = 0;
    lastSweepMs = 0;
    lastStoredScanMs = 0;
    config = Config{};
}
#endif
}
}
