// BG1 (BATTLEGROUNDS-SPEC.md, decision D3), see host/bg/TbBgMapData.h. Boot only: one pass over the game object spawn store.
#include "host/bg/TbBgMapData.h"

#include <algorithm>
#include <array>
#include <string>

#include "BattlegroundMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"

#include "ai/playerbot/policy/bg/BgIds.h"

namespace TortoiseBots
{
namespace bg
{
namespace
{

std::array<BgMapInfo, 2>& Store()
{
    static std::array<BgMapInfo, 2> maps{};
    return maps;
}

bool& Loaded()
{
    static bool loaded = false;
    return loaded;
}

void PointFrom(Position const& position, BgPoint& out)
{
    out.x = position.GetPositionX();
    out.y = position.GetPositionY();
    out.z = position.GetPositionZ();
}

// The start location of one team, from the template's original (world_safe_locs) rows.
bool StartFrom(BattlegroundTemplate const* templ, int team, BgPoint& out)
{
    WorldSafeLocsEntry const* safe = templ && templ->StartLocation[team] ? templ->StartLocation[team] : nullptr;
    if (!safe)
        return false;
    PointFrom(safe->Loc, out);
    return true;
}

} // namespace

void LoadBgMapData()
{
    auto& maps = Store();
    maps = {};

    maps[0].kind = BgMapKind::Wsg;
    maps[0].mapId = kWsgMapId;
    maps[0].listId = kWsgListId;
    maps[0].name = "wsg";
    maps[1].kind = BgMapKind::Ab;
    maps[1].mapId = kAbMapId;
    maps[1].listId = kAbListId;
    maps[1].name = "ab";

    // One pass over the spawn store: the smallest spawn id per wanted (map, entry) wins, so a duplicated row resolves the same way
    // on every boot.
    std::array<std::uint64_t, 2> flagSpawn{};
    std::array<std::uint64_t, 5> nodeSpawn{};
    for (auto const& [spawnId, data] : sObjectMgr->GetAllGameObjectData())
    {
        if (data.mapId == kWsgMapId)
        {
            if (data.id == kWsgSilverwingFlag && (!flagSpawn[0] || spawnId < flagSpawn[0]))
            {
                flagSpawn[0] = spawnId;
                PointFrom(data.spawnPoint, maps[0].flags[0]);
            }
            else if (data.id == kWsgWarsongFlag && (!flagSpawn[1] || spawnId < flagSpawn[1]))
            {
                flagSpawn[1] = spawnId;
                PointFrom(data.spawnPoint, maps[0].flags[1]);
            }
        }
        else if (data.mapId == kAbMapId)
        {
            for (std::uint32_t i = 0; i < kAbNodeBannerCount; ++i)
            {
                if (data.id != kAbNodeBanners[i] || (nodeSpawn[i] && spawnId >= nodeSpawn[i]))
                    continue;
                nodeSpawn[i] = spawnId;
                PointFrom(data.spawnPoint, maps[1].nodes[i]);
                break;
            }
        }
    }
    for (std::uint64_t const spawn : nodeSpawn)
        if (spawn)
            ++maps[1].nodesLoaded;

    for (BgMapInfo& map : maps)
    {
        BattlegroundTemplate const* templ = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BattlegroundTypeId(map.listId));
        if (!templ)
            map.offWhy = "no battleground_template row";
        else if (templ->MapIDs.empty())
            map.offWhy = "the template has no map in BattlemasterListXMap";
        else if (!StartFrom(templ, TEAM_ALLIANCE, map.start[0]) || !StartFrom(templ, TEAM_HORDE, map.start[1]))
            map.offWhy = "no start location in world_safe_locs";

        if (map.offWhy.empty() && map.kind == BgMapKind::Wsg)
        {
            if (!flagSpawn[0] || !flagSpawn[1])
                map.offWhy = "a flag stand spawn is missing on map 489";
        }
        else if (map.offWhy.empty() && map.kind == BgMapKind::Ab && map.nodesLoaded != kAbNodeBannerCount)
        {
            map.offWhy = "a node banner spawn is missing on map 529";
        }

        map.enabled = map.offWhy.empty();
        if (map.enabled)
        {
            if (map.kind == BgMapKind::Wsg)
                TC_LOG_INFO("tortoisebots", "TortoiseBots bg: map {} (489, list 2) ready: stands {:.1f},{:.1f},{:.1f} and {:.1f},{:.1f},{:.1f}",
                    map.name, map.flags[0].x, map.flags[0].y, map.flags[0].z, map.flags[1].x, map.flags[1].y, map.flags[1].z);
            else
                TC_LOG_INFO("tortoisebots", "TortoiseBots bg: map {} (529, list 3) ready: {} node banners", map.name, map.nodesLoaded);
        }
        else
            TC_LOG_ERROR("tortoisebots", "TortoiseBots bg: map {} off: {}", map.name, map.offWhy);
    }
    Loaded() = true;
}

BgMapInfo const* BgMapByList(std::uint32_t listId)
{
    for (BgMapInfo const& map : Store())
        if (map.listId == listId)
            return &map;
    return nullptr;
}

BgMapInfo const* BgMapByMapId(std::uint32_t mapId)
{
    for (BgMapInfo const& map : Store())
        if (map.mapId == mapId)
            return &map;
    return nullptr;
}

char const* BgMapName(std::uint32_t listId)
{
    BgMapInfo const* map = BgMapByList(listId);
    return map ? map->name : "";
}

bool BgMapDataLoaded()
{
    return Loaded();
}

} // namespace bg
} // namespace TortoiseBots
