// Host service R6B-3: the tracing, observability, combat monitor and camp services of the legacy engine, switched off for the
// ForeverCore host. Built only with TORTOISEBOTS_SLICE1 (CMakeLists.txt).
//
// The engine calls these hooks from its tick (PlayerbotAI::DoNextAction, Engine::DoNextAction, ...): each one first asks whether
// its service is on, and the answer here is no. Their real owners (runtime/WhyTrace.cpp, RunTrace.cpp, ObservabilityEmitter.cpp,
// CombatMonitorService.cpp, ai/playerbot/CampMgr.cpp) are not part of the slice. Every function is listed in
// forever/docs/SLICE1-R6B-NOTES.md (section 3, choice c); none keeps state, so there is nothing to flush at shutdown.
#include "compat/botpch_forever.h"

#include "playerbot/CampMgr.h"
#include "runtime/CombatMonitorService.h"
#include "runtime/ObservabilityEmitter.h"
#include "runtime/RunTrace.h"
#include "runtime/WhyTrace.h"

namespace TortoiseBots
{
// ---- WhyTraceService: no bot is traced. ----
WhyTraceService& WhyTraceService::Instance()
{
    static WhyTraceService instance;
    return instance;
}

bool WhyTraceService::Traces(PlayerbotAI* /*ai*/) const
{
    return false;
}

void WhyTraceService::NoteTickStart(uint64_t /*botRaw*/, std::vector<std::pair<std::string, float>> const& /*pending*/)
{
}

void WhyTraceService::NoteOrder(PlayerbotAI* /*ai*/, Player* /*from*/, std::string const& /*text*/)
{
}

void WhyTraceService::NoteOrderAction(PlayerbotAI* /*ai*/, Player* /*from*/, std::string const& /*action*/, std::string const& /*source*/)
{
}

WhyTraceService::TickScope::~TickScope()
{
}

// ---- RunTraceService: inactive (Active() reads its own flag, false); the hooks are called only when it is active. ----
RunTraceService& RunTraceService::Instance()
{
    static RunTraceService instance;
    return instance;
}

void RunTraceService::NoteBotEvent(PlayerbotAI* /*ai*/, std::string const& /*eventName*/, std::string const& /*info1*/,
    std::string const& /*info2*/)
{
}

void RunTraceService::NoteSpellInterrupted(PlayerbotAI* /*ai*/, uint32_t /*spellId*/)
{
}

// ---- ObservabilityEmitter: disabled. ----
ObservabilityEmitter& ObservabilityEmitter::Instance()
{
    static ObservabilityEmitter instance;
    return instance;
}

bool ObservabilityEmitter::IsEnabled() const
{
    return false;
}

void ObservabilityEmitter::EmitAnomaly(std::string const& /*type*/, std::string const& /*severity*/, Player* /*bot*/,
    std::string const& /*details*/, std::string const& /*targetName*/, std::string const& /*strategy*/,
    std::string const& /*lastAction*/, uint32 /*killerLevel*/)
{
}

void ObservabilityEmitter::OnActionFailed(Player* /*bot*/, std::string const& /*actionName*/, std::string const& /*targetName*/,
    std::string const& /*strategy*/)
{
}

void ObservabilityEmitter::EmitBotActivity(std::string const& /*event*/, std::string const& /*info1*/, std::string const& /*info2*/,
    Player* /*bot*/, uint32 /*targetLevel*/)
{
}

// ---- CombatMonitorService: nothing to load. ----
CombatMonitorService& CombatMonitorService::Instance()
{
    static CombatMonitorService instance;
    return instance;
}

void CombatMonitorService::LoadTimelineWatch()
{
    tbcompat::Unported(tbcompat::Feature::Tracing);
}

// ---- CampMgr: no camp spots, no active camp. ----
CampMgr& CampMgr::Instance()
{
    static CampMgr instance;
    return instance;
}

void CampMgr::Load()
{
    tbcompat::Unported(tbcompat::Feature::CampSpots);
}

void CampMgr::ClearActiveCamp(uint32 /*botGuid*/)
{
}
}
