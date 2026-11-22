// Out-of-line parts of the R4V compat pair (TbCompat_V.h): the live-AreaTrigger scan.
// Compiled with the module, WITHOUT the cmangos shim in front (like TbCompat_C.cpp):
// this file only sees the core's own headers, so the AreaTrigger entity can be named here.
#include "compat/botpch_forever.h"

#include "compat/TbCompat_V.h"
// The shim above renames GetCasterGuid to GetCasterGUID (the aura spelling);
// AreaTrigger itself spells it GetCasterGuid, so that rename is undone here.
#undef GetCasterGuid

#include <list>
#include <vector>

namespace
{
// Live area triggers within `range` of a unit. WorldObjectListSearcher calls the
// check for every container type, so it takes a WorldObject* and filters by type id.
class AreaTriggersInRangeCheck
{
public:
    AreaTriggersInRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
    WorldObject const& GetFocusObject() const { return *i_obj; }
    bool operator()(WorldObject* u)
    {
        return u && u->GetTypeId() == TYPEID_AREATRIGGER && u->IsInWorld() && i_obj->IsWithinDistInMap(u, i_range);
    }

private:
    WorldObject const* i_obj;
    float i_range;
};

void FillAreaTriggerInfo(AreaTrigger* at, AreaTriggerHazardInfo& info)
{
    info.x = at->GetPositionX();
    info.y = at->GetPositionY();
    info.z = at->GetPositionZ();
    info.radius = at->GetMaxSearchRadius();
    info.spellId = at->GetSpellId();
    info.guid = at->GetGUID();
    info.casterGuid = at->GetCasterGuid();
    info.caster = at->GetCaster();
}

// Live area triggers within `range` of a map point (one bounded visit; the grid
// is never loaded for it).
class AreaTriggersNearPointCheck
{
public:
    AreaTriggersNearPointCheck(uint32 mapId, float x, float y, float z, float range)
        : mapId(mapId), x(x), y(y), z(z), range(range) {}
    bool operator()(WorldObject* u)
    {
        if (!u || u->GetTypeId() != TYPEID_AREATRIGGER || u->GetMapId() != mapId || !u->IsInWorld())
            return false;
        float const dx = u->GetPositionX() - x;
        float const dy = u->GetPositionY() - y;
        float const dz = u->GetPositionZ() - z;
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

private:
    uint32 mapId;
    float x, y, z, range;
};
}

void CollectAreaTriggerInfosCompat(WorldObject const* center, float range, std::vector<AreaTriggerHazardInfo>& out)
{
    out.clear();
    if (!center)
        return;
    AreaTriggersInRangeCheck check(center, range);
    std::list<WorldObject*> targets;
    VisitGridObjectsCompat(center, targets, check, range);
    for (WorldObject* wo : targets)
    {
        AreaTriggerHazardInfo info;
        FillAreaTriggerInfo(static_cast<AreaTrigger*>(wo), info);
        out.push_back(info);
    }
}

bool GetAreaTriggerInfoCompat(Player* bot, ObjectGuid guid, AreaTriggerHazardInfo& info)
{
    if (!bot || guid.IsEmpty())
        return false;
    Map* map = bot->GetMap();
    if (!map)
        return false;
    AreaTrigger* at = map->GetAreaTrigger(guid);
    if (!at || !at->IsInWorld())
        return false;
    FillAreaTriggerInfo(at, info);
    return true;
}
void CollectAreaTriggerInfosAtCompat(float x, float y, float z, Map* map, float range,
    std::vector<AreaTriggerHazardInfo>& out)
{
    out.clear();
    if (!map)
        return;
    std::list<WorldObject*> targets;
    AreaTriggersNearPointCheck check(map->GetId(), x, y, z, range);
    VisitGridObjectsAtCompat(x, y, map, targets, check, range);
    for (WorldObject* wo : targets)
    {
        AreaTriggerHazardInfo info;
        FillAreaTriggerInfo(static_cast<AreaTrigger*>(wo), info);
        out.push_back(info);
    }
}
