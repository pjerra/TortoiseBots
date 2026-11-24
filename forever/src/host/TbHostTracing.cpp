// Host service R6B-3: the tracing, observability, combat monitor and camp services of the legacy engine, switched off for the
// ForeverCore host. Built only with TORTOISEBOTS_SLICE1 (CMakeLists.txt).
//
// The engine calls these hooks from its tick (PlayerbotAI::DoNextAction, Engine::DoNextAction, ...): each one first asks whether
// its service is on, and the answer here is no. Their real owners (runtime/ObservabilityEmitter.cpp, ai/playerbot/CampMgr.cpp)
// are not part of the slice. Every function is listed in forever/docs/SLICE1-R6B-NOTES.md (section 3, choice c); none keeps
// state, so there is nothing to flush at shutdown. The run trace and the why trace are ported (runtime/RunTrace.cpp,
// runtime/WhyTrace.cpp), so their symbols are no longer answered here.
// runtime/CombatMonitorService.cpp is compiled and fed by package P5 (host/cmd/TbCmdCombat.cpp), so its two entry points are not stubbed here.
#include "compat/botpch_forever.h"

#include "playerbot/CampMgr.h"
#include "runtime/ObservabilityEmitter.h"
#include "TbDashboard.h"

namespace TortoiseBots
{
// ---- ObservabilityEmitter: disabled. ----
ObservabilityEmitter::ObservabilityEmitter() = default;
ObservabilityEmitter::~ObservabilityEmitter() = default;

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

void ObservabilityEmitter::OnActionFailed(Player* bot, std::string const& actionName, std::string const& targetName,
    std::string const& strategy)
{
    Dashboard::OnActionFailed(bot, actionName, targetName, strategy);
}

void ObservabilityEmitter::EmitBotActivity(std::string const& event, std::string const& info1, std::string const& info2,
    Player* bot, uint32 targetLevel)
{
    Dashboard::OnBotActivity(event, info1, info2, bot, targetLevel);
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

// The camp spot table (ai_playerbot_camp) is not ported: no spot is ever found, so the camp trigger stops here.
std::optional<CampSpot> CampMgr::FindNearestSpot(uint32, float, float, float, float) const
{
    tbcompat::Unported(tbcompat::Feature::CampSpots);
    return std::nullopt;
}

std::optional<std::pair<CampSpot, float>> CampMgr::FindNearestSpotWithDist(uint32, float, float, float, float) const
{
    tbcompat::Unported(tbcompat::Feature::CampSpots);
    return std::nullopt;
}

void CampMgr::ClearActiveCamp(uint32 /*botGuid*/)
{
}
}
