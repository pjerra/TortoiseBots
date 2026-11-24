// Standalone test of the pool registry's decisions (ai/playerbot/policy/world/PoolRegistryPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_pool_registry_policy.cpp -o /tmp/test_pool_registry_policy && /tmp/test_pool_registry_policy
//
// The defects the rounds fix, one group each:
//  - a row whose account id was deleted and reused by a real account (name, id back-lookup and the creation stamp each
//    have to catch it): the row is stale, never used;
//  - a creation that stopped halfway (account without its character, character without its row, a name that belongs to
//    somebody else): finish it or mark it failed/stale - never delete, and never read an unanswered database as absent;
//  - `.tb pool prune`: only a proven pool account with nothing ready and every character of it the pool's may go;
//  - the total ceiling: a request is bounded by the pool's size, not only by its own count.
#include "ai/playerbot/policy/world/PoolRegistryPolicy.h"

#include <cstdio>
#include <string>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static w::PoolAccountClaim Claim(std::uint32_t id, char const* name, char const* stamp)
{
    w::PoolAccountClaim claim;
    claim.accountId = id;
    claim.username = name;
    claim.stamp = stamp;
    return claim;
}

static w::PoolAccountFacts Facts(char const* name, std::uint32_t idOfName, char const* email)
{
    w::PoolAccountFacts facts;
    facts.found = name != nullptr && *name;
    facts.name = name ? name : "";
    facts.idOfName = idOfName;
    facts.email = email ? email : "";
    return facts;
}

int main()
{
    char const* const stamp = "0123456789ABCDEF";

    // `.tb create` goes only on a pool account or a test account the config names.
    CHECK(w::CreateAccountAllowed("TBPOOL0001", ""));
    CHECK(w::CreateAccountAllowed("tbtest1", "TBTEST1, TBTEST2"));
    CHECK(w::CreateAccountAllowed("TBTEST2", "tbtest1,tbtest2"));
    CHECK(!w::CreateAccountAllowed("SOMEPLAYER", ""));
    CHECK(!w::CreateAccountAllowed("SOMEPLAYER", "TBTEST1,TBTEST2"));
    CHECK(!w::CreateAccountAllowed("TBTEST", "TBTEST1"));
    CHECK(!w::CreateAccountAllowed("", ""));

    // The naming prefix is the pool's, and only it.
    CHECK(w::IsPoolAccountName("TBPOOL0001"));
    CHECK(w::IsPoolAccountName("tbpool9999"));
    CHECK(!w::IsPoolAccountName("TBPOOL001"));
    CHECK(!w::IsPoolAccountName("TBPOOL00001"));
    CHECK(!w::IsPoolAccountName("TBPOOL000A"));
    CHECK(!w::IsPoolAccountName("TBPOOL"));
    CHECK(!w::IsPoolAccountName("PLAYER01"));
    CHECK(w::NormalizePoolAccountName("tbpool0007") == "TBPOOL0007");
    CHECK(w::IsPoolStamp(stamp));
    CHECK(!w::IsPoolStamp("0123456789abcdef"));    // creation uppercases it; a lower-case stamp is not one the module wrote
    CHECK(!w::IsPoolStamp("0123"));
    CHECK(!w::IsPoolStamp(""));

    // The row's state: only `ready` is usable, unknown text is not ready.
    CHECK(w::PoolRowStateFromText("ready") == w::PoolRowState::Ready);
    CHECK(w::PoolRowStateFromText("creating") == w::PoolRowState::Creating);
    CHECK(w::PoolRowStateFromText("stale") == w::PoolRowState::Stale);
    CHECK(w::PoolRowStateFromText("failed") == w::PoolRowState::Failed);
    CHECK(w::PoolRowStateFromText("") == w::PoolRowState::Creating);
    CHECK(w::PoolRowStateFromText("READY") == w::PoolRowState::Creating);
    CHECK(std::string(w::PoolRowStateText(w::PoolRowState::Stale)) == "stale");
    CHECK(std::string(w::PoolRowStateText(w::PoolRowState::Failed)) == "failed");

    // Finding 1: an id the pool created, still ours.
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", stamp), Facts("TBPOOL0001", 3, stamp)) == w::PoolAccountCheck::Trusted);
    // A case difference is the same account (the core uppercases both sides).
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", "0123456789abcdef"), Facts("TBPOOL0001", 3, stamp)) == w::PoolAccountCheck::Trusted);

    // Finding 1: the id was deleted and a real account now has it. The name is not the row's.
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", stamp), Facts("REALPLAYER", 9, "real@example")) == w::PoolAccountCheck::NameChanged);
    // The real account was even renamed to the pool's name: the id and the name match, the stamp does not.
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", stamp), Facts("TBPOOL0001", 3, "real@example")) == w::PoolAccountCheck::StampChanged);
    // The row's username was deleted and a new account took the name: the name resolves to another id.
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", stamp), Facts("TBPOOL0001", 11, stamp)) == w::PoolAccountCheck::NameReused);
    // A row written by the round before the stamp existed has none: stale, not trusted by name alone.
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", ""), Facts("TBPOOL0001", 3, "")) == w::PoolAccountCheck::StampMissing);
    // A row naming a username outside the prefix is not the pool's, whatever the auth database answers.
    CHECK(w::CheckPoolAccount(Claim(3, "PLAYER01", stamp), Facts("PLAYER01", 3, stamp)) == w::PoolAccountCheck::NameOutsidePool);
    // A deleted account (GetName answers nothing) and an unclaimed row are both "nothing there".
    CHECK(w::CheckPoolAccount(Claim(3, "TBPOOL0001", stamp), Facts("", 0, "")) == w::PoolAccountCheck::NoAccount);
    CHECK(w::CheckPoolAccount(Claim(0, "TBPOOL0001", stamp), Facts("TBPOOL0001", 0, stamp)) == w::PoolAccountCheck::NoAccount);
    // Every non-trusted answer has its line for the log.
    for (w::PoolAccountCheck check : { w::PoolAccountCheck::Trusted, w::PoolAccountCheck::NoAccount, w::PoolAccountCheck::NameOutsidePool,
             w::PoolAccountCheck::NameChanged, w::PoolAccountCheck::NameReused, w::PoolAccountCheck::StampMissing, w::PoolAccountCheck::StampChanged })
        CHECK(std::string(w::PoolAccountCheckText(check)).size() > 10);

    // Finding 2: reconciliation of a creation row that stopped halfway. It finishes or marks; it never deletes.
    using A = w::PoolAccountAction;
    using C = w::PoolCharacterAction;
    using P = w::PoolCharacterPresence;
    // The account was made, its character was not: the row is failed and kept for prune, never deleted.
    CHECK(w::DecidePoolAccount(w::PoolAccountCheck::Trusted, false) == A::MarkFailed);
    // The character is there: the row becomes ready.
    CHECK(w::DecidePoolAccount(w::PoolAccountCheck::Trusted, true) == A::Finish);
    // No account, or somebody else's: a row only, and the account is never touched.
    CHECK(w::DecidePoolAccount(w::PoolAccountCheck::NoAccount, false) == A::MarkStale);
    for (w::PoolAccountCheck check : { w::PoolAccountCheck::NameChanged, w::PoolAccountCheck::NameReused, w::PoolAccountCheck::StampChanged,
             w::PoolAccountCheck::NameOutsidePool, w::PoolAccountCheck::StampMissing })
        CHECK(w::DecidePoolAccount(check, false) == A::MarkStale);

    // The character half: finish only a character the database has on our account; a database that does not answer
    // decides nothing; a character that was never made is failed, never dropped and never an account deletion.
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::Trusted, P::OnAccount) == C::Finish);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::Trusted, P::Elsewhere) == C::MarkStale);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::Trusted, P::Absent) == C::MarkFailed);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::Trusted, P::Unanswered) == C::Retain);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::NoAccount, P::Absent) == C::MarkFailed);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::NoAccount, P::OnAccount) == C::MarkStale);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::StampChanged, P::OnAccount) == C::MarkStale);
    CHECK(w::DecidePoolCharacter(w::PoolAccountCheck::StampChanged, P::Unanswered) == C::Retain);

    // `.tb pool prune`: every fact has to hold.
    using w::DecideAccountPrune;
    using Q = w::PoolPruneAction;
    w::PoolAccountPruneFacts facts;
    facts.poolAccount = true;
    facts.answered = true;
    facts.characters = 2;
    facts.poolCharacters = 2;
    CHECK(DecideAccountPrune(facts) == Q::RemoveAccount);
    CHECK(DecideAccountPrune(w::PoolAccountPruneFacts{}) == Q::KeepAccount);
    facts.poolAccount = false;
    CHECK(DecideAccountPrune(facts) == Q::KeepAccount);
    facts.poolAccount = true;
    facts.answered = false;
    CHECK(DecideAccountPrune(facts) == Q::KeepAccount);
    facts.answered = true;
    facts.inUse = true;
    CHECK(DecideAccountPrune(facts) == Q::KeepAccount);
    facts.inUse = false;
    facts.anyOnline = true;
    CHECK(DecideAccountPrune(facts) == Q::KeepAccount);
    facts.anyOnline = false;
    facts.poolCharacters = 1;                       // a character on it that no pool row names
    CHECK(DecideAccountPrune(facts) == Q::KeepAccount);
    facts.poolCharacters = 2;
    CHECK(DecideAccountPrune(facts) == Q::RemoveAccount);
    // A failed row of a live account loses only itself, and only from a trusted account the database answered for.
    CHECK(w::CanDropFailedCharacterRow(true, true, true));
    CHECK(!w::CanDropFailedCharacterRow(false, true, true));
    CHECK(!w::CanDropFailedCharacterRow(true, false, true));
    CHECK(!w::CanDropFailedCharacterRow(true, true, false));

    // Finding 3: the total ceiling.
    using w::AllowedPoolCharacters;
    CHECK(w::DefaultMaxPoolCharacters == 50);
    CHECK(AllowedPoolCharacters(10, 0, 50) == 10);
    CHECK(AllowedPoolCharacters(10, 45, 50) == 5);
    CHECK(AllowedPoolCharacters(1, 50, 50) == 0);
    CHECK(AllowedPoolCharacters(1, 51, 50) == 0);
    CHECK(AllowedPoolCharacters(10, 0, 0) == 0);
    CHECK(AllowedPoolCharacters(0, 10, 50) == 0);
    // The cap bounds the pool's accounts too: a cap of 50 characters at 10 per account is five accounts, 51 is six, 0 is none.
    CHECK(w::MaxPoolAccounts(50, 10) == 5);
    CHECK(w::MaxPoolAccounts(51, 10) == 6);
    CHECK(w::MaxPoolAccounts(1, 10) == 1);
    CHECK(w::MaxPoolAccounts(0, 10) == 0);
    CHECK(w::MaxPoolAccounts(10, 0) == 0);
    // Ten characters per account is the registry's own cap.
    CHECK(w::PoolHasRoomForCharacter(9, 10));
    CHECK(!w::PoolHasRoomForCharacter(10, 10));

    // The pool's placement is one character per account: one session per account means a second character of an account
    // could never be online, so the accounts hold exactly the pool's characters and the cap needs one account per character.
    CHECK(w::kPoolCharactersPerAccount == 1);
    CHECK(w::MaxPoolAccounts(22, w::kPoolCharactersPerAccount) == 22);
    CHECK(w::MaxPoolAccounts(50, w::kPoolCharactersPerAccount) == 50);
    CHECK(w::MaxPoolAccounts(1, w::kPoolCharactersPerAccount) == 1);
    CHECK(w::MaxPoolAccounts(0, w::kPoolCharactersPerAccount) == 0);
    CHECK(w::PoolHasRoomForCharacter(0, w::kPoolCharactersPerAccount));
    CHECK(!w::PoolHasRoomForCharacter(1, w::kPoolCharactersPerAccount));
    CHECK(!w::PoolHasRoomForCharacter(10, w::kPoolCharactersPerAccount));

    if (g_fail)
        std::printf("test_pool_registry_policy: %d checks, %d FAILED\n", g_checks, g_fail);
    else
        std::printf("RESULT: PASS (%d checks)\n", g_checks);

    return g_fail ? 1 : 0;
}
