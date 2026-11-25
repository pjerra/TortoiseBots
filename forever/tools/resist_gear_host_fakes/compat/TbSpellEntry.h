// The spell mirror's school, as BuildIndex compares a data row with its evidence spell.
#pragma once

#include "Define.h"

#include <map>

struct SpellEntry
{
    uint32 School = 0;
};

namespace tbcompat
{
inline std::map<uint32, SpellEntry>& SpellProtoTable()
{
    static std::map<uint32, SpellEntry> table;
    return table;
}

inline uint32 MaxSpellId()
{
    return SpellProtoTable().empty() ? 0 : SpellProtoTable().rbegin()->first;
}

inline SpellEntry const* SpellProto(uint32 id)
{
    auto const it = SpellProtoTable().find(id);
    return it == SpellProtoTable().end() ? nullptr : &it->second;
}
}
