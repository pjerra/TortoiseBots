// W0 (FOREVER-W0-W4-SPEC.md, round W0-A, fix round): creation of the pool's characters, see TbPoolCreate.h. Everything runs on the
// world thread: the schema guard, the registry writes and the core's own character creation. One character per tick at most, paced
// by TortoiseBots.Pool.CreatePerMinute, so `.tb pool create 200` is a job of about seven minutes and never a burst.
//
// The protocol is write-ahead: the claim row goes in first, the irreversible step second, the row's `ready` third. Every step is
// idempotent - an account is adopted when its name and e-mail carry the claim's stamp, a character is adopted when the cache has
// the claim's name on the claim's account - so a crash, a failed write or a restart never leaves an unmanaged account or
// character, and reconciliation only ever removes what the claim proves the pool made.
#include "host/pool/TbPoolCreate.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>

#ifdef TORTOISEBOTS_SLICE1

#include "host/pool/TbPoolRegistry.h"
#include "host/pool/TbPoolReset.h"
#include "TbBotMgr.h"

#include "ai/playerbot/policy/world/PoolCompositionPolicy.h"
#include "ai/playerbot/policy/world/PoolRegistryPolicy.h"
#include "compat/TbCompat_WL0.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Random.h"
#include "StringFormat.h"
#include "runtime/BotNameService.h"

namespace TortoiseBots
{
namespace pool
{
namespace
{
// Long enough for the core's own rules (MAX_PASS_STR, AccountMgr.cpp), and the alphabet leaves out the characters that are hard to
// tell apart. The password is only ever handed to `AccountMgr::CreateAccount`.
constexpr std::size_t kPasswordLength = 16;
char const* const kPasswordAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
char const* const kStampAlphabet = "0123456789ABCDEF";

// 9999 accounts is far beyond any pool the spec asks for, and keeps `TBPOOL<nnnn>` inside the core's 16-character account name.
constexpr std::uint32_t kHighestAccount = 9999;

// How many names one tick may skip when the auth database already has an account of them (somebody else's, never touched).
constexpr std::uint32_t kAccountNameTries = 64;

// A queued creation is confirmed by reading the core's characters table back: every kConfirmCheckMs, and no longer than
// kConfirmTimeoutMs before the row is marked failed (a commit that never lands is not a creation).
constexpr std::uint32_t kConfirmCheckMs = 500;
constexpr std::uint32_t kConfirmTimeoutMs = 30000;

enum class Step
{
    Made,
    Queued,     // the core's saves are queued: the row becomes ready when the characters database has it, on a later tick
    Retry,      // this tick's candidate was not usable; the request is not consumed
    Failed,     // the job stops, the reason is in State::stop
};

// A creation whose cross-database saves were queued and whose characters row has not been read back yet.
struct Pending
{
    bool active = false;
    std::string name;
    std::string username;           // the account row's username, for its `ready` write
    std::uint32_t accountId = 0;
    std::uint8_t race = 0;
    std::uint8_t klass = 0;
    std::uint8_t gender = 0;
    std::int64_t created = 0;
    std::uint32_t checkMs = 0;
    std::uint32_t elapsedMs = 0;
};

struct State
{
    std::uint32_t remaining = 0;
    std::uint32_t created = 0;
    std::uint32_t skipped = 0;
    std::uint32_t intervalMs = 2000;
    std::uint32_t nextMs = 0;
    std::uint32_t serial = 0;                  // the composition sequence: the pool's character count, continued across restarts
    std::uint32_t nextAccount = 0;             // 0 = one past the highest registered number
    std::uint32_t nameTries = 0;
    std::uint32_t cap = 0;                     // TortoiseBots.Pool.MaxCharacters as it was when the job started: a reload never changes a running job
    std::uint32_t jobAccountId = 0;            // the account this job is filling (a reload of the registry's cache must not make it open another)
    std::string jobAccountName;
    Pending pending;
    std::string stop;
};

State& Get()
{
    static State state;
    return state;
}

void Finish(State& s, std::string const& why)
{
    TC_LOG_INFO("tortoisebots", "TortoiseBots: pool create stopped after {} character(s) ({} already-existing name(s) skipped): {}", s.created, s.skipped, why);
    s.remaining = 0;
    if (s.stop.empty())
        s.stop = why;
}

std::string RandomPassword()
{
    std::string password;
    password.reserve(kPasswordLength);
    std::size_t const alphabet = std::char_traits<char>::length(kPasswordAlphabet);
    for (std::size_t i = 0; i < kPasswordLength; ++i)
        password.push_back(kPasswordAlphabet[urand(0, std::uint32_t(alphabet - 1))]);
    return password;
}

// The creation stamp: sixteen uppercase hex digits, written as the account's e-mail so `AccountMgr::GetEmail` can read it back
// (CreateAccount uppercases it; this is already uppercase).
std::string RandomStamp()
{
    std::string stamp;
    stamp.reserve(16);
    for (int i = 0; i < 16; i += 4)
    {
        std::uint32_t const value = urand(0, 0xffff);
        for (int shift = 12; shift >= 0; shift -= 4)
            stamp.push_back(kStampAlphabet[(value >> shift) & 0xf]);
    }
    return stamp;
}

// `TBPOOL<number>`, and the number after it for the next attempt.
std::string AccountNameFor(std::uint32_t& number)
{
    if (!number)
    {
        std::string const next = NextAccountName();        // TBPOOL0001 when the registry is empty
        number = std::uint32_t(std::strtoul(next.c_str() + 6, nullptr, 10));
    }
    return world::PoolAccountName(number);
}

struct ChosenAccount
{
    std::uint32_t id = 0;
    std::string username;
};

bool AccountCarriesStamp(std::uint32_t accountId, std::string const& username, std::string const& stamp)
{
    if (!accountId)
        return false;
    std::string name;
    if (!AccountMgr::GetName(accountId, name) || world::NormalizePoolAccountName(name) != world::NormalizePoolAccountName(username))
        return false;
    std::string email;
    return AccountMgr::GetEmail(accountId, email) && world::NormalizePoolAccountName(email) == world::NormalizePoolAccountName(stamp);
}

// The account this character goes on, in this order: the account this job is already filling while the database says it has room;
// a registry account with room; a failed pool account with nothing on it (proven the pool's); and only then a new one, never
// beyond what the cap's characters need. Empty `id` means Step::Retry or Step::Failed, which `s.stop` then explains. The caller
// verifies the answer against the database before any character goes on it (VerifyAccount): nothing here is trusted by itself.
ChosenAccount AccountFor(State& s, Step& step)
{
    auto const chosen = [&s](std::uint32_t id, std::string const& username)
    {
        s.jobAccountId = id;
        s.jobAccountName = username;
        return ChosenAccount{ id, username };
    };

    if (s.jobAccountId)
    {
        std::uint32_t present = 0;
        if (CharactersOnAccountNow(s.jobAccountId, present) && world::PoolHasRoomForCharacter(present, kCharactersPerAccount))
            return ChosenAccount{ s.jobAccountId, s.jobAccountName };
        s.jobAccountId = 0;
        s.jobAccountName.clear();
    }
    if (std::uint32_t const room = AccountWithRoom())
    {
        std::vector<Account> const accounts = Accounts();
        for (Account const& account : accounts)
            if (account.id == room)
                return chosen(account.id, account.username);
    }
    Account reusable;
    if (FindReusableAccount(reusable))
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool account {} (#{}) is empty and proven the pool's: it is used again", reusable.username, reusable.id);
        return chosen(reusable.id, reusable.username);
    }
    std::uint32_t const accountLimit = world::MaxPoolAccounts(s.cap, kCharactersPerAccount);
    std::uint32_t const accountRows = PoolAccountRows();
    if (accountRows >= accountLimit)
    {
        step = Step::Failed;
        s.stop = Trinity::StringFormat("the pool already has {} account row(s), the most its cap of {} character(s) needs ({}): no account is added (`.tb pool prune` removes failed ones)",
            accountRows, s.cap, accountLimit);
        return {};
    }
    for (std::uint32_t attempt = 0; attempt < kAccountNameTries; ++attempt)
    {
        if (s.nextAccount > kHighestAccount)
        {
            step = Step::Failed;
            s.stop = "the pool's account range TBPOOL0001-9999 is used up";
            return {};
        }
        std::uint32_t number = s.nextAccount;
        std::string const username = AccountNameFor(number);
        s.nextAccount = number + 1;

        // The claim first: from here on a crash leaves a row the reconciliation can finish or remove.
        Account claim;
        claim.username = username;
        claim.stamp = RandomStamp();
        claim.state = world::PoolRowState::Creating;
        claim.created = std::int64_t(std::time(nullptr));
        if (!StoreAccount(claim))
        {
            step = Step::Failed;
            s.stop = "the pool account row " + username + " could not be written";
            return {};
        }

        std::string password = RandomPassword();
        AccountOpResult const result = sAccountMgr->CreateAccount(username, password, claim.stamp);
        std::fill(password.begin(), password.end(), '\0');       // the random password is not kept, logged or stored

        if (result == AccountOpResult::AOR_OK)
        {
            std::uint32_t const accountId = AccountMgr::GetId(username);
            if (!accountId)
            {
                // The claim (state creating, no id) stays: the reconciliation resolves the name and its stamp at the next start.
                step = Step::Failed;
                s.stop = "the account " + username + " was created but its id cannot be read";
                return {};
            }
            claim.id = accountId;
            if (!UpdateAccount(claim))
            {
                step = Step::Failed;
                s.stop = "the account " + username + " was created but not registered (see the error line)";
                return {};
            }
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool account {} (#{}) created", username, accountId);
            return chosen(accountId, username);
        }

        // Not OK. The account may still be ours (a write that landed but answered an error, or a retry after a crash): the
        // claim's stamp says so, and only that is adopted. Any other account of that name is somebody else's and is left alone.
        if (result == AccountOpResult::AOR_DB_INTERNAL_ERROR || result == AccountOpResult::AOR_NAME_ALREADY_EXIST)
        {
            std::uint32_t const accountId = AccountMgr::GetId(username);
            if (AccountCarriesStamp(accountId, username, claim.stamp))
            {
                claim.id = accountId;
                if (!UpdateAccount(claim))
                {
                    step = Step::Failed;
                    s.stop = "the account " + username + " was created but not registered (see the error line)";
                    return {};
                }
                TC_LOG_INFO("tortoisebots", "TortoiseBots: pool account {} (#{}) adopted (its creation had already landed)", username, accountId);
                return chosen(accountId, username);
            }
        }

        // Nothing of the pool's exists under this name: drop the claim only, never the account.
        DeleteAccountRow(username);
        if (result == AccountOpResult::AOR_NAME_ALREADY_EXIST)
        {
            ++s.skipped;
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool account {} already exists outside the pool registry: skipped", username);
            step = Step::Retry;
            continue;
        }

        // U4 of the spec: a core that cannot create an account (no Battle.net account, say) stops creation with the result code.
        step = Step::Failed;
        s.stop = Trinity::StringFormat("AccountMgr::CreateAccount({}) failed with result {}", username, std::uint32_t(result));
        return {};
    }
    step = Step::Failed;
    s.stop = "no usable pool account name came up";
    return {};
}

// The character cache follows the database, never the other way round: an entry is added only after the characters table
// answered with the row, so a cache lookup may miss (and in this file a miss never means "absent").
void PublishCharacter(Character const& character)
{
    sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(character.guid), character.accountId, character.name, character.gender, character.race,
        character.klass, 1, false);
}

// A character the database has on the chosen account: the row becomes ready, the account row follows, and the cache learns
// of it now.
bool FinishCharacter(Character claim, std::uint64_t guid, std::string const& username, std::string& why)
{
    claim.guid = guid;
    claim.state = world::PoolRowState::Ready;
    if (!UpdateCharacter(claim))
    {
        why = claim.name + " exists but could not be registered in the pool (see the error line)";
        return false;
    }
    PublishCharacter(claim);
    Account account;
    if (FindAccountRow(username, account) && account.state != world::PoolRowState::Ready)
    {
        account.state = world::PoolRowState::Ready;
        UpdateAccount(account);
    }
    return true;
}

// Marks a claim the database proved dead (or that stopped being usable): never deleted, only marked, with one log line.
bool MarkCharacter(Character claim, world::PoolRowState state)
{
    claim.state = state;
    return UpdateCharacter(claim);
}

Step MakeOne(State& s)
{
    std::uint32_t const present = PoolCharacters();       // a database count, every tick: never a list's size
    if (present == kPoolCountUnknown)
    {
        s.stop = "the characters database did not answer how many characters the pool holds";
        return Step::Failed;
    }
    if (present >= s.cap)
    {
        s.stop = Trinity::StringFormat("the pool's cap of {} character(s) is reached (TortoiseBots.Pool.MaxCharacters)", s.cap);
        return Step::Failed;
    }

    world::PoolComposition composition;
    std::uint8_t gender = 0;
    if (CensusComposition())
    {
        world::CensusPoolComposition const pick = world::CensusCompositionAt(s.serial, urand(0, 0xffff), urand(0, 0xffff), urand(0, 999));
        composition.race = pick.race;
        composition.klass = pick.klass;
        gender = pick.gender;
    }
    else
    {
        composition = world::CompositionAt(s.serial, urand(0, 0xffff));
        gender = world::GenderAt(urand(0, 1));
    }
    std::string const name = PickBotCharacterName(composition.race, composition.klass, gender);
    // The name is settled before any account is made: the core's own name rules, then the characters table.
    if (name.empty() || !CheckPlayerNameCompat(name))
    {
        // The name service already tried its pools and the fallback: one more tick, then the job stops rather than spin.
        if (++s.nameTries >= 5)
        {
            s.stop = "no usable character name came up";
            return Step::Failed;
        }
        return Step::Retry;
    }
    s.nameTries = 0;

    // The characters database, never the cache: whether the name exists is a fact only the table knows, and a table that
    // does not answer is not a free name either (a few ticks, then the job stops rather than spin).
    bool exists = false;
    std::uint64_t knownGuid = 0;
    std::uint32_t knownAccount = 0;
    if (!CharacterOwner(name, exists, knownGuid, knownAccount))
    {
        if (++s.nameTries >= 5)
        {
            s.stop = "the characters database did not answer whether " + name + " exists";
            return Step::Failed;
        }
        return Step::Retry;
    }
    s.nameTries = 0;

    ChosenAccount chosen;
    Character claimed;                                  // the pool's own claim of an existing name (adoption only)
    if (exists)
    {
        // The name is a character of the database. It is adopted only when THIS pool's own `creating` row for that exact name and
        // account claimed it before the character existed (a crash left it); anything else, a character on a pool account that
        // nobody claimed included, is never touched: it is logged and the name skipped, and no claim is ever written for it.
        if (!FindCharacterRow(name, claimed) || claimed.state != world::PoolRowState::Creating || claimed.accountId != knownAccount)
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool name {} is already a character (#{} on account #{}) that no unfinished pool claim of that name and account holds: it is not adopted and not touched, the name is skipped",
                name, knownGuid, knownAccount);
            if (++s.skipped >= 5)
            {
                s.stop = name + " already exists and no pool claim holds it (not adopted)";
                return Step::Failed;
            }
            return Step::Retry;
        }
        std::vector<Account> const accounts = Accounts();
        for (Account const& account : accounts)
            if (account.id == knownAccount)
                chosen = ChosenAccount{ account.id, account.username };
        if (!chosen.id)
        {
            s.stop = name + " already exists on another account";
            return Step::Failed;
        }
    }
    else
    {
        Step step = Step::Made;
        chosen = AccountFor(s, step);
        if (!chosen.id)
            return step;
    }

    // Before EVERY character: the account is re-verified by database reads (the registry row and the account manager), never
    // by the cached list. An account that was renamed, deleted or reused since it was listed gets no character.
    std::string why;
    if (VerifyAccount(chosen.id, chosen.username, why) != world::PoolAccountCheck::Trusted)
    {
        ForgetAccount(chosen.id);
        s.jobAccountId = 0;
        s.jobAccountName.clear();
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool account {} (#{}) is no longer the pool's ({}): no character is made on it", chosen.username, chosen.id, why);
        s.stop = Trinity::StringFormat("pool account {} (#{}) is no longer the pool's ({}): no character was made on it", chosen.username, chosen.id, why);
        return Step::Failed;
    }

    // The character's claim, before the core's creation: a crash after the character exists leaves this row to adopt. An existing
    // character already has its claim (checked above) and gets no new one.
    Character pending;
    if (exists)
        pending = claimed;
    else
    {
        pending.name = name;
        pending.accountId = chosen.id;
        pending.race = composition.race;
        pending.klass = composition.klass;
        pending.gender = gender;
        pending.state = world::PoolRowState::Creating;
        pending.created = std::int64_t(std::time(nullptr));
        if (!StoreCharacter(pending))
        {
            s.stop = name + " could not be reserved in the pool registry";
            return Step::Failed;
        }
    }

    if (exists)
    {
        // The database has it on the claim's account (a crash between its saves and its `ready` write): finish the row,
        // never make the character twice.
        std::string why;
        if (!FinishCharacter(pending, knownGuid, chosen.username, why))
        {
            s.stop = why;
            return Step::Failed;
        }
        ++s.serial;
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool character {} (#{}) adopted: the characters database already held it on account #{}",
            name, knownGuid, chosen.id);
        return Step::Made;
    }

    std::string const error = BotMgr::Instance().CreateCharacter(chosen.id, name, composition.race, composition.klass, gender);
    if (!error.empty())
    {
        // The core made no character: drop the pool's row only, and stop (the spec: never retry into a loop).
        DeleteCharacterRow(name);
        s.stop = "the core refused to create " + name + ": " + error;
        return Step::Failed;
    }

    // Queued, not created: the saves go asynchronously to the character and login databases. The row stays `creating` and
    // the character cache learns nothing until the characters table answers with the row (ConfirmPending).
    s.pending = Pending{ true, name, chosen.username, chosen.id, composition.race, composition.klass, gender, pending.created, 0, 0 };
    return Step::Queued;
}

} // namespace

void Reconcile()
{
    if (!Enabled())
        return;
    Rows rows = ReadRows();
    bool const anyClaim = std::any_of(rows.accounts.begin(), rows.accounts.end(), [](Account const& row) { return row.state == world::PoolRowState::Creating; })
        || std::any_of(rows.characters.begin(), rows.characters.end(), [](Character const& row) { return row.state == world::PoolRowState::Creating; });
    if (!anyClaim)
    {
        // No unfinished creation: only the ready rows' identity is checked (and a stale one marked), once.
        (void)Accounts();
        (void)Characters();
        return;
    }

    std::size_t finished = 0, failed = 0, stale = 0, retained = 0;
    // 1. The character claims. The characters database decides, and a row is only finished or marked: no account and no
    //    character is ever deleted here, and a cache miss is not an absence (a creation can only be undone by `.tb pool prune`).
    for (Character const& claim : rows.characters)
    {
        if (claim.state != world::PoolRowState::Creating)
            continue;
        Account const* account = nullptr;
        for (Account const& row : rows.accounts)
            if (row.id && row.id == claim.accountId)
                account = &row;
        world::PoolAccountCheck const check = account ? CheckAccount(*account) : world::PoolAccountCheck::NoAccount;

        bool found = false;
        std::uint64_t guid = 0;
        std::uint32_t owner = 0;
        world::PoolCharacterPresence presence = world::PoolCharacterPresence::Unanswered;
        if (CharacterOwner(claim.name, found, guid, owner))
        {
            if (!found)
                presence = world::PoolCharacterPresence::Absent;
            else
                presence = owner == claim.accountId ? world::PoolCharacterPresence::OnAccount : world::PoolCharacterPresence::Elsewhere;
        }

        // The account manager gave no answer for the claim's account row: that may be a failed lookup, and a failed lookup never
        // marks a row stale. A character that exists is left for the next start; one the database says does not exist is
        // still marked failed (never stale), which `.tb pool prune` re-verifies.
        if (account && check == world::PoolAccountCheck::NoAccount && (presence == world::PoolCharacterPresence::OnAccount || presence == world::PoolCharacterPresence::Elsewhere))
        {
            ++retained;
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: the account of {} cannot be checked now; the claim stays for the next start", claim.name);
            continue;
        }

        switch (world::DecidePoolCharacter(check, presence))
        {
            case world::PoolCharacterAction::Finish:
            {
                std::string why;
                if (FinishCharacter(claim, guid, account->username, why))
                {
                    ++finished;
                    TC_LOG_INFO("tortoisebots", "TortoiseBots: pool reconciliation finished {} (#{}): the characters database holds it on account #{}",
                        claim.name, guid, claim.accountId);
                }
                else
                    TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: {}", why);
                break;
            }
            case world::PoolCharacterAction::MarkFailed:
                if (MarkCharacter(claim, world::PoolRowState::Failed))
                    ++failed;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: {} was never created (the characters database has no such character); its row is failed and waits for `.tb pool prune`",
                    claim.name);
                break;
            case world::PoolCharacterAction::MarkStale:
                if (MarkCharacter(claim, world::PoolRowState::Stale))
                    ++stale;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: {} is not the pool's ({}); its row is marked stale and nothing is touched",
                    claim.name, world::PoolAccountCheckText(check));
                break;
            case world::PoolCharacterAction::Retain:
                ++retained;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: the characters database did not answer for {}; the claim stays for the next start", claim.name);
                break;
        }
    }

    // 2. The account claims: the id may have been written or not; the name and stamp decide whether the account is the pool's.
    rows = ReadRows();
    for (Account claim : rows.accounts)
    {
        if (claim.state != world::PoolRowState::Creating)
            continue;
        if (!claim.id)
        {
            // A crash between CreateAccount and the id write: resolve the name; the stamp says whether it is ours.
            std::uint32_t const accountId = AccountMgr::GetId(claim.username);
            if (!accountId)
            {
                // Nothing was created under the claim. The row is marked, never deleted: `.tb pool prune` removes it.
                claim.state = world::PoolRowState::Failed;
                if (UpdateAccount(claim))
                    ++failed;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: no account was created for {}; its row is failed and waits for `.tb pool prune`",
                    claim.username);
                continue;
            }
            claim.id = accountId;
            if (!UpdateAccount(claim))
            {
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: the claim {} could not take its account id; it stays for the next start", claim.username);
                continue;
            }
        }
        world::PoolAccountCheck const check = CheckAccount(claim);
        if (check == world::PoolAccountCheck::NoAccount)
        {
            // The account manager did not answer for the id: leave the claim to the next start, never guess.
            ++retained;
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: the pool claim {} (#{}) cannot be checked now; it stays for the next start", claim.username, claim.id);
            continue;
        }
        bool hasReadyCharacter = false;
        bool hasClaimedCharacter = false;
        for (Character const& row : rows.characters)
        {
            if (row.accountId != claim.id)
                continue;
            if (row.state == world::PoolRowState::Ready)
                hasReadyCharacter = true;
            if (row.state == world::PoolRowState::Creating)
                hasClaimedCharacter = true;
        }
        if (hasClaimedCharacter)
        {
            // The character half was not decided (no answer from the characters database): the account waits with it.
            ++retained;
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: pool account {} (#{}) still has an unresolved character claim; it stays for the next start",
                claim.username, claim.id);
            continue;
        }

        switch (world::DecidePoolAccount(check, hasReadyCharacter))
        {
            case world::PoolAccountAction::Finish:
                claim.state = world::PoolRowState::Ready;
                if (UpdateAccount(claim))
                    ++finished;
                break;
            case world::PoolAccountAction::MarkFailed:
                claim.state = world::PoolRowState::Failed;
                if (UpdateAccount(claim))
                    ++failed;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: pool account {} (#{}) has no ready character; its row is failed and waits for `.tb pool prune`",
                    claim.username, claim.id);
                break;
            case world::PoolAccountAction::MarkStale:
                claim.state = world::PoolRowState::Stale;
                if (UpdateAccount(claim))
                    ++stale;
                TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool reconciliation: pool account {} (#{}) is not the pool's ({}); its row is marked stale and the account is never touched",
                    claim.username, claim.id, world::PoolAccountCheckText(check));
                break;
        }
    }

    Invalidate();
    if (finished || failed || stale || retained)
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool reconciliation: {} finished, {} failed (kept for `.tb pool prune`), {} stale, {} left for the next start",
            finished, failed, stale, retained);
}

CreateStart StartCreate(std::uint32_t count)
{
    CreateStart start;
    if (!Reset::PoolAvailable())
    {
        start.message = "a pool reset is in progress or failed: nothing is created until it finishes";
        return start;
    }
    if (!Enabled())
    {
        start.message = "the pool is off: TortoiseBots.Pool.Enable = 0, or its tables are missing (see the error line)";
        return start;
    }
    std::uint32_t const rate = CreatePerMinute();
    if (!rate)
    {
        start.message = "TortoiseBots.Pool.CreatePerMinute is 0: nothing may be created";
        return start;
    }
    State& s = Get();
    if (s.remaining)
    {
        start.message = Trinity::StringFormat("a creation is already running: {} character(s) left", s.remaining);
        return start;
    }
    if (!count)
    {
        start.message = "name how many characters to create: `.tb pool create <n>`";
        return start;
    }

    // Nothing new starts on top of a creation that stopped halfway.
    Reconcile();

    std::uint32_t const cap = MaxCharacters();
    std::uint32_t const present = PoolCharacters();
    if (present == kPoolCountUnknown)
    {
        start.message = "the characters database did not answer how many characters the pool holds: nothing was created";
        return start;
    }
    std::uint32_t const allowed = world::AllowedPoolCharacters(count, present, cap);
    if (!allowed)
    {
        start.message = Trinity::StringFormat("the pool holds {} character(s), its cap is {} (TortoiseBots.Pool.MaxCharacters): nothing was created", present, cap);
        return start;
    }

    std::uint32_t const asked = count;
    count = allowed;
    s.remaining = count;
    s.created = 0;
    s.skipped = 0;
    s.stop.clear();
    s.nameTries = 0;
    s.intervalMs = std::max<std::uint32_t>(1, 60000 / rate);
    s.nextMs = 0;
    s.serial = std::uint32_t(Characters().size());
    s.nextAccount = 0;
    s.cap = cap;                      // the job keeps the cap (and the rate above) it started with
    s.jobAccountId = 0;
    s.jobAccountName.clear();
    start.started = true;
    if (count != asked)
        start.message = Trinity::StringFormat("the pool holds {} of its cap of {} character(s): creating the {} that fit (of {} asked), at {} per minute", present, cap, count, asked, rate);
    else
        start.message = Trinity::StringFormat("creating {} pool character(s) at {} per minute", count, rate);
    TC_LOG_INFO("tortoisebots", "TortoiseBots: pool create started: {} character(s) at {} per minute (pool {} of cap {})", count, rate, present, cap);
    return start;
}

bool Creating()
{
    return Get().remaining != 0;
}

std::string StopReason()
{
    return Get().stop;
}

namespace
{
// A short listing of character names for the prune's line: the first eight, then the count of the rest.
std::string ListedNames(std::vector<std::string> const& names)
{
    std::size_t const shown = std::min<std::size_t>(names.size(), 8);
    std::string text;
    for (std::size_t i = 0; i < shown; ++i)
    {
        if (i)
            text += ", ";
        text += names[i];
    }
    if (names.size() > shown)
        text += Trinity::StringFormat(" and {} more", names.size() - shown);
    return text;
}
} // namespace

PruneReport Prune(bool confirm)
{
    PruneReport report;
    if (!Enabled())
    {
        report.lines.push_back("the pool is off: TortoiseBots.Pool.Enable = 0, or its tables are missing (see the error line)");
        return report;
    }
    report.enabled = true;
    if (Creating())
    {
        // A running job's claim is in flux: prune waits rather than race it.
        report.lines.push_back("a creation is running: wait for it to finish before pruning");
        return report;
    }

    // An unfinished creation is finished (or marked) first: a claim whose character has landed is a ready row by now, and what is
    // still a `creating` row after it is kept below.
    Reconcile();
    Rows rows = ReadRows();
    bool const anyFailed = std::any_of(rows.accounts.begin(), rows.accounts.end(), [](Account const& row) { return row.state == world::PoolRowState::Failed; })
        || std::any_of(rows.characters.begin(), rows.characters.end(), [](Character const& row) { return row.state == world::PoolRowState::Failed; });
    if (!anyFailed)
    {
        report.lines.push_back("no failed pool row: nothing to prune");
        return report;
    }
    report.applied = confirm;

    // 1. The failed account rows: re-verify the account (name, id, stamp), the absence of any ready row, and every
    //    character the database lists for it. Anything unproven is listed with its reason and kept.
    for (Account const& row : rows.accounts)
    {
        if (row.state != world::PoolRowState::Failed)
            continue;
        Account claim = row;
        std::uint32_t const accountId = claim.id ? claim.id : AccountMgr::GetId(claim.username);
        std::string const head = claim.username + (accountId ? Trinity::StringFormat(" (#{})", accountId) : std::string());
        if (!accountId)
        {
            // No account answers that name: the row is the only thing left (a character cannot belong to a missing account).
            report.lines.push_back(head + ": no account exists under that name; " + (confirm ? "the row was removed" : "would remove the row"));
            ++report.rows;
            if (confirm)
            {
                if (row.id)
                    for (Character const& character : rows.characters)
                        if (character.accountId == row.id)
                            DeleteCharacterRow(character.name);
                DeleteAccountRow(claim.username);
            }
            continue;
        }
        claim.id = accountId;
        world::PoolAccountCheck const check = CheckAccount(claim);
        if (check != world::PoolAccountCheck::Trusted)
        {
            report.lines.push_back(head + ": kept, " + world::PoolAccountCheckText(check));
            continue;
        }

        bool inUse = false;
        for (Account const& other : rows.accounts)
            if (other.id == accountId && other.state == world::PoolRowState::Ready)
                inUse = true;
        for (Character const& character : rows.characters)
            if (character.accountId == accountId && (character.state == world::PoolRowState::Ready || character.state == world::PoolRowState::Creating))
                inUse = true;
        if (inUse)
        {
            report.lines.push_back(head + ": kept, the account is in use (a ready or unfinished row)");
            continue;
        }

        std::vector<OwnedCharacter> characters;
        if (!AccountCharacters(accountId, characters))
        {
            report.lines.push_back(head + ": kept, the characters database did not answer (try again later)");
            continue;
        }
        world::PoolAccountPruneFacts facts;
        facts.poolAccount = true;
        facts.answered = true;
        facts.characters = std::uint32_t(characters.size());
        std::vector<std::string> names;
        std::string stranger;
        for (OwnedCharacter const& character : characters)
        {
            names.push_back(character.name);
            Character registryRow;
            if (FindCharacterRow(character.name, registryRow) && registryRow.accountId == accountId)
                ++facts.poolCharacters;
            else if (stranger.empty())
                stranger = character.name;
            if (character.online)
                facts.anyOnline = true;
        }
        if (world::DecideAccountPrune(facts) == world::PoolPruneAction::KeepAccount)
        {
            std::string why;
            if (facts.anyOnline)
                why = "a character of it is online";
            else if (!stranger.empty())
                why = "`" + stranger + "` on it is not a pool character";
            else
                why = "it does not match what prune may remove";
            report.lines.push_back(head + ": kept, " + why);
            continue;
        }

        std::string const what = confirm ? "removed" : "would remove";
        std::string const line = head + ": " + what + " the account and " + std::to_string(characters.size()) + " character(s)" +
            (names.empty() ? std::string() : " (" + ListedNames(names) + ")");
        if (confirm)
        {
            AccountOpResult const result = AccountMgr::DeleteAccount(accountId);
            if (result != AccountOpResult::AOR_OK)
            {
                report.lines.push_back(line + Trinity::StringFormat(": AccountMgr::DeleteAccount answered {}", std::uint32_t(result)));
                continue;
            }
            for (Character const& character : rows.characters)
                if (character.accountId == accountId)
                    DeleteCharacterRow(character.name);
            DeleteAccountRow(claim.username);
        }
        report.lines.push_back(line);
        ++report.accounts;
        report.characters += std::uint32_t(characters.size());
    }

    // 2. Failed character rows of an account that stays (a live pool account): only the row goes, and only when the
    //    database has no character of that name. A failed account's rows went with its own line above.
    for (Character const& row : rows.characters)
    {
        if (row.state != world::PoolRowState::Failed)
            continue;
        Account const* account = nullptr;
        for (Account const& other : rows.accounts)
            if (other.id == row.accountId)
                account = &other;
        if (!account)
        {
            report.lines.push_back(row.name + ": kept, no pool account row names its account");
            continue;
        }
        if (account->state == world::PoolRowState::Failed)
            continue;
        if (account->state != world::PoolRowState::Ready)
        {
            report.lines.push_back(row.name + ": kept, its account row is " + world::PoolRowStateText(account->state) + " (the reconciliation's)");
            continue;
        }
        bool found = false;
        std::uint64_t guid = 0;
        std::uint32_t owner = 0;
        bool const answered = CharacterOwner(row.name, found, guid, owner);
        bool const trusted = CheckAccount(*account) == world::PoolAccountCheck::Trusted;
        if (!world::CanDropFailedCharacterRow(trusted, answered, answered && !found))
        {
            std::string why;
            if (!trusted)
                why = "its account is not the pool's";
            else if (!answered)
                why = "the characters database did not answer";
            else
                why = "the characters database holds the name";
            report.lines.push_back(row.name + ": kept, " + why);
            continue;
        }
        report.lines.push_back(row.name + ": " + (confirm ? "removed" : "would remove") + " the row (no such character exists)");
        ++report.rows;
        if (confirm)
            DeleteCharacterRow(row.name);
    }

    if (confirm)
        Invalidate();
    TC_LOG_INFO("tortoisebots", "TortoiseBots: pool prune ({}): {} account(s) and {} character(s) {}, {} row(s) {}",
        confirm ? "confirm" : "dry run", report.accounts, report.characters, confirm ? "removed" : "listed", report.rows, confirm ? "removed" : "listed");
    return report;
}

namespace
{
// Finishes the creation whose saves were queued, from the characters database: the row (and the account's, and the
// character cache) becomes ready only when the table has it on that account. A commit that does not land within
// kConfirmTimeoutMs leaves a row that is marked failed, never ready. True = this tick made nothing else.
bool ConfirmPending(State& s, std::uint32_t diff)
{
    Pending& p = s.pending;
    if (!p.active)
        return false;
    p.elapsedMs += diff;
    p.checkMs += diff;
    if (p.checkMs < kConfirmCheckMs)
        return true;
    p.checkMs = 0;

    bool found = false;
    std::uint64_t guid = 0;
    std::uint32_t owner = 0;
    bool const answered = CharacterOwner(p.name, found, guid, owner);

    if (answered && found && owner == p.accountId)
    {
        // The character row landed; the login side (the realm's character count) and the creation hooks follow it in BotMgr. The
        // row becomes ready only after both, so a stop in between never leaves a ready pool character without them.
        if (BotMgr::Instance().CreationInFlight(p.name))
            return true;
        Character claim;
        if (!FindCharacterRow(p.name, claim))
        {
            s.stop = p.name + " was created but its pool row is gone (see the error line)";
            p.active = false;
            Finish(s, s.stop);
            return true;
        }
        std::string why;
        if (!FinishCharacter(claim, guid, p.username, why))
        {
            p.active = false;
            Finish(s, why);
            return true;
        }
        ++s.serial;
        ++s.created;
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool character {} (#{}) created: race {} class {} gender {} on account #{}",
            p.name, guid, uint32(p.race), uint32(p.klass), uint32(p.gender), p.accountId);
        p.active = false;
        if (--s.remaining == 0)
            Finish(s, "the request is complete");
        return true;
    }

    if (answered && found)
    {
        // The name landed on another account (somebody made it meanwhile): never ours, and the job stops.
        Character claim;
        if (FindCharacterRow(p.name, claim))
            MarkCharacter(claim, world::PoolRowState::Stale);
        p.active = false;
        Finish(s, p.name + " was created on another account (#" + std::to_string(owner) + ")");
        return true;
    }

    if (answered && !found && p.elapsedMs >= kConfirmTimeoutMs)
    {
        // The saves were queued and the characters database never got the row: it is not a creation. The row is marked,
        // never deleted: `.tb pool prune` removes it once (and only if) the database still has no such character.
        Character claim;
        if (FindCharacterRow(p.name, claim))
            MarkCharacter(claim, world::PoolRowState::Failed);
        p.active = false;
        Finish(s, Trinity::StringFormat("{} did not reach the characters database within {} s; its row is failed and kept for `.tb pool prune`",
            p.name, kConfirmTimeoutMs / 1000));
        return true;
    }

    return true;
}
} // namespace

void CreateUpdate(std::uint32_t diff)
{
    State& s = Get();
    if (!s.remaining)
        return;
    if (!Reset::PoolAvailable())
        return;                       // a startup reset owns the pool: the job waits, its claim rows stay as they are
    if (!KeyOn())
    {
        // `.reload config` turned the pool off while the job ran: it stops here. A queued character's row stays `creating`; the
        // reconciliation finishes or marks it at the next start with the pool on.
        s.pending.active = false;
        Finish(s, "the pool was turned off (TortoiseBots.Pool.Enable = 0) while the job ran");
        return;
    }
    if (ConfirmPending(s, diff))
        return;
    if (s.nextMs > diff)
    {
        s.nextMs -= diff;
        return;
    }
    s.nextMs = s.intervalMs;
    switch (MakeOne(s))
    {
        case Step::Made:
            ++s.created;
            if (--s.remaining == 0)
                Finish(s, "the request is complete");
            break;
        case Step::Queued:
            break;       // the claim row counts toward the cap while the database is read back
        case Step::Retry:
            break;
        case Step::Failed:
            Finish(s, s.stop);
            break;
    }
}

} // namespace pool
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace pool
{

void Reconcile() {}

PruneReport Prune(bool /*confirm*/)
{
    PruneReport report;
    report.lines.push_back("the bot engine is not built in (TORTOISEBOTS_SLICE1 is off): there is nothing to prune");
    return report;
}

CreateStart StartCreate(std::uint32_t /*count*/)
{
    CreateStart start;
    start.message = "the bot engine is not built in (TORTOISEBOTS_SLICE1 is off): the pool can create nothing";
    return start;
}

void CreateUpdate(std::uint32_t /*diff*/) {}

bool Creating()
{
    return false;
}

std::string StopReason()
{
    return std::string();
}

} // namespace pool
} // namespace TortoiseBots

#endif
