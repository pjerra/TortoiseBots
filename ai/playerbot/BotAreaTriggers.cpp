#include "playerbot/playerbot.h"
#include "BotAreaTriggers.h"

#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "../../runtime/CommandedGroupQuery.h"

#include <algorithm>
#include <unordered_map>
#include "playerbot/TbCompatW3.h"

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
        uint32 listed = 0;
        for (auto const& itr : sObjectMgr.GetAreaTriggersMap())
        {
            AreaTriggerEntry const& at = itr.second;
            MapEntry const* map = sMapStorage.LookupEntry<MapEntry>(at.mapid);
            // Listed or not: an unlisted one is armed only while a run's current move step names it.
            if (!IsBotIndexableAreaTrigger(ScriptMgrW3().GetAreaTriggerScriptId(at.id) != 0,
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
            shape.listed = IsListedAreaTrigger(sPlayerbotAIConfig.botAreaTriggerIds, at.id);
            index[at.mapid].push_back(shape);
            ++count;
            if (shape.listed)
                ++listed;
        }
        sLog.outString("TortoiseBots: %u listed area triggers on %u dungeon maps can fire for bots "
                       "(%u scripted in all; an unlisted one only at a run's move step that names it).",
                       listed, uint32(index.size()), count);
        return index;
    }
}

void ai::UpdateBotAreaTriggers(Player* bot, BotAreaTriggerState& state, std::vector<uint32_t> const* runArmed)
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
        if (!IsBotAreaTriggerArmed(shape.listed, shape.id, runArmed))
            continue;
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
                HasNetworkTransportCompat(member->GetSession()) && member->GetMapId() == bot->GetMapId() &&
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

    // The run's armed ids, copied before any script runs: a fired one is erased from
    // the run's own vector (runArmed points into it), so it is not read after a script.
    std::vector<uint32_t> const armedIds = runArmed ? *runArmed : std::vector<uint32_t>();
    for (uint32_t id : state.Step(bot->GetMapId(), bot->GetInstanceId(), insideNow, nearNow, partyAllowed))
    {
        // Re-read the entry from the store: the index keeps copies, never
        // pointers into it.
        AreaTriggerEntry const* at = sObjectMgr.GetAreaTrigger(id);
        if (!at)
            continue;
        sLog.outDetail("TortoiseBots: %s fires area trigger %u (map %u).", CStrCompat(bot->GetName()), id, bot->GetMapId());
        bool const armedByRun = std::find(armedIds.begin(), armedIds.end(), id) != armedIds.end();
        bool const ran = ScriptMgrW3().OnAreaTrigger(bot, at);
        // A run-armed trigger counts as fired for the run (DungeonRun::NoteTriggerFired) only
        // when its script ran (at_zumrah returns false while Zum'rah is not within 30 yd);
        // a refusal keeps it armed and this bot tries again in kBotAreaTriggerRetryMs while
        // it stays inside.
        if (armedByRun)
        {
            if (ran)
                TortoiseBots::BotCommands::NoteRunAreaTriggerFired(bot, id);
            else
                state.Refused(id);
        }
        // The script may have teleported, killed or ungrouped the bot.
        if (!bot->IsInWorld() || bot->IsBeingTeleported())
            break;
    }
}
