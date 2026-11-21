// The pool registry's decisions (world slice W0, fix round of round W0-A of FOREVER-W0-W4-SPEC.md): is a registry row
// still about the account the module created, and what does a creation that stopped halfway need?
//
// The registry rows live in the characters database (forever/src/host/pool/TbPoolRegistry.cpp). A row is not ownership by
// itself: a characters-database restore, a manual repair or an auth id that was deleted and reused can leave a row naming
// an id that now belongs to somebody else's account. A row is trusted only when all of these hold:
//   - the account id still resolves to a name (AccountMgr::GetName),
//   - that name is the row's username, with the pool's naming prefix (`TBPOOL<nnnn>`),
//   - the name still resolves back to that id (AccountMgr::GetId: the name was not reused by a newer account),
//   - the account carries the row's creation stamp. The stamp is the random token written as the account's e-mail when the
//     module creates it (AccountMgr::CreateAccount takes one and AccountMgr::GetEmail reads it back; CreateAccount keeps no
//     other field the module may read), so an id reused by a real account - even one renamed to a pool name - does not match.
// Anything else is stale: the module marks the row and never acts on that account or its characters.
//
// A creation that stopped halfway is decided from the core's own `characters` table (never the character cache): finished when
// the character is there on the claim's account, `failed` when the database says it was never made, untouched when the database
// does not answer. The reconciliation deletes nothing; the clean-up is `.tb pool prune` (an explicit, re-verified GM command).
//
// Pure (std only): ai/playerbot/policy/world/PoolRegistryPolicy.h is the module's own decision code, tested by
// forever/tools/test_pool_registry_policy.cpp and used by the registry and the creation below.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace world
{

// TortoiseBots.Pool.MaxCharacters, the hard ceiling on the pool's characters (ready or being created). A request beyond it
// is refused; the per-command count only bounds one job, not the table.
inline constexpr std::uint32_t DefaultMaxPoolCharacters = 50;

// A registry row's state. `creating` is written before the irreversible step; only `ready` rows are used. `failed` is the
// reconciliation's verdict on a creation the database says never happened: never acted on again, and the only state
// `.tb pool prune` cleans (nothing else removes a row).
enum class PoolRowState : std::uint8_t
{
    Creating,
    Ready,
    Stale,
    Failed,
};

inline char const* PoolRowStateText(PoolRowState state)
{
    switch (state)
    {
        case PoolRowState::Ready: return "ready";
        case PoolRowState::Stale: return "stale";
        case PoolRowState::Failed: return "failed";
        case PoolRowState::Creating: break;
    }
    return "creating";
}

// An unknown text is never read as ready: it keeps the row out of use (do less).
inline PoolRowState PoolRowStateFromText(std::string const& text)
{
    if (text == "ready")
        return PoolRowState::Ready;
    if (text == "stale")
        return PoolRowState::Stale;
    if (text == "failed")
        return PoolRowState::Failed;
    return PoolRowState::Creating;
}

// The core uppercases account names and e-mails on creation (Utf8ToUpperOnlyLatin, AccountMgr.cpp); a row stores what the
// module wrote, so both sides are compared in that form.
inline std::string NormalizePoolAccountName(std::string name)
{
    for (char& c : name)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return name;
}

// `TBPOOL<nnnn>`, the pool's account name for a number (four digits, so it stays inside the core's 16-character name and
// reads back as a pool name).
inline std::string PoolAccountName(std::uint32_t number)
{
    std::string digits = std::to_string(number);
    if (digits.size() < 4)
        digits.insert(digits.begin(), 4 - digits.size(), '0');
    return "TBPOOL" + digits;
}

// The pool's own naming prefix: `TBPOOL` and exactly four digits.
inline bool IsPoolAccountName(std::string const& name)
{
    std::string const upper = NormalizePoolAccountName(name);
    if (upper.size() != 10 || upper.compare(0, 6, "TBPOOL") != 0)
        return false;
    for (std::size_t i = 6; i < upper.size(); ++i)
        if (upper[i] < '0' || upper[i] > '9')
            return false;
    return true;
}

// `.tb create <account> ...` makes a character on a named account: only a pool account, or a test account that
// `TortoiseBots.Create.TestAccounts` (a comma or space separated list) names, may take one. Any other account is somebody's.
inline bool CreateAccountAllowed(std::string const& account, std::string const& testAccounts)
{
    if (IsPoolAccountName(account))
        return true;
    std::string const wanted = NormalizePoolAccountName(account);
    if (wanted.empty())
        return false;
    std::string word;
    for (std::size_t i = 0; i <= testAccounts.size(); ++i)
    {
        char const c = i < testAccounts.size() ? testAccounts[i] : ',';
        if (c == ',' || c == ' ' || c == ';' || c == '\t')
        {
            if (!word.empty() && NormalizePoolAccountName(word) == wanted)
                return true;
            word.clear();
        }
        else
            word += c;
    }
    return false;
}

// The creation stamp as the module writes it: sixteen uppercase hex digits.
inline bool IsPoolStamp(std::string const& stamp)
{
    if (stamp.size() != 16)
        return false;
    for (char const c : stamp)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

struct PoolAccountClaim
{
    std::uint32_t accountId = 0;
    std::string username;
    std::string stamp;
};

struct PoolAccountFacts
{
    bool found = false;           // AccountMgr::GetName answered a non-empty name for the id
    std::string name;             // that name
    std::uint32_t idOfName = 0;   // AccountMgr::GetId(the row's username)
    std::string email;            // AccountMgr::GetEmail(the id)
};

enum class PoolAccountCheck : std::uint8_t
{
    Trusted,
    NoAccount,        // the id (or the row's) does not resolve: nothing there to act on
    NameOutsidePool,  // the row's username is not a pool name: not ours by construction
    NameChanged,      // the id now answers a different name: deleted and reused, or renamed
    NameReused,       // the username now belongs to a different id: deleted and re-created
    StampMissing,     // the row has no stamp (a row from before the stamp existed, or a broken one)
    StampChanged,     // the account's stamp is not the row's: somebody else's account
};

inline PoolAccountCheck CheckPoolAccount(PoolAccountClaim const& claim, PoolAccountFacts const& facts)
{
    if (!claim.accountId || !facts.found || facts.name.empty())
        return PoolAccountCheck::NoAccount;
    if (!IsPoolAccountName(claim.username))
        return PoolAccountCheck::NameOutsidePool;
    if (NormalizePoolAccountName(facts.name) != NormalizePoolAccountName(claim.username))
        return PoolAccountCheck::NameChanged;
    if (facts.idOfName != claim.accountId)
        return PoolAccountCheck::NameReused;
    if (claim.stamp.empty())
        return PoolAccountCheck::StampMissing;
    if (NormalizePoolAccountName(facts.email) != NormalizePoolAccountName(claim.stamp))
        return PoolAccountCheck::StampChanged;
    return PoolAccountCheck::Trusted;
}

inline char const* PoolAccountCheckText(PoolAccountCheck check)
{
    switch (check)
    {
        case PoolAccountCheck::Trusted: return "its account is the pool's";
        case PoolAccountCheck::NoAccount: return "its account is not in the auth database";
        case PoolAccountCheck::NameOutsidePool: return "its username does not carry the pool prefix";
        case PoolAccountCheck::NameChanged: return "its account id now names another account";
        case PoolAccountCheck::NameReused: return "its username now belongs to another account id";
        case PoolAccountCheck::StampMissing: return "it carries no creation stamp";
        case PoolAccountCheck::StampChanged: return "its account does not carry its creation stamp";
    }
    return "it is not the pool's";
}

// What the core's own `characters` table says about the character a claim names. This is a database read, never the
// character cache: a cache miss is not an absence, and nothing is removed or finished on one.
enum class PoolCharacterPresence : std::uint8_t
{
    Unanswered,   // the characters database did not answer: the claim stays `creating` and is tried again
    Absent,       // it answered: no character of that name
    OnAccount,    // it answered: the character exists on the claim's account
    Elsewhere,    // it exists on another account (or the claim's account is not the pool's): never touch it
};

enum class PoolCharacterAction : std::uint8_t
{
    Finish,      // the database has the character on the account: the row becomes ready
    MarkFailed,  // the database answered: the character was never made; the row waits for `.tb pool prune`
    MarkStale,   // the name belongs to somebody else, or the account is not the pool's: never touch it
    Retain,      // no answer: the row stays `creating`, nothing is guessed
};

// Reconciliation of a character claim. It only ever finishes a row or marks it: deleting an account or a character on a
// claim's word is what let a transient cache miss take an account and every character on it, so nothing here deletes.
inline PoolCharacterAction DecidePoolCharacter(PoolAccountCheck account, PoolCharacterPresence presence)
{
    if (presence == PoolCharacterPresence::Unanswered)
        return PoolCharacterAction::Retain;
    if (account == PoolAccountCheck::Trusted)
    {
        if (presence == PoolCharacterPresence::OnAccount)
            return PoolCharacterAction::Finish;
        if (presence == PoolCharacterPresence::Absent)
            return PoolCharacterAction::MarkFailed;
        return PoolCharacterAction::MarkStale;
    }
    // Not the pool's account: nothing is adopted. A name that exists is somebody else's; one that does not is dead.
    return presence == PoolCharacterPresence::Absent ? PoolCharacterAction::MarkFailed : PoolCharacterAction::MarkStale;
}

enum class PoolAccountAction : std::uint8_t
{
    Finish,      // a character of it is ready: the account row becomes ready
    MarkFailed,  // the pool's account and no ready character: failed, kept for `.tb pool prune`
    MarkStale,   // not the pool's account: mark the row and never touch the account
};

// Reconciliation of an account claim: finish it or mark it, never delete (the account and its characters stay; only an
// explicit `.tb pool prune confirm`, which re-verifies against the database, removes one).
inline PoolAccountAction DecidePoolAccount(PoolAccountCheck account, bool hasReadyCharacter)
{
    if (account != PoolAccountCheck::Trusted)
        return PoolAccountAction::MarkStale;
    return hasReadyCharacter ? PoolAccountAction::Finish : PoolAccountAction::MarkFailed;
}

// What `.tb pool prune` may remove, from facts it re-reads every run: the account row is the pool's (username prefix and
// creation stamp checked against the account), nothing of it is ready, the characters database answered its character
// list, no character is online, and every character the database lists is named by a pool character row of that account.
// Anything unknown or unexplained is kept.
struct PoolAccountPruneFacts
{
    bool poolAccount = false;          // CheckPoolAccount(row) == Trusted
    bool inUse = false;                // a ready row of the account (its own or a character's)
    bool answered = false;             // the characters database answered the account's character list
    std::uint32_t characters = 0;      // characters the database lists for the account
    std::uint32_t poolCharacters = 0;  // of those, the ones a pool character row of that account names
    bool anyOnline = false;
};

enum class PoolPruneAction : std::uint8_t
{
    RemoveAccount,  // `.tb pool prune confirm`: the account and its characters are the pool's, and nothing else
    KeepAccount,    // one fact is missing or unexplained: nothing is removed
};

inline PoolPruneAction DecideAccountPrune(PoolAccountPruneFacts const& facts)
{
    if (!facts.poolAccount || !facts.answered || facts.inUse || facts.anyOnline)
        return PoolPruneAction::KeepAccount;
    if (facts.poolCharacters != facts.characters)
        return PoolPruneAction::KeepAccount;
    return PoolPruneAction::RemoveAccount;
}

// A failed character row whose account is in use (a live pool account) may lose the row itself, and only when the pool's
// account is proven, the database answered, and it has no character of that name.
inline bool CanDropFailedCharacterRow(bool accountTrusted, bool answered, bool absent)
{
    return accountTrusted && answered && absent;
}

// One creation request against the total ceiling: what may be made, not what was asked. 0 means refuse (the request would
// add nothing, and the caller says why).
inline std::uint32_t AllowedPoolCharacters(std::uint32_t requested, std::uint32_t present, std::uint32_t cap)
{
    if (present >= cap)
        return 0;
    return std::min(requested, cap - present);
}

// How many pool accounts the cap allows: the accounts the cap's characters fill at `perAccount` each (a cap of 50 at 10 is five
// accounts). An account holding no character still counts, so a creation that keeps failing cannot keep making them.
inline std::uint32_t MaxPoolAccounts(std::uint32_t capCharacters, std::uint32_t perAccount)
{
    if (!capCharacters || !perAccount)
        return 0;
    return (capCharacters + perAccount - 1) / perAccount;
}

inline bool PoolHasRoomForCharacter(std::uint32_t charactersOnAccount, std::uint32_t perAccount)
{
    return charactersOnAccount < perAccount;
}

} // namespace world
} // namespace TortoiseBots
