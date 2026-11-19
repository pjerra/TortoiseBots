#include "GuidPosition.h"
#include <numeric>
#include <iomanip>
#include <regex>

#include "GameEventMgr.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"

using namespace ai;
using namespace MaNGOS;

GuidPosition::GuidPosition(std::string qualifier)
{
    std::stringstream b(qualifier);

    char p;
    b >> this->mapId >> p >> this->x >> p >> this->y >> p >> this->z >> p >> this->o;

    //if (b.tellp() == std::streampos(0))
    //    return;

    b >> p;

    ObjectGuid guid = GuidReadCompat(b);
    ObjectGuid::operator=(guid);
}

GuidPosition::GuidPosition(CreationMask type, const std::string& qualifier, const WorldPosition& referencePos, uint32 instanceId)
{
    if (type & CreationMask::COORDINATES)
    {
        static const std::regex pattern(
            R"(^\d+;-?\d+(\.\d+)?;-?\d+(\.\d+)?;-?\d+(\.\d+)?;-?\d+(\.\d+)?$)");
        if (std::regex_match(qualifier, pattern))
        {
            WorldPosition::set(WorldPosition(qualifier));
            ObjectGuid::Clear();
            return;
        }
    }

    if (type & CreationMask::GAMETELE)
    {
        GameTele const* tele = sObjectMgr.GetGameTele(qualifier);
        if (tele)
        {
            float teleX, teleY, teleZ;
            GameTelePointCompat(tele, teleX, teleY, teleZ);
            set(WorldPosition(tele->mapId, teleX, teleY, teleZ, referencePos.o));
            ObjectGuid::Clear();
            return;
        }
    }
}

std::string GuidPosition::to_string() const
{
    std::ostringstream b;
    char p = '|';
    b << this->WorldPosition::GetMapId() << p << this->x << p << this->y << p << this->z << p << this->o << p;
    GuidWriteCompat(b, *this);
    return b.str();
}

Creature* GuidPosition::GetCreature(uint32 instanceId) const
{
    if (!*this)
        return nullptr;

    if (!getMap(instanceId))
        return nullptr;

    return GetAnyTypeCreatureCompat(getMap(instanceId), *this);
}

Unit* GuidPosition::GetUnit(uint32 instanceId) const
{
    if (!*this)
        return nullptr;

    if (IsPlayer())
        return sObjectAccessor.FindPlayer(*this);

    return GetCreature(instanceId);
}

GameObject* GuidPosition::GetGameObject(uint32 instanceId) const
{
    if (!*this)
        return nullptr;

    if (!getMap(instanceId))
        return nullptr;

    return getMap(instanceId)->GetGameObject(*this);
}

Player* GuidPosition::GetPlayer() const
{
    if (!*this)
        return nullptr;

    if (IsPlayer())
        return sObjectAccessor.FindPlayer(*this);

    return nullptr;
}

const FactionTemplateEntry* GuidPosition::GetFactionTemplateEntry() const
{
    if (IsPlayer() && GetPlayer())
        return GetPlayer()->GetFactionTemplateEntry();
    if (IsCreature()  && GetCreatureTemplate())
        return sFactionTemplateStore.LookupEntry(GetCreatureTemplate()->faction);
    if (IsGameObject() && GetGameObjectInfo())
        return sFactionTemplateStore.LookupEntry(GameObjectFactionCompat(GetGameObjectInfo()));

    return nullptr;
}

const ReputationRank GuidPosition::GetReactionTo(const GuidPosition& other, uint32 instanceId) const
{
    if(other.IsUnit() && other.GetUnit(instanceId))
        if (IsPlayerControlledCompat(other.GetUnit(instanceId)))
        {
            if (const Player* unitPlayer = other.GetUnit(instanceId)->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                if (IsContestedPvpCompat(unitPlayer) && GetFactionTemplateEntry()->IsContestedGuardFaction())
                    return REP_HOSTILE;

                if (const ReputationRank* rank = ForcedReputationRankCompat(unitPlayer, GetFactionTemplateEntry()))
                    return (*rank);

                const FactionEntry* unitFactionEntry = sFactionStore.LookupEntry(FactionOfTemplateCompat(GetFactionTemplateEntry()));
                return ReputationAtWarCompat(unitPlayer, unitFactionEntry) ? REP_HOSTILE : REP_FRIENDLY;
            }
        }

    return PlayerbotAI::GetFactionReaction(GetFactionTemplateEntry(), other.GetFactionTemplateEntry());
}

bool GuidPosition::isDead(uint32 instanceId) const
{
    if (!getMap(instanceId))
        return false;

    if (!MapIsLoadedCompat(getMap(instanceId), getX(), getY()))
        return false;

    if (IsUnit() && GetUnit(instanceId) && GetUnit(instanceId)->IsInWorld() && GetUnit(instanceId)->IsAlive())
        return false;

    if (IsGameObject() && GetGameObject(instanceId) && GetGameObject(instanceId)->IsInWorld())
        return false;

    return true;
}

uint16 GuidPosition::IsPartOfAPool()
{
    if (IsCreature())
        return PoolOfSpawnCompat(true, GetCounter());
    if (IsGameObject())
        return PoolOfSpawnCompat(false, GetCounter());

    return 0;
}

uint16 GuidPosition::GetGameEventId()
{
    return GameEventOfSpawnCompat(IsPartOfAPool(), IsCreature(), IsGameObject(), GetCounter());
}

bool GuidPosition::IsEventUnspawned()
{
    if (int16 event_id = GetGameEventId())
        if (!GameEventActiveCompat(event_id))
            return true;

    return false;
}

std::string GuidPosition::print()
{
    std::ostringstream out;
    GuidWriteCompat(out, *this);
    out << ';' << mapId << std::fixed << std::setprecision(2);
    out << ';' << x;
    out << ';' << y;
    out << ';' << z;
    out << ';' << o;

    return out.str();
}