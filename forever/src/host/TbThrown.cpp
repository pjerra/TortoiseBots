// S3 (throws and falls): see TbThrown.h. Core only; no engine header and no legacy type.
#include "host/TbThrown.h"
#include "host/TbThrownPure.h"
#include "host/TbEngineBrain.h"
#include "host/TbH3.h"
#include "TbBotMgr.h"

#include "CharacterCache.h"
#include "Log.h"
#include "Chat.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Player.h"
#include "Timer.h"
#include "Unit.h"

#include "compat/TbCompat_KB.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace TortoiseBots
{
namespace kb
{
namespace
{
struct Record
{
    State flight;
    uint32 thrown = 0;
    uint32 falldmg = 0;
};

std::map<ObjectGuid, Record>& Records()
{
    static std::map<ObjectGuid, Record> records;
    return records;
}

// The core's liquid read at the bot's feet. A landing in water is a swim start on a client, not CMSG_MOVE_FALL_LAND:
// nothing is charged. Asked only when a flight ends, never per tick.
bool InWaterAt(Player* bot)
{
    Map* map = bot->FindMap();
    if (!map)
        return false;
    ZLiquidStatus const status = map->GetLiquidStatus(bot->GetPhaseShift(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    return (status & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
}

Player* BotByName(ChatHandler* handler, std::string const& name)
{
    ObjectGuid const guid = sCharacterCache ? sCharacterCache->GetCharacterGuidByName(name) : ObjectGuid::Empty;
    if (guid.IsEmpty())
    {
        handler->PSendSysMessage("TortoiseBots: no character named %s.", name.c_str());
        handler->SetSentErrorMessage(true);
        return nullptr;
    }
    if (Player* bot = BotMgr::Instance().FindBot(guid))
        return bot;
    handler->PSendSysMessage("TortoiseBots: %s is not a logged-in bot.", name.c_str());
    handler->SetSentErrorMessage(true);
    return nullptr;
}
}

void Update(Player* bot, engine::Brain* brain)
{
    if (!bot || !bot->IsInWorld())
        return;

    Record& record = Records()[bot->GetGUID()];
    MotionMaster* motion = bot->GetMotionMaster();
    Sample sample;
    sample.effectMotion = motion && motion->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE;
    sample.splineDone = !bot->movespline || bot->movespline->Finalized();
    sample.remainingMs = sample.splineDone ? 0 : uint32(std::max(0, bot->movespline->timeRemaining()));
    sample.mapId = bot->GetMapId();
    sample.instanceId = bot->GetInstanceId();
    sample.x = bot->GetPositionX();
    sample.y = bot->GetPositionY();
    sample.z = bot->GetPositionZ();
    sample.nowMs = getMSTime();

    Step const step = Observe(record.flight, sample, [bot] { return InWaterAt(bot); });
    switch (step.kind)
    {
        case Kind::Start:
            ++record.thrown;
            if (brain)
                engine::NoteThrown(brain, step.jumpUntilMs, true);
            break;
        case Kind::Flying:
            // A flight may grow (the spline is re-timed); the engine's wait follows it, never shrinks.
            if (brain)
                engine::NoteThrown(brain, step.jumpUntilMs, false);
            break;
        case Kind::Land:
            // Not charged when the flight ended somewhere else (a teleport mid-flight) or in liquid: what the core would
            // charge there is a fall the bot did not take.
            record.falldmg = step.chargesFall ? tbcompat::FallLandCompat(bot, step.apexZ) : 0;
            TC_LOG_INFO("tortoisebots", "thrown: {} landed at z {:.1f} from apex {:.1f} ({} fall damage{})", bot->GetName(),
                step.landZ, step.apexZ, record.falldmg, step.chargesFall ? "" : ", not this flight's landing");
            break;
        case Kind::None:
            break;
    }
}

void Forget(ObjectGuid guid)
{
    Records().erase(guid);
}

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
void AppendThrownState(h3::Rec& rec, Player* bot)
{
    Record const& record = Records()[bot->GetGUID()];
    rec.N("thrown", record.thrown).N("falldmg", record.falldmg).F("runrate", bot->GetSpeedRate(MOVE_RUN), 2);
}
#else   // TbH3.cpp (Rec's writers) is not built without the engine, and only the engine's `.tb state` calls this
void AppendThrownState(h3::Rec&, Player*) {}
#endif

bool HandleKnock(ChatHandler* handler, std::string name, float speedXY, float speedZ)
{
    Player* bot = BotByName(handler, name);
    if (!bot)
        return false;
    if (!bot->IsAlive())
    {
        handler->PSendSysMessage("TortoiseBots: %s is dead.", name.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }
    if (std::abs(speedXY) < 0.01f && std::abs(speedZ) < 0.01f)
    {
        handler->PSendSysMessage("TortoiseBots: both speeds are zero: nothing would move.");
        handler->SetSentErrorMessage(true);
        return false;
    }

    float const x = bot->GetPositionX() + std::cos(bot->GetOrientation());
    float const y = bot->GetPositionY() + std::sin(bot->GetOrientation());
    bot->KnockbackFrom(Position(x, y, bot->GetPositionZ(), bot->GetOrientation()), speedXY, speedZ);
    handler->PSendSysMessage("TortoiseBots: %s knocked back (speedXY %.1f, speedZ %.1f); the host watches the flight and lands it with the core's fall damage.",
        name.c_str(), speedXY, speedZ);
    return true;
}

bool HandleSpeed(ChatHandler* handler, std::string name, std::string kind, float rate)
{
    Player* bot = BotByName(handler, name);
    if (!bot)
        return false;
    UnitMoveType type;
    if (kind == "run")
        type = MOVE_RUN;
    else if (kind == "walk")
        type = MOVE_WALK;
    else if (kind == "swim")
        type = MOVE_SWIM;
    else
    {
        handler->PSendSysMessage("TortoiseBots: the speed is run, walk or swim.");
        handler->SetSentErrorMessage(true);
        return false;
    }
    if (!(rate > 0.0f) || rate > 10.0f)
    {
        handler->PSendSysMessage("TortoiseBots: the rate must be above 0 and at most 10.");
        handler->SetSentErrorMessage(true);
        return false;
    }
    bot->SetSpeedRate(type, rate);
    handler->PSendSysMessage("TortoiseBots: %s %s rate is %.2f (runrate=%.2f).", name.c_str(), kind.c_str(), rate, bot->GetSpeedRate(MOVE_RUN));
    return true;
}
}
}
