#include "RaidBossDiagnostics.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/WorldPosition.h"
#include "Creature.h"
#include "Player.h"
#include "Map.h"
#include "ThreatManager.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include <ctime>

namespace TortoiseBots {
namespace {
bool EngagedBoss(Creature* boss)
{
    return boss && boss->IsWorldBoss() && boss->GetMap() && boss->GetMap()->IsRaid() &&
        boss->IsAlive() && boss->IsInCombat() && !boss->IsInEvadeMode();
}

std::string Timestamp()
{
    // Map workers may log concurrently; avoid the shared std::localtime buffer.
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[20];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &utc);
    return text;
}

void Write(Unit* actor, char const* event, std::string const& subject, std::string const& detail)
{
    // Retain bot_events.csv's nine-column layout; actor may be a boss or player.
    std::ostringstream position;
    position << std::fixed << std::setprecision(2);
    ai::WorldPosition(actor).printWKT(position);
    auto row = RaidBossLog::Row(Timestamp(), actor->GetName(), event, position.str(),
        actor->IsPlayer() ? actor->ToPlayer()->GetRace() : 0,
        actor->IsPlayer() ? actor->ToPlayer()->GetClass() : 0,
        float(actor->GetLevel()), subject, detail);
    sPlayerbotAIConfig.log("bot_events.csv", row.c_str());
}

// Heal packets carry no boss reference, so ObservePacket needs to know which (map, instance) pairs hold an engaged
// boss. Its own mutex: ObservePacket never takes the diagnostics mutex, and OnUnitUpdate fans out on map workers.
std::mutex bossSeenMutex;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> bossSeen;
constexpr uint32_t kHealBossSeenMs = 5000;

void NoteEngagedBoss(uint32_t map, uint32_t instance, uint32_t now)
{
    std::lock_guard<std::mutex> guard(bossSeenMutex);
    bossSeen[{map, instance}] = now;
    for (auto it = bossSeen.begin(); it != bossSeen.end();)
        if (uint32(now - it->second) > kHealBossSeenMs) it = bossSeen.erase(it);
        else ++it;
}

bool EngagedBossRecent(uint32_t map, uint32_t instance, uint32_t now)
{
    std::lock_guard<std::mutex> guard(bossSeenMutex);
    auto it = bossSeen.find({map, instance});
    return it != bossSeen.end() && uint32(now - it->second) <= kHealBossSeenMs;
}
}

RaidBossDiagnostics::Key RaidBossDiagnostics::Identity(Unit* unit)
{
    return {unit->GetMapId(), unit->GetInstanceId(), unit->GetObjectGuid().GetRawValue()};
}

void RaidBossDiagnostics::OnUnitUpdate(Unit* unit, uint32 diff)
{
    if (!sPlayerbotAIConfig.raidBossDiagnostics || !unit || !unit->IsCreature()) return;
    Creature* boss = static_cast<Creature*>(unit);
    if (!EngagedBoss(boss) || !sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    {
        std::lock_guard<std::mutex> guard(mutex);
        uint32 now = WorldTimer::getMSTime();
        Clock& clock = clocks[Identity(boss)];
        clock.seen = now;
        NoteEngagedBoss(boss->GetMapId(), boss->GetInstanceId(), now);
        if (!clock.throttle.Due(diff)) return;
        // A removed creature need not emit combat-exit. Retire unseen state
        // on the next snapshot, using wrap-safe time and no stored Unit pointers.
        for (auto it = clocks.begin(); it != clocks.end();)
            if (uint32(now - it->second.seen) > 30000) it = clocks.erase(it);
            else ++it;
    }

    std::vector<RaidBossLog::ThreatEntry> entries;
    for (auto* ref : boss->GetThreatManager().getThreatList())
    {
        Unit* target = ref ? ref->getTarget() : nullptr;
        if (target)
            entries.push_back({target->GetObjectGuid().GetRawValue(), target->GetName(), ref->getThreat()});
    }
    Unit* victim = boss->GetVictim();
    Write(boss, "BossThreat", boss->GetName(), RaidBossLog::Threat(boss->GetEntry(), boss->GetMapId(),
        boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue(), victim ? victim->GetName() : "none", entries));
}

void RaidBossDiagnostics::OnUnitExitCombat(Unit* unit)
{
    if (!unit || !unit->IsCreature() || !static_cast<Creature*>(unit)->IsWorldBoss() ||
        !unit->GetMap() || !unit->GetMap()->IsRaid()) return;
    std::lock_guard<std::mutex> guard(mutex);
    clocks.erase(Identity(unit));
}

void RaidBossDiagnostics::ObservePacket(WorldSession* session, WorldPacket const& packet)
{
    uint16 const opcode = packet.getOpcode();
    if ((opcode != SMSG_SPELLNONMELEEDAMAGELOG && opcode != SMSG_SPELLHEALLOG) ||
        !sPlayerbotAIConfig.raidBossDiagnostics || !session ||
        !sPlayerbotAIConfig.hasLog("bot_events.csv")) return;
    // Unlike chat, these opcodes are emitted synchronously by combat processing
    // (WorldObject::SendSpellNonMeleeDamageLog and SendHealSpellLog -> Map::MessageBroadcast).
    // Only these opcodes may inspect the victim here: they run on the owning map
    // worker or world thread, where player/AI lifetime is stable. Never route
    // asynchronous chat or channel packets through these object reads.
    Player* victim = session->GetPlayer();
    if (!victim || !victim->IsInWorld() || !victim->GetMap()->IsRaid() ||
        (!victim->GetGroup() && !PlayerbotAIStorage::Instance().GetAI(victim))) return;

    if (opcode == SMSG_SPELLHEALLOG)
    {
        // A heal packet names no boss; the row needs an engaged boss in this map and instance just now.
        if (!EngagedBossRecent(victim->GetMapId(), victim->GetInstanceId(), WorldTimer::getMSTime())) return;
        RaidBossLog::Heal heal;
        if (!RaidBossLog::ParseHeal(packet.contents(), packet.size(), heal) ||
            !RaidBossLog::TargetCopy(victim->GetObjectGuid().GetRawValue(), heal)) return;
        Player* healer = victim->GetMap()->GetPlayer(ObjectGuid(heal.caster));
        if (!healer) return;
        // Only the target's session copy: the cast reaches every witness nearby.
        Write(victim, "BossHeal", healer->GetName(), RaidBossLog::HealRow(victim->GetMapId(),
            victim->GetInstanceId(), heal, healer->GetName(), victim->GetName(), victim->GetHealth(),
            victim->GetMaxHealth()));
        return;
    }

    RaidBossLog::Damage damage;
    if (!RaidBossLog::ParseDamage(packet.contents(), packet.size(), damage) ||
        !RaidBossLog::VictimCopy(victim->GetObjectGuid().GetRawValue(), damage)) return;
    Creature* boss = victim->GetMap()->GetCreature(ObjectGuid(damage.caster));
    if (!EngagedBoss(boss)) return;
    // Only the victim's session copy: broadcasts to other players/bots do not duplicate rows.
    // This packet is emitted before application on the normal direct-damage path.
    Write(victim, "BossSpellHit", boss->GetName(), RaidBossLog::Hit(boss->GetEntry(), boss->GetMapId(),
        boss->GetInstanceId(), damage, boss->GetName(), victim->GetName(), victim->GetHealth()));
}
}
