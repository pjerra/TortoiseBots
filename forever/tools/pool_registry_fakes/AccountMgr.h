// The account manager the pool uses (ForeverCore's AccountMgr): CreateAccount/DeleteAccount on the fake auth database, and the
// three reads the registry makes (GetName, GetId, GetEmail) with the core's uppercasing of account names and e-mails. Every
// creation and deletion is recorded in `fakePoolEvents`, in order with the registry's own writes.
#pragma once

#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Define.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

enum class AccountOpResult : uint8
{
    AOR_OK,
    AOR_NAME_TOO_LONG,
    AOR_PASS_TOO_LONG,
    AOR_EMAIL_TOO_LONG,
    AOR_NAME_ALREADY_EXIST,
    AOR_NAME_NOT_EXIST,
    AOR_DB_INTERNAL_ERROR,
    AOR_ACCOUNT_BAD_LINK
};

struct FakeAccount
{
    uint32 id = 0;
    std::string name;
    std::string email;
};

inline std::vector<FakeAccount> fakeAccounts;
inline uint32 fakeNextAccountId = 1;
// The auth database does not answer: every read below answers "nothing there", exactly what the core's AccountMgr says for an
// account that does not exist (its readers return false for both).
inline bool fakeAuthDown = false;
// Only the e-mail read fails (the auth connection drops between two reads of one lookup).
inline bool fakeEmailDown = false;

inline std::string FakeUpper(std::string text)
{
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return text;
}

class AccountMgr
{
public:
    static AccountMgr* instance()
    {
        static AccountMgr manager;
        return &manager;
    }

    AccountOpResult CreateAccount(std::string username, std::string password, std::string email = "", uint32 /*bnetAccountId*/ = 0, uint8 /*bnetIndex*/ = 0)
    {
        username = FakeUpper(std::move(username));
        email = FakeUpper(std::move(email));
        (void)password;
        if (GetId(username))
            return AccountOpResult::AOR_NAME_ALREADY_EXIST;
        fakeAccounts.push_back(FakeAccount{ fakeNextAccountId++, username, email });
        fakePoolEvents.push_back("CreateAccount " + username);
        return AccountOpResult::AOR_OK;
    }

    static AccountOpResult DeleteAccount(uint32 accountId)
    {
        auto const found = std::find_if(fakeAccounts.begin(), fakeAccounts.end(), [&](FakeAccount const& account) { return account.id == accountId; });
        if (found == fakeAccounts.end())
            return AccountOpResult::AOR_NAME_NOT_EXIST;
        fakePoolEvents.push_back("DeleteAccount " + std::to_string(accountId));
        fakeAccounts.erase(found);
        fakeCharacterCache.erase(std::remove_if(fakeCharacterCache.begin(), fakeCharacterCache.end(),
            [&](FakeCachedCharacter const& character) { return character.accountId == accountId; }), fakeCharacterCache.end());
        // The core's DeleteAccount deletes every character of the account (CHAR_SEL_CHARS_BY_ACCOUNT_ID and Player::DeleteFromDB).
        CharacterDatabase.coreCharacters.erase(std::remove_if(CharacterDatabase.coreCharacters.begin(), CharacterDatabase.coreCharacters.end(),
            [&](FakePoolDatabase::CoreCharacterRow const& character) { return character.accountId == accountId; }), CharacterDatabase.coreCharacters.end());
        return AccountOpResult::AOR_OK;
    }

    static uint32 GetId(std::string_view username)
    {
        if (fakeAuthDown)
            return 0;
        std::string const upper = FakeUpper(std::string(username));
        for (FakeAccount const& account : fakeAccounts)
            if (account.name == upper)
                return account.id;
        return 0;
    }

    static bool GetName(uint32 accountId, std::string& name)
    {
        if (fakeAuthDown)
            return false;
        for (FakeAccount const& account : fakeAccounts)
            if (account.id == accountId)
            {
                name = account.name;
                return true;
            }
        return false;
    }

    static bool GetEmail(uint32 accountId, std::string& email)
    {
        if (fakeAuthDown || fakeEmailDown)
            return false;
        for (FakeAccount const& account : fakeAccounts)
            if (account.id == accountId)
            {
                email = account.email;
                return true;
            }
        return false;
    }

    static uint32 GetCharactersCount(uint32 accountId)
    {
        uint32 count = 0;
        for (FakeCachedCharacter const& character : fakeCharacterCache)
            if (character.accountId == accountId)
                ++count;
        return count;
    }
};

#define sAccountMgr AccountMgr::instance()
