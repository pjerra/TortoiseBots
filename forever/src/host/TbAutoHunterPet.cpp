// Host half of automatic starter hunter pets (TbAutoHunterPetPure.h).
#include "host/TbAutoHunterPet.h"
#include "host/TbAutoHunterPetPure.h"

#include "TbBotMgr.h"

#include "Config.h"
#include "Pet.h"
#include "PetDefines.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <atomic>
#include <string>
#include <vector>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "slice/SliceLog.h"
#else
#include "Log.h"
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

    bool grantedPet = false;
    if (Decide(in) == Verdict::Grant)
    {
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
        factory.InitPet();
        if (Pet* pet = bot->GetPet())
        {
            factory.InitPetSpells();
            ai::slice::LogInfo("auto hunter pet: " + bot->GetName() + " got starter pet " +
                std::to_string(pet->GetEntry()) + " (level " + std::to_string(uint32(bot->GetLevel())) + ") on " +
                (why ? why : "sync"));
            grantedPet = true;
        }
#endif
    }

    std::vector<PetControlSpell> const missing = MissingPetControlSpells(in,
        [bot](uint32_t spellId) { return bot->HasSpell(spellId); });
    for (PetControlSpell const& spell : missing)
    {
        SpellInfo const* info = SpellMgr::instance()->GetSpellInfo(spell.spellId, DIFFICULTY_NONE);
        if (!info || !SpellMgr::IsSpellValid(info, bot, false))
            continue;
        if (bot->HasSpell(spell.spellId))
            continue;
        bot->LearnSpell(spell.spellId, false);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        ai::slice::LogInfo("auto hunter pet: " + bot->GetName() + " learned " + std::to_string(spell.spellId) +
            " " + spell.name + " on " + (why ? why : "sync"));
#else
        TC_LOG_INFO("tortoisebots", "auto hunter pet: {} learned {} {} on {}",
            bot->GetName(), spell.spellId, spell.name, why ? why : "sync");
#endif
    }

    return grantedPet;
}

} // namespace autohunterpet
} // namespace TortoiseBots
