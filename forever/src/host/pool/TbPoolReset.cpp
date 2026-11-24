// The managed pool's startup reset, see host/pool/TbPoolReset.h. World thread only.
//
// This file gathers facts from the registry, the core's characters table, the session table and the guild tables, and
// hands them to ai/playerbot/policy/world/PoolResetSafetyPolicy.h; the setting and one-shot rules are
// runtime/policy/infra/PoolResetPolicy.h's. It deletes a character only through Player::DeleteFromDB, one per tick,
// after re-reading every fact for that one target, and records the generation only after the characters table proves
// the targets gone. No query here is built from configuration text: a token is escaped as a string literal and every
// other value is a number.
#include "host/pool/TbPoolReset.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Log.h"

#ifdef TORTOISEBOTS_SLICE1

#include "host/pool/TbPoolCreate.h"
#include "host/pool/TbPoolRegistry.h"
#include "TbBotMgr.h"

#include "ai/playerbot/policy/world/PoolResetSafetyPolicy.h"
#include "runtime/policy/infra/PoolResetPolicy.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbCompat_WL0.h"

#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

namespace TortoiseBots
{
namespace pool
{
namespace Reset
{
namespace
{

// One row of the reset's own state table; "random" is the pool TurtleBots's reset names, kept for the same meaning.
constexpr char const* kPoolName = "random";
constexpr char const* kStateTable = "tb_pool_state";
constexpr char const* kLineageTable = "tb_pool_lineage";

// A session that refuses to close must stop the reset, never become "delete anyway".
constexpr std::uint32_t kDrainTimeoutMs = 120000;
constexpr std::uint32_t kDeleteStallTimeoutMs = 120000;
// Player::DeleteFromDB commits through the character database's queue, so a deletion can be visible for a moment.
constexpr std::uint32_t kVerifyTimeoutMs = 15000;
constexpr std::uint32_t kProgressEvery = 25;

enum class Phase
{
    Disabled,
    Planning,
    Deleting,
    Verifying,
    Rebuilding,
    Complete,
    Failed,
};

char const* PhaseText(Phase phase)
{
    switch (phase)
    {
        case Phase::Planning:   return "planning";
        case Phase::Deleting:   return "deleting";
        case Phase::Verifying:  return "verifying";
        case Phase::Rebuilding: return "rebuilding";
        case Phase::Complete:   return "complete";
        case Phase::Failed:     return "failed";
        case Phase::Disabled:   break;
    }
    return "disabled";
}

struct Target
{
    std::uint64_t guid = 0;
    std::uint32_t accountId = 0;
    std::string name;
    std::string username;
};

struct State
{
    PoolResetSetting setting;
    Phase phase = Phase::Disabled;
    bool maintenance = false;
    bool haveApplied = false;
    std::string applied;
    std::vector<Target> targets;
    std::uint32_t kept = 0;
    std::vector<std::string> keptNames;
    std::size_t next = 0;
    std::uint32_t stallMs = 0;
};

State& Get()
{
    static State state;
    return state;
}

void ResetState(State& s)
{
    s = State();
}

std::string SqlText(std::string const& value)
{
    std::string out = "'";
    for (char const c : value)
    {
        if (c == '\'' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '\'';
    return out;
}

std::string IdList(std::vector<std::uint64_t> const& ids)
{
    std::string list;
    for (std::uint64_t const id : ids)
        list += (list.empty() ? "" : ",") + std::to_string(id);
    return list;
}

std::string AccountIdList(std::set<std::uint32_t> const& ids)
{
    std::string list;
    for (std::uint32_t const id : ids)
        list += (list.empty() ? "" : ",") + std::to_string(id);
    return list;
}

// `once:<token>` / `always` / `off`, as the status line reports it.
std::string SettingText(PoolResetSetting const& setting)
{
    switch (setting.mode)
    {
        case PoolResetMode::Once:    return "once:" + setting.token;
        case PoolResetMode::Always:  return "always";
        case PoolResetMode::Invalid: return "invalid";
        case PoolResetMode::Off:     break;
    }
    return "off";
}

void Abort(std::string const& reason)
{
    State& s = Get();
    s.phase = Phase::Failed;
    TC_LOG_ERROR("tortoisebots", "TortoiseBots: random pool reset aborted before any deletion: {}", reason);
}

void Fail(std::string const& reason)
{
    State& s = Get();
    s.phase = Phase::Failed;
    TC_LOG_ERROR("tortoisebots", "TortoiseBots: random pool reset failed: {}", reason);
    if (s.maintenance)
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: random pool consumers stay paused and the generation was NOT recorded, so the next server start resumes the reset");
}

bool EnsureStateTable()
{
    CharacterExecuteGuardedCompat(Trinity::StringFormat(
        "CREATE TABLE IF NOT EXISTS `{}` ("
        "`pool_name` varchar(32) NOT NULL,"
        "`applied_generation` varchar(128) NOT NULL DEFAULT '',"
        "`completed_at` datetime DEFAULT NULL,"
        "PRIMARY KEY (`pool_name`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", kStateTable));
    std::set<std::string> const columns = { "pool_name", "applied_generation", "completed_at" };
    return CharacterSchemaHasCompat(kStateTable, columns);
}

// True when the table answered (an empty table is an answer); `out` is the generation, empty when no row is stored.
bool LoadAppliedGeneration(std::string& out)
{
    out.clear();
    QueryResult count = CharacterQueryGuardedCompat(
        Trinity::StringFormat("SELECT COUNT(*) FROM `{}` WHERE `pool_name` = {}", kStateTable, SqlText(kPoolName)));
    if (!count)
        return false;
    if (!count->Fetch()[0].GetUInt64())
        return true;
    QueryResult row = CharacterQueryGuardedCompat(
        Trinity::StringFormat("SELECT `applied_generation` FROM `{}` WHERE `pool_name` = {}", kStateTable, SqlText(kPoolName)));
    if (!row)
        return false;
    out = row->Fetch()[0].GetString();
    return true;
}

// The generation is recorded last, after verification. `always` writes only the completion time and never overwrites a
// stored one-shot token.
bool ApplyGeneration(PoolResetSetting const& setting)
{
    std::set<std::string> const columns = { "pool_name", "applied_generation", "completed_at" };
    if (!CharacterSchemaHasCompat(kStateTable, columns))
        return false;
    if (setting.mode == PoolResetMode::Once)
    {
        CharacterExecuteGuardedCompat(Trinity::StringFormat(
            "INSERT INTO `{}` (`pool_name`, `applied_generation`, `completed_at`) VALUES ({}, {}, NOW()) "
            "ON DUPLICATE KEY UPDATE `applied_generation` = VALUES(`applied_generation`), `completed_at` = NOW()",
            kStateTable, SqlText(kPoolName), SqlText(setting.token)));
        std::string applied;
        if (!LoadAppliedGeneration(applied))
            return false;
        return applied == setting.token;
    }
    CharacterExecuteGuardedCompat(Trinity::StringFormat(
        "INSERT INTO `{}` (`pool_name`, `applied_generation`, `completed_at`) VALUES ({}, '', NOW()) "
        "ON DUPLICATE KEY UPDATE `completed_at` = NOW()", kStateTable, SqlText(kPoolName)));
    std::string applied;
    return LoadAppliedGeneration(applied);
}

// A pool bot still in the world is drained, never deleted; a human session is final.
bool Drain(State& s, std::string& why, bool& busy)
{
    busy = false;
    for (Target const& target : s.targets)
    {
        if (WorldSession* session = sWorld->FindSession(target.accountId))
        {
            if (!session->IsBot())
            {
                why = Trinity::StringFormat("pool character {} ({}) has a session that is not a headless bot; human sessions are never deleted",
                    target.name, target.guid);
                return false;
            }
        }
        if (BotMgr::Instance().IsBot(ObjectGuid::Create<HighGuid::Player>(std::uint32_t(target.guid))))
        {
            BotMgr::Instance().Logout(ObjectGuid::Create<HighGuid::Player>(std::uint32_t(target.guid)), "pool reset");
            busy = true;
            continue;
        }
        if (ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(std::uint32_t(target.guid))))
            busy = true;
    }
    return true;
}

struct MemberRow
{
    std::uint64_t guid = 0;
    bool characterKnown = false;
    std::uint32_t accountId = 0;
    std::string name;
    std::string guildName;
    std::uint64_t leaderGuid = 0;
};

// The pool's account ids and its ready character guids: the only membership a guild may hold.
struct PoolScope
{
    std::set<std::uint32_t> accounts;
    std::set<std::uint64_t> readyCharacters;
};

PoolScope ScopeOfPool()
{
    PoolScope scope;
    for (Account const& account : Accounts())
        scope.accounts.insert(account.id);
    for (Character const& character : Characters())
        if (character.state == world::PoolRowState::Ready && character.guid)
            scope.readyCharacters.insert(character.guid);
    return scope;
}

// The join is the whole safety rule: a member is outside the pool when the characters table has no row for it, when its
// account is not a ready pool account, or when no ready pool row names it. The safe form needs no list of 4000 guids.
constexpr char const* kGuildJoins =
    " FROM `guild` g JOIN `guild_member` gm ON gm.`guildid` = g.`guildid` "
    "LEFT JOIN `characters` c ON c.`guid` = gm.`guid` "
    "LEFT JOIN `tb_pool_character` p ON p.`guid` = gm.`guid` AND p.`state` = 'ready'";

std::string OutsideClause(PoolScope const& scope)
{
    std::string clause = " AND (c.`account` IS NULL OR p.`guid` IS NULL";
    if (!scope.accounts.empty())
        clause += " OR c.`account` NOT IN (" + AccountIdList(scope.accounts) + ")";
    return clause + ")";
}

bool CountGuildOutsiders(std::string const& filter, PoolScope const& scope, std::uint64_t& count)
{
    count = 0;
    QueryResult result = CharacterQueryGuardedCompat(
        "SELECT COUNT(*)" + std::string(kGuildJoins) + " WHERE " + filter + OutsideClause(scope));
    if (!result)
        return false;
    count = result->Fetch()[0].GetUInt64();
    return true;
}

// Only called once the count above proved at least one outsider, so a null answer is a failed read.
bool ReadGuildOutsiders(std::string const& filter, PoolScope const& scope, std::vector<MemberRow>& out)
{
    out.clear();
    QueryResult result = CharacterQueryGuardedCompat(
        "SELECT gm.`guid`, c.`account`, c.`name` AS `member_name`, g.`name` AS `guild_name`, g.`leaderguid`" + std::string(kGuildJoins) +
        " WHERE " + filter + OutsideClause(scope));
    if (!result)
        return false;
    do
    {
        Field* fields = result->Fetch();
        MemberRow row;
        row.guid = fields[0].GetUInt64();
        row.characterKnown = !fields[1].IsNull();
        row.accountId = fields[1].GetUInt32();
        row.name = fields[2].GetString();
        row.guildName = fields[3].GetString();
        row.leaderGuid = fields[4].GetUInt64();
        out.push_back(std::move(row));
    } while (result->NextRow());
    return true;
}

std::string GuildBlockText(MemberRow const& member)
{
    return "guild '" + member.guildName + "' is led by pool character " + std::to_string(member.leaderGuid) +
        " but holds a member outside the managed pool (character " + std::to_string(member.guid) +
        (member.name.empty() ? std::string() : " '" + member.name + "'") + "); reset aborted";
}

// The SQL join already filters outsiders; the module's own predicate re-decides the fetched rows, so a statement that
// drifted from the rule cannot make the reset delete around a real member. A count that the detail disagrees with is a
// failed re-check, never "clear".
bool FirstGuildOutsider(std::vector<MemberRow> const& rows, PoolScope const& scope, MemberRow const*& offender)
{
    offender = nullptr;
    for (MemberRow const& row : rows)
    {
        world::PoolResetMemberFacts facts;
        facts.guid = row.guid;
        facts.characterKnown = row.characterKnown;
        facts.poolAccount = scope.accounts.count(row.accountId) != 0;
        facts.readyPoolRow = scope.readyCharacters.count(row.guid) != 0;
        if (world::PoolResetMemberOutsidePool(facts))
        {
            offender = &row;
            return true;
        }
    }
    return false;
}

// The startup preflight: one COUNT over every guild the targets lead; the detail read only names the first offender.
bool CheckGuilds(std::vector<Target> const& targets, PoolScope const& scope, std::string& why)
{
    if (targets.empty())
        return true;
    std::vector<std::uint64_t> guids;
    guids.reserve(targets.size());
    for (Target const& target : targets)
        guids.push_back(target.guid);
    std::string const filter = "g.`leaderguid` IN (" + IdList(guids) + ")";
    std::uint64_t count = 0;
    if (!CountGuildOutsiders(filter, scope, count))
    {
        why = "the guild preflight query failed";
        return false;
    }
    if (!count)
        return true;
    std::vector<MemberRow> offenders;
    MemberRow const* offender = nullptr;
    if (!ReadGuildOutsiders(filter, scope, offenders) || !FirstGuildOutsider(offenders, scope, offender))
    {
        why = "the guild membership preflight query failed";
        return false;
    }
    why = GuildBlockText(*offender);
    return false;
}

bool OwnsAuctions(std::uint64_t guid, std::uint64_t& count, bool& answered)
{
    answered = false;
    count = 0;
    QueryResult result = CharacterQueryGuardedCompat(Trinity::StringFormat(
        "SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` = {}", guid));
    if (!result)
        return false;
    answered = true;
    count = result->Fetch()[0].GetUInt64();
    return true;
}

// One query per deletion, re-run for the guilds this target leads.
bool RecheckGuildsOfLeader(std::uint64_t guid, PoolScope const& scope, std::string& why, bool& blocked)
{
    blocked = false;
    std::string const filter = Trinity::StringFormat("g.`leaderguid` = {}", guid);
    std::uint64_t count = 0;
    if (!CountGuildOutsiders(filter, scope, count))
    {
        why = "the guild membership re-check failed";
        return false;
    }
    if (!count)
        return true;
    std::vector<MemberRow> offenders;
    MemberRow const* offender = nullptr;
    if (!ReadGuildOutsiders(filter, scope, offenders) || !FirstGuildOutsider(offenders, scope, offender))
    {
        why = "the guild membership re-check failed";
        return false;
    }
    blocked = true;
    why = GuildBlockText(*offender);
    return true;
}

void DeleteModuleRows(std::uint64_t guid)
{
    struct Table
    {
        char const* table;
        char const* column;
    };
    // Only tables whose column is proven: the columns are literals, the guid a number.
    static Table const tables[] = {
        { "ai_playerbot_db_store", "guid" },
        { "ai_playerbot_custom_strategy", "owner" },
        { "tb_commander_binding", "commander_guid" },
        { "tortoise_bots_guild_order", "bot_guid" },
        { "tortoise_bots_guild_disenchant", "bot_guid" },
    };
    for (Table const& table : tables)
    {
        if (!CharacterSchemaHasCompat(table.table, { table.column }))
            continue;
        CharacterExecuteGuardedCompat(Trinity::StringFormat("DELETE FROM `{}` WHERE `{}` = {}",
            table.table, table.column, guid));
    }
}

// The kept characters of the pool's accounts, one query: the reset reports them and never touches them.
bool ReadPoolAccountCharacters(std::set<std::uint32_t> const& accounts, std::vector<MemberRow>& out, bool& answered)
{
    out.clear();
    answered = false;
    if (accounts.empty())
    {
        answered = true;
        return true;
    }
    QueryResult result = CharacterQueryGuardedCompat(Trinity::StringFormat(
        "SELECT `guid`, `account`, `name` FROM `characters` WHERE `account` IN ({})", AccountIdList(accounts)));
    if (!result)
        return true;                 // answered stays false: a null result is an empty table or a failed read
    answered = true;
    do
    {
        Field* fields = result->Fetch();
        MemberRow row;
        row.guid = fields[0].GetUInt64();
        row.characterKnown = true;
        row.accountId = fields[1].GetUInt32();
        row.name = fields[2].GetString();
        out.push_back(std::move(row));
    } while (result->NextRow());
    return true;
}

bool CountCharacters(std::vector<std::uint64_t> const& guids, std::uint64_t& count)
{
    count = 0;
    if (guids.empty())
        return true;
    QueryResult result = CharacterQueryGuardedCompat(Trinity::StringFormat(
        "SELECT COUNT(*) FROM `characters` WHERE `guid` IN ({})", IdList(guids)));
    if (!result)
        return false;
    count = result->Fetch()[0].GetUInt64();
    return true;
}

enum class Step
{
    Deleted,   // the target is gone, its row removed and its module rows cleaned
    Waiting,   // still a bot or a Player in the world; the caller counts the stall
    Skipped,   // the characters table no longer has it; the row was removed
    Stop,      // the reset stops here (`why` says why); nothing more is deleted
};

// One target's step, every fact read now.
Step DeleteOne(State& s, PoolScope const& scope, std::string& why)
{
    Target& target = s.targets[s.next];
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(std::uint32_t(target.guid));

    world::PoolResetTargetFacts facts;

    // The row itself is re-read: a row that stopped being ready, or now names another guid or account, is a stop.
    Character rowNow;
    if (!pool::FindCharacterRow(target.name, rowNow))
    {
        why = Trinity::StringFormat("pool character {} ({}) has no readable registry row any more; reset aborted", target.name, target.guid);
        return Step::Stop;
    }
    facts.rowReady = rowNow.state == world::PoolRowState::Ready && rowNow.guid == target.guid && rowNow.accountId == target.accountId;

    std::string accountWhy;
    facts.accountTrusted = target.accountId && VerifyAccount(target.accountId, target.username, accountWhy) == world::PoolAccountCheck::Trusted;
    if (!facts.rowReady || !facts.accountTrusted)
    {
        why = Trinity::StringFormat("character {} ({}) is not deleted: its row is no longer a ready pool row on its account ({})",
            target.name, target.guid, facts.accountTrusted ? "the row changed" : accountWhy.empty() ? "its account row is gone" : accountWhy);
        return Step::Stop;
    }

    bool found = false;
    std::uint64_t nowGuid = 0;
    std::uint32_t nowAccount = 0;
    if (!CharacterOwner(target.name, found, nowGuid, nowAccount))
    {
        why = Trinity::StringFormat("character {} ({}) could not be read back", target.name, target.guid);
        return Step::Stop;
    }
    facts.guidExists = found;
    facts.sameAccount = found && nowGuid == target.guid && nowAccount == target.accountId;
    if (!facts.sameAccount && found)
    {
        why = Trinity::StringFormat("character {} ({}) moved to account {} during the reset", target.name, target.guid, nowAccount);
        return Step::Stop;
    }

    if (WorldSession* session = sWorld->FindSession(target.accountId))
        facts.humanSession = !session->IsBot();

    bool auctionsAnswered = false;
    std::uint64_t owned = 0;
    if (!OwnsAuctions(target.guid, owned, auctionsAnswered))
    {
        why = Trinity::StringFormat("the auctions of character {} ({}) could not be read", target.name, target.guid);
        return Step::Stop;
    }
    facts.ownsAuctions = owned != 0;

    bool guildBlocked = false;
    if (facts.sameAccount && !RecheckGuildsOfLeader(target.guid, scope, why, guildBlocked))
        return Step::Stop;
    if (guildBlocked)
        return Step::Stop;           // why is the guild's message

    facts.inWorld = ObjectAccessor::FindConnectedPlayer(guid) != nullptr;
    facts.botRegistered = BotMgr::Instance().IsBot(guid);

    switch (world::DecidePoolResetTarget(facts))
    {
        case world::PoolResetTargetAction::Delete:
            break;
        case world::PoolResetTargetAction::Wait:
            if (facts.botRegistered)
                BotMgr::Instance().Logout(guid, "pool reset");
            return Step::Waiting;
        case world::PoolResetTargetAction::Gone:
            pool::DeleteCharacterRow(target.name);
            ++s.next;
            return Step::Skipped;
        case world::PoolResetTargetAction::Refuse:
            if (facts.humanSession)
                why = Trinity::StringFormat("pool character {} ({}) gained a session that is not a headless bot; human sessions are never deleted", target.name, target.guid);
            else if (facts.ownsAuctions)
                why = Trinity::StringFormat("character {} ({}) owns {} auction listing(s) the core deletion does not remove", target.name, target.guid, owned);
            else
                why = Trinity::StringFormat("character {} ({}) is no longer the pool's; it is not deleted", target.name, target.guid);
            return Step::Stop;
    }

    Player::DeleteFromDB(guid, target.accountId, true, true);
    if (!pool::DeleteCharacterRow(target.name))
    {
        why = Trinity::StringFormat("character {} ({}) was deleted, but its pool row could not be removed", target.name, target.guid);
        return Step::Stop;
    }
    DeleteModuleRows(target.guid);
    ++s.next;
    return Step::Deleted;
}

} // namespace

void PlanAtStartup(std::string const& configuredValue)
{
    State& s = Get();
    ResetState(s);
    s.setting = ParsePoolResetSetting(configuredValue);

    if (s.setting.mode == PoolResetMode::Off)
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset off");
        return;
    }

    std::string failure;
    if (s.setting.mode == PoolResetMode::Invalid)
    {
        failure = s.setting.error;
        Abort(failure);
        return;
    }

    if (!pool::Enabled())
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset skipped: the pool is off (TortoiseBots.Pool.Enable = 0, or its tables are missing)");
        return;
    }

    if (!EnsureStateTable())
    {
        failure = Trinity::StringFormat("`{}` is missing and could not be created; apply forever/sql/characters/2026_11_23_03_tb_pool_state.sql", kStateTable);
        Abort(failure);
        return;
    }

    std::string applied;
    s.haveApplied = LoadAppliedGeneration(applied);
    s.applied = applied;
    if (!s.haveApplied)
    {
        failure = "the applied generation could not be read";
        Abort(failure);
        return;
    }

    world::PoolResetPlanFacts plan;
    plan.settingOff = false;
    plan.settingInvalid = false;
    plan.poolEnabled = true;
    plan.refillPossible = CreatePerMinute() > 0;
    plan.stateAnswered = true;
    plan.generationPending = ShouldResetForGeneration(s.setting, applied);
    plan.targetsAnswered = true;

    if (!plan.generationPending)
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool generation '{}' already applied; reset skipped", applied);
        return;
    }

    if (!plan.refillPossible)
    {
        failure = "TortoiseBots.Pool.CreatePerMinute is 0, so a reset could not refill the pool";
        Abort(failure);
        return;
    }

    // Snapshot: only ready rows. A ready row always names a character; a row that does not is a broken registry and
    // stops the reset (nothing is guessed).
    std::set<std::uint32_t> accountIds;
    for (Character const& character : Characters())
    {
        if (character.state != world::PoolRowState::Ready)
            continue;
        if (!character.guid || !character.accountId || character.name.empty())
        {
            failure = Trinity::StringFormat("pool row {} is ready but names no character or account", character.name);
            Abort(failure);
            return;
        }
        Target target;
        target.guid = character.guid;
        target.accountId = character.accountId;
        target.name = character.name;
        s.targets.push_back(std::move(target));
        accountIds.insert(s.targets.back().accountId);
    }

    // Every ready row must be in the snapshot. A ready row whose account the module cannot prove (the auth database did
    // not answer, a stale row) is not a target; recording the generation while it exists would burn the token without
    // resetting the pool. Fail closed: nothing is deleted and the next start plans again.
    std::size_t readyRows = 0;
    for (Character const& row : ReadRows().characters)
        if (row.state == world::PoolRowState::Ready)
            ++readyRows;
    if (readyRows != s.targets.size())
    {
        failure = Trinity::StringFormat("the pool registry holds {} ready row(s) but only {} could be proven the pool's (a stale row, or the auth database did not answer); reset aborted",
            std::uint32_t(readyRows), std::uint32_t(s.targets.size()));
        plan.accountsTrusted = false;
        Abort(failure);
        return;
    }

    // Every account is re-proved from the account manager, and its username is kept for the per-delete re-check.
    std::vector<Account> const accounts = Accounts();
    for (Target& target : s.targets)
    {
        auto const account = std::find_if(accounts.begin(), accounts.end(),
            [&](Account const& row) { return row.id == target.accountId; });
        if (account != accounts.end())
            target.username = account->username;
    }
    for (std::uint32_t const accountId : accountIds)
    {
        Account row;
        bool haveRow = false;
        for (Account const& account : accounts)
        {
            if (account.id == accountId)
            {
                row = account;
                haveRow = true;
                break;
            }
        }
        if (!haveRow || pool::CheckAccount(row) != world::PoolAccountCheck::Trusted)
        {
            failure = Trinity::StringFormat("target account #{} is no longer the pool's ({}); nothing is deleted",
                accountId, haveRow ? world::PoolAccountCheckText(pool::CheckAccount(row)) : "its row is gone");
            plan.accountsTrusted = false;
            Abort(failure);
            return;
        }
    }
    plan.accountsTrusted = true;

    PoolScope const scope = ScopeOfPool();

    // The characters table is the authority for what exists on the pool's accounts: targets must still be there, on
    // their own account, and everything else is kept.
    std::vector<MemberRow> accountCharacters;
    bool charactersAnswered = false;
    ReadPoolAccountCharacters(scope.accounts, accountCharacters, charactersAnswered);
    if (!charactersAnswered && !scope.accounts.empty())
    {
        // A null result may be an empty table; a COUNT of the same accounts separates them.
        std::uint64_t count = 0;
        if (!PoolCharactersOnAccountsCompat(scope.accounts, count) || count != 0)
        {
            plan.targetsAnswered = false;
            failure = "the characters database did not answer which characters the pool's accounts hold";
            Abort(failure);
            return;
        }
        charactersAnswered = true;
    }
    if (!charactersAnswered)
    {
        plan.targetsAnswered = false;
        failure = "the characters database did not answer which characters the pool's accounts hold";
        Abort(failure);
        return;
    }

    std::set<std::uint64_t> targetGuids;
    for (Target const& target : s.targets)
        targetGuids.insert(target.guid);
    std::set<std::uint64_t> accountCharacterGuids;
    for (MemberRow const& row : accountCharacters)
    {
        accountCharacterGuids.insert(row.guid);
        if (targetGuids.count(row.guid))
        {
            Target const& target = *std::find_if(s.targets.begin(), s.targets.end(),
                [&](Target const& candidate) { return candidate.guid == row.guid; });
            if (row.accountId != target.accountId || row.name != target.name)
            {
                plan.targetsAnswered = false;
                failure = Trinity::StringFormat("character {} ({}) is not on its pool account #{} any more (the database says {} on #{}); reset aborted",
                    target.name, target.guid, target.accountId, row.name, row.accountId);
                Abort(failure);
                return;
            }
        }
    }
    for (Target const& target : s.targets)
    {
        if (!accountCharacterGuids.count(target.guid))
        {
            plan.targetsAnswered = false;
            failure = Trinity::StringFormat("pool character {} ({}) is not in the characters table; reset aborted", target.name, target.guid);
            Abort(failure);
            return;
        }
    }
    for (MemberRow const& row : accountCharacters)
    {
        if (targetGuids.count(row.guid))
            continue;
        ++s.kept;
        if (s.keptNames.size() < 8)
            s.keptNames.push_back(row.name);
    }

    // A human session on a target's account stops it before anything is deleted.
    plan.noHumanSession = true;
    for (std::uint32_t const accountId : accountIds)
    {
        if (WorldSession* session = sWorld->FindSession(accountId))
        {
            if (!session->IsBot())
            {
                plan.noHumanSession = false;
                failure = Trinity::StringFormat("account #{} carries a session that is not a headless bot; human sessions are never deleted", accountId);
                Abort(failure);
                return;
            }
        }
    }

    // Auctions: the core deletion leaves them, so a target that owns one refuses the whole reset.
    plan.noAuctions = true;
    if (!s.targets.empty())
    {
        std::vector<std::uint64_t> guids;
        for (Target const& target : s.targets)
            guids.push_back(target.guid);
        QueryResult result = CharacterQueryGuardedCompat(Trinity::StringFormat(
            "SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` IN ({})", IdList(guids)));
        if (!result)
        {
            plan.noAuctions = false;
            failure = "the auction preflight query failed";
            Abort(failure);
            return;
        }
        std::uint64_t const listings = result->Fetch()[0].GetUInt64();
        if (listings)
        {
            plan.noAuctions = false;
            failure = Trinity::StringFormat("{} auction listing(s) are owned by pool characters and the reset does not settle them; reset aborted", listings);
            Abort(failure);
            return;
        }
    }

    std::string guildWhy;
    plan.guildsClear = CheckGuilds(s.targets, scope, guildWhy);
    if (!plan.guildsClear)
    {
        Abort(guildWhy);
        return;
    }

    char const* reason = nullptr;
    if (world::DecidePoolResetPlan(plan, &reason) != world::PoolResetPlanAction::Schedule)
    {
        Abort(reason ? reason : "the plan was refused");
        return;
    }

    s.maintenance = true;
    s.phase = Phase::Planning;
    s.stallMs = 0;
    std::string const generation = s.setting.mode == PoolResetMode::Once
        ? "'" + s.setting.token + "'"
        : std::string("(always)");
    TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool generation {}; reset scheduled for {} characters on {} managed accounts",
        generation, std::uint32_t(s.targets.size()), std::uint32_t(accountIds.size()));
}

void Update(std::uint32_t diff)
{
    State& s = Get();
    switch (s.phase)
    {
        case Phase::Planning:
        {
            std::string why;
            bool busy = false;
            if (!Drain(s, why, busy))
            {
                Fail(why);
                return;
            }
            if (busy)
            {
                s.stallMs += diff;
                if (s.stallMs >= kDrainTimeoutMs)
                    Fail("pool sessions did not close within 120 seconds");
                return;
            }
            s.stallMs = 0;
            s.phase = Phase::Deleting;
            s.next = 0;
            TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset: {} character(s) to delete, one per world tick",
                std::uint32_t(s.targets.size()));
            return;
        }

        case Phase::Deleting:
        {
            if (s.next >= s.targets.size())
            {
                s.phase = Phase::Verifying;
                s.stallMs = 0;
                return;
            }
            std::string why;
            switch (DeleteOne(s, ScopeOfPool(), why))
            {
                case Step::Stop:
                    Fail(why);
                    return;
                case Step::Waiting:
                    s.stallMs += diff;
                    if (s.stallMs >= kDeleteStallTimeoutMs)
                        Fail(Trinity::StringFormat("character {} ({}) stayed busy for 120 seconds",
                            s.targets[s.next].name, s.targets[s.next].guid));
                    return;
                case Step::Skipped:
                case Step::Deleted:
                    break;
            }
            s.stallMs = 0;
            if (s.next % kProgressEvery == 0 || s.next >= s.targets.size())
                TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset progress: {}/{} characters deleted",
                    std::uint32_t(s.next), std::uint32_t(s.targets.size()));
            if (s.next >= s.targets.size())
            {
                s.phase = Phase::Verifying;
                s.stallMs = 0;
            }
            return;
        }

        case Phase::Verifying:
        {
            std::vector<std::uint64_t> guids;
            for (Target const& target : s.targets)
                guids.push_back(target.guid);
            std::uint64_t remaining = 0;
            if (!CountCharacters(guids, remaining))
            {
                s.stallMs += diff;
                if (s.stallMs < kVerifyTimeoutMs)
                    return;
                Fail("the characters database did not answer the verification");
                return;
            }
            // Rows left in the registry would mean a deletion that did not finish; the character count is the authority.
            std::size_t rowsLeft = 0;
            for (Character const& row : ReadRows().characters)
                if (row.guid && std::find(guids.begin(), guids.end(), row.guid) != guids.end())
                    ++rowsLeft;
            if (remaining || rowsLeft)
            {
                s.stallMs += diff;
                if (s.stallMs < kVerifyTimeoutMs)
                    return;
                Fail(Trinity::StringFormat("{} of {} pool character(s) and {} registry row(s) remain after deletion",
                    remaining, std::uint32_t(s.targets.size()), std::uint32_t(rowsLeft)));
                return;
            }
            s.stallMs = 0;
            TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset verified: {} of {} pool characters remain; {} character(s) on pool accounts kept (not pool rows)",
                std::uint32_t(remaining), std::uint32_t(s.targets.size()), s.kept);
            if (!s.keptNames.empty())
            {
                std::string names;
                for (std::size_t i = 0; i < s.keptNames.size(); ++i)
                    names += (i ? ", " : "") + s.keptNames[i];
                TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset kept {} character(s) that are not pool rows: {}",
                    s.kept, names);
            }
            s.phase = Phase::Rebuilding;
            return;
        }

        case Phase::Rebuilding:
        {
            if (!ApplyGeneration(s.setting))
            {
                Fail("the applied generation could not be recorded");
                return;
            }
            if (s.setting.mode == PoolResetMode::Once)
                s.applied = s.setting.token;
            if (CharacterSchemaHasCompat(kLineageTable, {}))
                CharacterExecuteGuardedCompat(Trinity::StringFormat("DELETE FROM `{}`", kLineageTable));
            Invalidate();
            s.phase = Phase::Complete;
            s.maintenance = false;
            if (s.setting.mode == PoolResetMode::Once)
                TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool generation '{}' applied; pool rebuild starts now", s.setting.token);
            else
                TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset complete (development 'always' mode); pool rebuild starts now");
            if (Enabled() && CreatePerMinute() > 0)
            {
                CreateStart const start = StartCreate(MaxCharacters());
                if (!start.started)
                    TC_LOG_WARN("tortoisebots", "TortoiseBots: the pool refill did not start: {}", start.message);
            }
            return;
        }

        case Phase::Disabled:
        case Phase::Complete:
        case Phase::Failed:
            return;
    }
}

void Shutdown()
{
    ResetState(Get());
}

bool Active()
{
    Phase const phase = Get().phase;
    return phase == Phase::Planning || phase == Phase::Deleting || phase == Phase::Verifying || phase == Phase::Rebuilding;
}

bool PoolAvailable()
{
    // A refusal before maintenance never pauses the pool; a failure after it keeps maintenance on until a restart.
    return !Get().maintenance;
}

std::string StatusLine()
{
    State& s = Get();
    return Trinity::StringFormat("reset={} phase={} targets={} applied={}",
        SettingText(s.setting), PhaseText(s.phase), std::uint32_t(s.targets.size()), s.applied);
}

} // namespace Reset
} // namespace pool
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace pool
{
namespace Reset
{

void PlanAtStartup(std::string const& /*configuredValue*/)
{
    TC_LOG_INFO("tortoisebots", "TortoiseBots: random pool reset off (the bot engine is not built in: TORTOISEBOTS_SLICE1 is off)");
}

void Update(std::uint32_t /*diff*/) {}
void Shutdown() {}
bool PoolAvailable() { return true; }
bool Active() { return false; }
std::string StatusLine() { return "reset=off phase=disabled targets=0 applied="; }

} // namespace Reset
} // namespace pool
} // namespace TortoiseBots

#endif
