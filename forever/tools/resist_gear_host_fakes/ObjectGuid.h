// The two guid kinds the resist-gear host names (a character and an item), with the core's own identity: the kind in
// the high bits, the row counter below, so a character guid and an item guid never compare equal.
#pragma once

#include "Define.h"

#include <functional>

namespace HighGuid
{
enum Kind : uint32
{
    Player = 0,
    Item = 6,
};
}

class ObjectGuid
{
public:
    ObjectGuid() = default;

    template <HighGuid::Kind Kind>
    static ObjectGuid Create(uint64 counter)
    {
        return ObjectGuid((uint64(Kind) << 48) | (counter & 0xFFFFFFFFFFFFull));
    }

    bool IsEmpty() const { return _value == 0; }
    uint64 GetRawValue() const { return _value; }
    uint64 GetCounter() const { return _value & 0xFFFFFFFFFFFFull; }

    bool operator==(ObjectGuid const& other) const { return _value == other._value; }
    bool operator!=(ObjectGuid const& other) const { return _value != other._value; }
    bool operator<(ObjectGuid const& other) const { return _value < other._value; }

private:
    explicit ObjectGuid(uint64 value) : _value(value) {}
    uint64 _value = 0;
};

namespace std
{
template <>
struct hash<ObjectGuid>
{
    std::size_t operator()(ObjectGuid const& guid) const { return std::hash<uint64>()(guid.GetRawValue()); }
};
}
