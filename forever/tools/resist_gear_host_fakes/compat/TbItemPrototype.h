// The item mirror's resistances, as the host reads them: the six school fields the swap compares, per item entry.
#pragma once

#include "Define.h"

#include <map>

struct ItemPrototype
{
    int32 HolyRes = 0;
    int32 FireRes = 0;
    int32 NatureRes = 0;
    int32 FrostRes = 0;
    int32 ShadowRes = 0;
    int32 ArcaneRes = 0;
};

namespace tbcompat
{
inline std::map<uint32, ItemPrototype>& ItemProtoTable()
{
    static std::map<uint32, ItemPrototype> table;
    return table;
}

inline ItemPrototype const* ItemProto(uint32 entry)
{
    auto const it = ItemProtoTable().find(entry);
    return it == ItemProtoTable().end() ? nullptr : &it->second;
}
}
