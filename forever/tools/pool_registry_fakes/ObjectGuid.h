// ObjectGuid for the standalone pool test: the counter is all the pool uses (the character cache's answers and the guid a
// created character is stored under). `Create<HighGuid::Player>` is the core's factory for a counter read from the characters
// database.
#pragma once

#include "Define.h"

enum class HighGuid : uint8
{
    Player,
};

class ObjectGuid
{
public:
    ObjectGuid() = default;
    explicit ObjectGuid(uint64 counter) : _counter(counter) {}

    template <HighGuid Kind>
    static ObjectGuid Create(uint64 counter)
    {
        static_assert(Kind == HighGuid::Player, "the pool's guids are characters");
        return ObjectGuid(counter);
    }

    bool IsEmpty() const { return !_counter; }
    uint64 GetCounter() const { return _counter; }

private:
    uint64 _counter = 0;
};
