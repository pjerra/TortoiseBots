// The core surface TbPoolReset.cpp uses on the world side: the target's session and the character deletion. The fake
// keeps the deletion honest: it removes the core's `characters` row exactly as Player::DeleteFromDB does, and the
// guild_member rows through Guild::DeleteMember, and records the guid so a test can assert "zero deletes".
#pragma once

#include "DatabaseEnv.h"
#include "Define.h"
#include "ObjectGuid.h"
#include "WorldSession.h"

#include <algorithm>
#include <set>
#include <vector>

inline std::set<uint64> fakeConnectedPlayers;
inline std::vector<uint64> fakeDeletes;

class Player
{
public:
    static void DeleteFromDB(ObjectGuid guid, uint32 /*accountId*/, bool /*updateRealmChars*/, bool /*deleteFinally*/)
    {
        uint64 const counter = guid.GetCounter();
        fakeDeletes.push_back(counter);
        std::vector<FakePoolDatabase::CoreCharacterRow>& rows = CharacterDatabase.coreCharacters;
        rows.erase(std::remove_if(rows.begin(), rows.end(), [counter](FakePoolDatabase::CoreCharacterRow const& row)
        {
            return row.guid == counter;
        }), rows.end());
        std::vector<FakePoolDatabase::GuildMemberRow>& members = CharacterDatabase.guildMembers;
        members.erase(std::remove_if(members.begin(), members.end(), [counter](FakePoolDatabase::GuildMemberRow const& row)
        {
            return row.guid == counter;
        }), members.end());
        fakeConnectedPlayers.erase(counter);
    }
};
