#pragma once

#include "Define.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Player;
namespace ai { struct GearItemCandidate; }

namespace TortoiseBots
{
namespace gear
{

struct GearSlotResult
{
    uint8 slot = 0;
    uint32 itemId = 0;
    uint16 itemLevel = 0;
    uint32 score = 0;
    std::string status; // "equipped" | "kept" | "refused"
    std::string code;   // "no-item" | "bags-full" | "two-hand" | "core-refused"
};

struct GearAuditEntry
{
    uint8 slot = 0;
    uint32 candidates = 0;
};

struct GearSetOutcome
{
    bool ok = false;
    std::string errorCode;
    std::string errorDetail;
    std::string specName;
    uint32 level = 0;
    uint8 maxQuality = 0;
    uint32 equipped = 0;
    uint32 failed = 0;
    std::vector<GearSlotResult> slots;
};

void InitGearIndex();
uint32 GearIndexItemCount();
std::vector<GearAuditEntry> AuditGearIndex(uint32 level);
std::vector<ai::GearItemCandidate> const& SlotCandidates(uint8 slot);
ai::GearItemCandidate const* FindCandidate(uint32 itemId);
GearSetOutcome SetGearForTarget(Player* target, std::string const& specArg, uint32 levelArg, int32 qualityArg);
// One-time spawn kit for a fresh pool bot (level >= 5): the quality mix of runtime/GearSeedMixPolicy.h, the effective
// required level, the pool item-level cap and the source gate. Replaces the core's starting outfit; refuses below 5.
GearSetOutcome SeedGearForTarget(Player* target, uint32 levelArg);
// Incremental pool-bot top-up: fills only the equipment slots in `slotMask` (one bit per slot, the policy is
// runtime/PoolGearTopUpPolicy.h), with the gear index and the pool-bot quality and item-level caps.
GearSetOutcome TopUpGearForTarget(Player* target, uint32 levelArg, uint32 slotMask);

} // namespace gear
} // namespace TortoiseBots
