// A group and its member slots, as the resist-gear host reads them (who is in the group, and whether any of them is
// a real player rather than a host bot).
#pragma once

#include "ObjectGuid.h"

#include <vector>

class Group
{
public:
    struct MemberSlot
    {
        ObjectGuid guid;
    };

    std::vector<MemberSlot> const& GetMemberSlots() const { return _slots; }
    void AddMember(ObjectGuid guid) { _slots.push_back(MemberSlot{ guid }); }

private:
    std::vector<MemberSlot> _slots;
};
