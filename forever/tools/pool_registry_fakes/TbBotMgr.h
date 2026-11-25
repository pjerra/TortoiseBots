// The one BotMgr call the pool makes: `CreateCharacter`, which in the fake prepares the core's character row and records the
// call in `fakePoolEvents`, between the pool's claim write and its `ready` write. Like the core, it does NOT touch the
// character cache: the saves are queued, and the row reaches the characters database only when the commit lands. The pool then
// confirms it by reading that table back, and only then publishes the cache entry.
#pragma once

#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <set>
#include <string>
#include <vector>

struct FakeBotMgrState
{
    int createCalls = 0;
    bool failCreate = false;
    std::string failReason = "the core refused to create it";
    bool loginCommitHeld = false; // the character row landed, but the login commit and OnPlayerCreate have not run yet
    bool commitLands = true;     // the queued saves reach the characters database; false = the commit is still in flight
};

inline FakeBotMgrState fakeBotMgrState;
inline uint64 fakeNextCharacterGuid = 1;
// `failCreate` refuses every creation; `fakeFailNextCreates` refuses that many and then creates (one bad composition, then a
// good one).
inline int fakeFailNextCreates = 0;

struct FakePendingCharacter
{
    std::string name;
    uint32 accountId = 0;
    uint64 guid = 0;
};

// Creations whose saves are queued: a later tick's commit moves them into the characters database.
inline std::vector<FakePendingCharacter> fakePendingCreations;
// Names whose character row is in the database but whose creation is not complete (BotMgr::CreationInFlight).
inline std::vector<std::string> fakeLoginHeld;
// The reset's drain: the guids BotMgr still holds as bots, and the logouts it asked for.
inline std::set<uint64> fakeBots;
inline std::vector<uint64> fakeLogouts;

inline void fakeCommitPendingCharacters()
{
    for (FakePendingCharacter const& character : fakePendingCreations)
        CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ character.name, character.guid, character.accountId, false });
    fakePendingCreations.clear();
}

class BotMgr
{
public:
    static BotMgr& Instance()
    {
        static BotMgr manager;
        return manager;
    }

    bool CreationInFlight(std::string const& name) const
    {
        for (std::string const& held : fakeLoginHeld)
            if (held == name)
                return true;
        return false;
    }

    bool IsBot(ObjectGuid guid) const { return fakeBots.count(guid.GetCounter()) != 0; }

    bool Logout(ObjectGuid guid, char const* /*reason*/)
    {
        fakeLogouts.push_back(guid.GetCounter());
        return fakeBots.erase(guid.GetCounter()) != 0;
    }

    std::string CreateCharacter(uint32 accountId, std::string const& name, uint8 /*race*/, uint8 /*playerClass*/, uint8 /*gender*/)
    {
        ++fakeBotMgrState.createCalls;
        fakePoolEvents.push_back("CreateCharacter " + name);
        if (fakeBotMgrState.failCreate)
            return fakeBotMgrState.failReason;
        if (fakeFailNextCreates > 0)
        {
            --fakeFailNextCreates;
            return fakeBotMgrState.failReason;
        }
        for (FakePoolDatabase::CoreCharacterRow const& row : CharacterDatabase.coreCharacters)
            if (row.name == name)
                return "that name is taken";
        for (FakePendingCharacter const& row : fakePendingCreations)
            if (row.name == name)
                return "that name is taken";
        uint64 const guid = fakeNextCharacterGuid++;
        if (fakeBotMgrState.loginCommitHeld)
            fakeLoginHeld.push_back(name);
        if (fakeBotMgrState.commitLands)
            CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ name, guid, accountId, false });
        else
            fakePendingCreations.push_back(FakePendingCharacter{ name, accountId, guid });
        return {};
    }
};
