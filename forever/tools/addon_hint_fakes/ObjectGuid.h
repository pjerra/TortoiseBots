// The guid surface the add-on hint uses: Create<HighGuid::Player>(counter), SetRawValue (the hook gives halves),
// GetCounter (the characters.guid), IsEmpty and GetRawValue.
#pragma once

#include "Define.h"

enum class HighGuid : uint32
{
    Player = 1,
};

class ObjectGuid
{
public:
    ObjectGuid() = default;

    template <HighGuid>
    static ObjectGuid Create(uint64 counter)
    {
        ObjectGuid guid;
        guid.counter_ = counter;
        return guid;
    }

    void SetRawValue(uint64 hi, uint64 lo) { counter_ = (hi << 32) | (lo & 0xFFFFFFFFu); }
    uint64 GetCounter() const { return counter_; }
    bool IsEmpty() const { return counter_ == 0; }
    uint64 GetRawValue(uint8 index) const { return index == 0 ? (counter_ & 0xFFFFFFFFu) : (counter_ >> 32); }

    bool operator==(ObjectGuid const& other) const { return counter_ == other.counter_; }
    bool operator!=(ObjectGuid const& other) const { return !(*this == other); }

private:
    uint64 counter_ = 0;
};
