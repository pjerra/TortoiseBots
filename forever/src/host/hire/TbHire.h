// Hired companions on ForeverCore (issue #298): the contract between the hire pieces. Built only with
// TORTOISEBOTS_SLICE1. Plan and package split: forever/docs/HIRE-PORT-PLAN.md.
//
// Hiring is at a <Mercenary Hire> recruiter by default. The fast path (`.bot hire`, the add-on Hire tab) works anywhere only
// when AiPlayerbot.HireAnywhere is on (default off on Forever).
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <cstdint>
#include <string>
#include <vector>

class Creature;
class Player;

namespace TortoiseBots::hire
{
struct Selection
{
    uint8 classId = 0;
    uint8 race = 0;
    uint8 gender = 0;
    uint8 role = 0;        // ai::BOT_ROLE_* bit; 0 = the class default
    int specIndex = -1;    // row of the recruiter spec menu (runtime/HireSpecPolicy.h); -1 = the role default
    uint8 gearTier = 0;    // 0 = levelled kit, 1.. = contract sets (TbHireTiers.h)
    uint8 level = 0;       // 0 = the hirer's level (runtime/HireLevelPolicy.h clamps)
    bool anyFaction = false;
};

enum class Status
{
    Ok,
    Disabled,
    NoPermission,
    RecruiterRequired,
    InvalidChoice,
    GroupFull,
    CapReached,
    Poor,
    TierLocked,
    Failed
};

struct Outcome
{
    Status status = Status::Failed;
    std::string message;   // plain player-facing text, never a TBM: line
    std::string botName;
    uint32 cost = 0;       // copper charged
};

// ---- Package P2 (TbHireProvision.cpp) ----
// One hire: gates, fee, a new character on a hire account, level, spec, gear, login bound to the hirer, party invite.
// recruiter = the NPC the hire came from; nullptr = fast path (refused with RecruiterRequired unless HireAnywhere()).
Outcome Hire(Player* hirer, Selection const& selection, Creature* recruiter);
uint32 QuoteFee(Player* hirer, uint8 level, uint8 gearTier);
bool Enabled();
bool HireAnywhere();
// Work deferred to the world tick (provision steps of hires in flight). Called from the module's world update.
void Update(uint32 diff);

// ---- Package P4 (TbHireTiers.cpp): contract gear tiers and their unlocks ----
uint8 HighestUnlockedTier(Player* hirer);
// Equips the tier set for the hire's class/spec/level; false = no set (caller keeps the levelled kit).
bool EquipTierSet(Player* bot, uint8 tier, int specIndex);
std::string TierName(uint8 tier);
// Contracts gossip page (capital recruiters, level 60): the unlock progress. Show* adds menu items and sends the menu.
bool ContractsVisible(Player* player, Creature* recruiter);
void ShowContractsMenu(Player* player, Creature* recruiter);
// true = the click belonged to this page and was handled.
bool OnContractsSelect(Player* player, Creature* recruiter, uint32 sender, uint32 action);

// ---- Package P5 (TbHireWorldBuffs.cpp): world buffs sold at capital recruiters ----
bool WorldBuffsVisible(Player* player, Creature* recruiter);
void ShowWorldBuffMenu(Player* player, Creature* recruiter);
bool OnWorldBuffSelect(Player* player, Creature* recruiter, uint32 sender, uint32 action);

// Gossip sender ranges (one owner each; P3 routes a click by its sender).
constexpr uint32 kSenderWizardFirst = 501;     // P3: class 501, race 502, gender 503, spec 504
constexpr uint32 kSenderWorldBuffFirst = 505;  // P5: 505-509
constexpr uint32 kSenderWorldBuffLast = 509;
constexpr uint32 kSenderContractsFirst = 510;  // P4: 510-512
constexpr uint32 kSenderContractsLast = 512;
constexpr uint32 kSenderTierFirst = 520;       // P3: tier step 520+t, confirm 530+t, back 540 (runtime/HireTierRequestPolicy.h)
}
