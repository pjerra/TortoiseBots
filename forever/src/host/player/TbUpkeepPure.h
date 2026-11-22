// The `.bot maintenance` rule without the core: TB's maintenance step order
// (commands/Inventory.cpp HandleMaintenance) and its eligibility checklist
// (same-account owned bot, pool bots refused, the flag, alive). Tested
// standalone by forever/tools/test_upkeep_pure.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{
namespace upkeep
{

// TB's maintenance body, in order: the seven factory top-ups, then the free
// repair, then the save.
enum class Step : std::uint8_t
{
    Reagents,
    Potions,
    Food,
    Consumes,
    Bandages,
    Ammo,
    Thrown,
    Repair,
    Save,
};

inline constexpr Step kSteps[] = {
    Step::Reagents,
    Step::Potions,
    Step::Food,
    Step::Consumes,
    Step::Bandages,
    Step::Ammo,
    Step::Thrown,
    Step::Repair,
    Step::Save,
};

inline constexpr std::size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// Which steps run on ForeverCore. Potions, food and consumes need the
// unported RandomItemMgr caches (SliceLinkStubs answers 0 for GetFood and
// GetRandomPotion, so those top-ups would find nothing); thrown only ever
// served pool bots carrying the item cheat, never an owned bot.
inline constexpr bool RunsOnForever(Step step)
{
    switch (step)
    {
        case Step::Potions:
        case Step::Food:
        case Step::Consumes:
        case Step::Thrown:
            return false;
        case Step::Reagents:
        case Step::Bandages:
        case Step::Ammo:
        case Step::Repair:
        case Step::Save:
            return true;
    }
    return false;
}

struct Input
{
    bool enabled = false;   // AiPlayerbot.OwnedBotMaintenanceEnabled
    bool poolBot = false;   // BotMgr::IsPoolBot: progresses organically
    bool alive = false;
};

enum class Refusal
{
    None,
    Pool,
    Disabled,
    Dead,
};

// TB's check order: pool first, then the flag, then alive.
inline Refusal Decide(Input const& in)
{
    if (in.poolBot)
        return Refusal::Pool;
    if (!in.enabled)
        return Refusal::Disabled;
    if (!in.alive)
        return Refusal::Dead;
    return Refusal::None;
}

inline char const* CodeOf(Refusal refusal)
{
    switch (refusal)
    {
        case Refusal::None:     return "";
        case Refusal::Pool:     return "pool";
        case Refusal::Disabled: return "disabled";
        case Refusal::Dead:     return "dead";
    }
    return "";
}

} // namespace upkeep
} // namespace TortoiseBots
