#include "playerbot/playerbot.h"
#include "BotAreaTriggers.h"

#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "../../runtime/CommandedGroupQuery.h"

#include <unordered_map>

using namespace ai;

namespace
{
    using BotAreaTriggerIndex = std::unordered_map<uint32, std::vector<BotAreaTriggerShape>>;

    // Built once, on the world thread, from the loaded world; read-only after.
    // Only the audited allowlist (AiPlayerbot.BotAreaTriggers), dungeons only
    // (raid triggers wait until bots are tested in raids).
    BotAreaTriggerIndex const& GetIndex()
    {
        static BotAreaTriggerIndex index;
        static bool built = false;
        if (built)
            return index;
        built = true;

        uint32 count = 0;
        for (auto const& itr : sObjectMgr.GetAreaTriggersMap())
        {
            AreaTriggerEntry const& at = itr.second;
            MapEntry const* map = sMapStorage.LookupEntry<MapEntry>(at.mapid);
            if (!IsBotFireableAreaTrigger(IsListedAreaTrigger(sPlayerbotAIConfig.botAreaTriggerIds, at.id),
                                          sScriptMgr.GetAreaTriggerScriptId(at.id) != 0,
                                          map && map->IsNonRaidDungeon(),
                                          sObjectMgr.GetAreaTriggerTeleport(at.id) != nullptr,
                                          sObjectMgr.IsTavernAreaTrigger(at.id),
                                          sObjectMgr.GetBattlegroundEntranceTrigger(at.id) != nullptr))
                continue;

            BotAreaTriggerShape shape;
            shape.id = at.id;
            shape.x = at.x;
            shape.y = at.y;
            shape.z = at.z;
            shape.radius = at.radius;
            shape.boxX = at.box_x;
            shape.boxY = at.box_y;
            shape.boxZ = at.box_z;
            shape.boxOrientation = at.box_orientation;
            index[at.mapid].push_back(shape);
            ++count;
        }
        sLog.outString("TortoiseBots: %u listed area triggers on %u dungeon maps can fire for bots.",
                       count, uint32(index.size()));
        return index;
    }
}

void ai::UpdateBotAreaTriggers(Player* bot, BotAreaTriggerState& state)
{
    // Only a living bot in a group, inside a dungeon (not a raid), standing still or
    // walking (not on a taxi, not mid-teleport), the way a client would send it.
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsTaxiFlying() ||
        !bot->IsAlive() || !bot->GetGroup() || !bot->GetMap() || !bot->GetMap()->IsNonRaidDungeon())
    {
        state.Clear();
        return;
    }

    BotAreaTriggerIndex const& index = GetIndex();
    auto const itr = index.find(bot->GetMapId());
    if (itr == index.end())
    {
        state.Clear();
        return;
    }

    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float const z = bot->GetPositionZ();
    std::vector<uint32_t> insideNow;
    std::vector<uint32_t> nearNow;
    for (BotAreaTriggerShape const& shape : itr->second)
    {
        if (IsInsideAreaTriggerShape(shape, x, y, z, 0.0f))
            insideNow.push_back(shape.id);
        if (IsInsideAreaTriggerShape(shape, x, y, z, kBotAreaTriggerLeaveMargin))
            nearNow.push_back(shape.id);
    }

    // Asked only while the bot stands inside a listed trigger. A human member
    // close by is resolved through the group's own slots (a GroupReference
    // pointer can outlive a relogging bot, issue #225).
    auto partyAllowed = [bot]()
    {
        bool humanNearby = false;
        for (Group::MemberSlot const& slot : bot->GetGroup()->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (member && member != bot && member->IsInWorld() && member->GetSession() &&
                member->GetSession()->HasNetworkTransport() && member->GetMapId() == bot->GetMapId() &&
                member->GetInstanceId() == bot->GetInstanceId() &&
                member->IsWithinDist(bot, kBotAreaTriggerHumanRange))
            {
                humanNearby = true;
                break;
            }
        }
        // Bound to a remote commander: every run-leader run is.
        bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(bot);
        return IsBotAreaTriggerPartyAllowed(commanded, humanNearby);
    };

    for (uint32_t id : state.Step(bot->GetMapId(), bot->GetInstanceId(), insideNow, nearNow, partyAllowed))
    {
        // Re-read the entry from the store: the index keeps copies, never
        // pointers into it.
        AreaTriggerEntry const* at = sObjectMgr.GetAreaTrigger(id);
        if (!at)
            continue;
        sLog.outDetail("TortoiseBots: %s fires area trigger %u (map %u).", bot->GetName(), id, bot->GetMapId());
        sScriptMgr.OnAreaTrigger(bot, at);
        // The script may have teleported, killed or ungrouped the bot.
        if (!bot->IsInWorld() || bot->IsBeingTeleported())
            break;
    }
}
