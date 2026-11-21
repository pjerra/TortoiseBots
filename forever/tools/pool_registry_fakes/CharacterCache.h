// The character cache as the pool uses it (ForeverCore's CharacterCache): name to guid/account and guid to name, filled by the
// fake BotMgr's creation and emptied by the fake AccountMgr's deletion.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <algorithm>
#include <string>
#include <vector>

struct FakeCachedCharacter
{
    uint64 guid = 0;
    uint32 accountId = 0;
    std::string name;
    uint8 gender = 0;
    uint8 race = 0;
    uint8 klass = 0;
};

inline std::vector<FakeCachedCharacter> fakeCharacterCache;

class CharacterCache
{
public:
    static CharacterCache* instance()
    {
        static CharacterCache cache;
        return &cache;
    }

    ObjectGuid GetCharacterGuidByName(std::string const& name) const
    {
        for (FakeCachedCharacter const& character : fakeCharacterCache)
            if (character.name == name)
                return ObjectGuid(character.guid);
        return ObjectGuid();
    }

    uint32 GetCharacterAccountIdByName(std::string const& name) const
    {
        for (FakeCachedCharacter const& character : fakeCharacterCache)
            if (character.name == name)
                return character.accountId;
        return 0;
    }

    bool GetCharacterNameByGuid(ObjectGuid guid, std::string& name) const
    {
        for (FakeCachedCharacter const& character : fakeCharacterCache)
            if (character.guid == guid.GetCounter())
            {
                name = character.name;
                return true;
            }
        return false;
    }

    void AddCharacterCacheEntry(ObjectGuid guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 /*level*/, bool /*isDeleted*/)
    {
        for (FakeCachedCharacter& character : fakeCharacterCache)
            if (character.guid == guid.GetCounter())
            {
                character = FakeCachedCharacter{ guid.GetCounter(), accountId, name, gender, race, playerClass };
                return;
            }
        fakeCharacterCache.push_back(FakeCachedCharacter{ guid.GetCounter(), accountId, name, gender, race, playerClass });
    }

    void DeleteCharacterCacheEntry(ObjectGuid guid, std::string const& /*name*/)
    {
        fakeCharacterCache.erase(std::remove_if(fakeCharacterCache.begin(), fakeCharacterCache.end(),
            [&](FakeCachedCharacter const& character) { return character.guid == guid.GetCounter(); }), fakeCharacterCache.end());
    }
};

inline CharacterCache* sCharacterCache = CharacterCache::instance();
