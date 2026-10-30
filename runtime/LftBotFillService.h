#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class ObjectGuid;

namespace TortoiseBots
{

// Default-on bounded fill of human-waiting LFT queues with live random Headless bots.
// The service observes the copy-only native queue API from core #416 and
// replays the core offer match (LftCompPolicy) over entries that may join a
// new offer - core queue order, party blocks atomic, committed in-offer
// members excluded - to find the missing 1 tank / 1 healer / 3 dps slots,
// then filters in-memory Headless random candidates by the authoritative
// Turtle client LFT LFT_Data.lua minLevel/maxLevel range, team,
// hardcore, group, state, and AiFactory spec role before QueuePlayer.
// Native core owns offers, acceptance, cancellation, and group formation; the
// service auto-accepts its own Headless participants and, independently of the
// fill switch, managed party bots whose real-player master/group leader shares
// the offer (rolecheck answers come from the rolecheck hook: forced role,
// then named combat specs, then AiFactory spec/gear). Unknown dungeon ranges
// fail closed. No second queue, DB tick, addon protocol, or role hook.
class LftBotFillService
{
public:
    static LftBotFillService& Instance();

    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    // Activity-lease eviction hook (issue #89): synchronously leaves the
    // native LFT queue, clears the forced role, and drops pending tracking.
    // Must not touch the lease map; the manager owns the transition.
    void OnLeaseEvicted(uint32_t guidLow);

    uint8 GetBotRoleMask(Player const* bot) const;
    void ClearForcedRole(uint32 guidLow);
    // The forced role and the kit rebuild `.bot role` runs; 0 clears it.
    void ApplyForcedRole(Player* bot, uint8 role);
    // Issue #189 Phase 4: natural-role eligibility (no borrowing by default)
    // plus best-shield-from-bags equip for shield-class tanks. Returns the
    // skip reason when the bot must not fill the role, empty when it may.
    std::string RoleMismatchReason(Player* bot, uint8 needRole) const;
    bool EquipBestShieldFromBags(Player* bot) const;
    // Issue #966: role bits of the installed combat kit (named spec
    // strategies only, like BotPlayerAdapter::GetBotRoles); 0 = bare kit.
    uint8 GetCombatKitRoleMask(Player const* bot) const;

private:
    LftBotFillService() = default;
    ~LftBotFillService() = default;

    bool IsEligibleCandidate(Player* bot) const;
    bool IsModuleOwnedHeadlessBot(Player const* bot) const;
    void ReconcilePending(bool cancelAll, std::vector<std::string> const* activeInstances = nullptr);
    void AcceptPendingOffers();
    // Own-party offer acceptance (independent of the random-fill switch):
    // auto-accept native offers for managed bots grouped with / mastered by a
    // real player who shares the offer. Fill-owned bots (m_pending) are never
    // touched here; solo managed bots never auto-accept.
    void AcceptPartyBotOffers();

    bool m_initialized = false;
    uint32_t m_elapsedMs = 0;
    // guidLow -> instance we queued the bot for (single instance, the one we filled)
    std::unordered_map<uint32_t, std::string> m_pending;
    // "instance:role:reason" -> skip count for this tick, logged once per tick.
    std::unordered_map<std::string, uint32_t> m_skipReasons;
};

} // namespace TortoiseBots
