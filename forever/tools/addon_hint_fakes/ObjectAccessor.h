// FindConnectedPlayer with the test's world: counts the lookups (the test uses the count to prove an entry left
// the pending set) and answers only for a guid in the fake online map.
#pragma once

#include "FakeWorld.h"
#include "ObjectGuid.h"
#include "Player.h"

class ObjectAccessor
{
public:
    static Player* FindConnectedPlayer(ObjectGuid const& guid)
    {
        ++fakeworld::connectedLookups;
        auto const found = fakeworld::online.find(guid.GetCounter());
        return found == fakeworld::online.end() ? nullptr : found->second;
    }
};
