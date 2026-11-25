// The character cache surface the `.tb addonhint` command uses.
#pragma once

#include "ObjectGuid.h"

#include <map>
#include <string>

class CharacterCache
{
public:
    ObjectGuid GetCharacterGuidByName(std::string const& name) const
    {
        auto const found = names.find(name);
        return found == names.end() ? ObjectGuid() : ObjectGuid::Create<HighGuid::Player>(found->second);
    }

    bool GetCharacterNameByGuid(ObjectGuid const& guid, std::string& name) const
    {
        for (auto const& entry : names)
        {
            if (entry.second == guid.GetCounter())
            {
                name = entry.first;
                return true;
            }
        }
        return false;
    }

    std::map<std::string, uint64> names;
};

inline CharacterCache fakeCharacterCache;
inline CharacterCache* sCharacterCache = &fakeCharacterCache;
