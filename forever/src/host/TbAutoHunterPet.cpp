// Host half of automatic starter hunter pets (TbAutoHunterPetPure.h).
#include "host/TbAutoHunterPet.h"
#include "host/TbAutoHunterPetPure.h"

#include "TbBotMgr.h"

#include "Config.h"
#include "Pet.h"
#include "PetDefines.h"
#include "Player.h"
#include "SharedDefines.h"

#include <atomic>
#include <string>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "slice/SliceLog.h"
#endif

namespace TortoiseBots
{
namespace autohunterpet
{
namespace
{

std::atomic<bool> g_enabled{ true };

void ReadStableState(Player const* bot, bool& hasActivePet, bool& hasStabledPet)
{
    hasActivePet = false;
    hasStabledPet = false;
    PetStable const* stable = bot ? bot->GetPetStable() : nullptr;
    if (!stable)
        return;
    for (Optional<PetStable::PetInfo> const& info : stable->ActivePets)
    {
        if (info)
        {
            hasActivePet = true;
            break;
        }
    }
    for (Optional<PetStable::PetInfo> const& info : stable->StabledPets)
    {
        if (info)
        {
            hasStabledPet = true;
            break;
        }
    }
}

} // namespace

void LoadConfig()
{
    g_enabled = sConfigMgr->GetBoolDefault("TortoiseBots.AutoHunterPet", true);
}

bool Enabled()
{
    return g_enabled;
}

bool Sync(Player* bot, char const* why)
{
    if (!bot)
        return false;

    Input in;
    in.enabled = Enabled();
    in.isBot = BotMgr::Instance().IsBot(bot->GetGUID());
    in.isHunter = bot->GetClass() == CLASS_HUNTER;
    in.level = bot->GetLevel();
    in.inWorld = bot->IsInWorld() && bot->FindMap() != nullptr;
    in.teleportingFar = bot->IsBeingTeleportedFar();
    in.hasLivePet = bot->GetPet() != nullptr || !bot->GetPetGUID().IsEmpty();
    ReadStableState(bot, in.hasActivePet, in.hasStabledPet);

    if (Decide(in) != Verdict::Grant)
        return false;

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
    factory.InitPet();
    if (Pet* pet = bot->GetPet())
    {
        factory.InitPetSpells();
        ai::slice::LogInfo("auto hunter pet: " + bot->GetName() + " got starter pet " +
            std::to_string(pet->GetEntry()) + " (level " + std::to_string(uint32(bot->GetLevel())) + ") on " +
            (why ? why : "sync"));
        return true;
    }
#else
    (void)why;
#endif
    return false;
}

} // namespace autohunterpet
} // namespace TortoiseBots
