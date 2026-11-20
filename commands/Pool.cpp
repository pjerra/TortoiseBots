// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/AltBotPolicy.h"
#include "../runtime/AltBotService.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireRaidPolicy.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/HireLevelPolicy.h"
#include "../runtime/HireTierRequestPolicy.h"
#include "BotCommandContext.h"
#include "../behavior/PlayerConvenience.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/AhMarketService.h"
#include "../host/ModuleVersion.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../runtime/RandomBotPoolReset.h"
#include "../runtime/RandomBotService.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotDbStore.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/PullOpeningPolicy.h"
#include "../ai/playerbot/PullStartPolicy.h"
#include "../ai/playerbot/PullRefusalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/RtiTargetValue.h"
#include "../ai/playerbot/strategy/values/AttackersValue.h"
#include "../ai/playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/actions/EquipAction.h"
#include "../ai/playerbot/strategy/actions/UnequipAction.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotFactory.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/SurvivePolicy.h"
#include "runtime/OwnedBotQolPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../ai/playerbot/AiFactory.h"
#include "../runtime/RemoteCommanderView.h"
#include "../runtime/RemoteCommanderTasks.h"
#include "../runtime/RemoteCommanderEnter.h"
#include "../runtime/CommanderTelemetry.h"
#include "../ai/playerbot/LootObjectStack.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemGroupPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
#include "../ai/playerbot/TbCompatW3.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/PathFinder.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Maps/Cell.h"
#include "Maps/Map.h"
#include "Database/DBCStores.h"
#include "Database/SQLStorages.h"
#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spell.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Threat/ThreatManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <cstring>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

// ---- Managed random-bot pool (issue #265) ---------------------------------
// The module's AllCommandScript intercepts `.bot` before the core command
// table can protect a subcommand, so every permission check below is local.
// Adoption and account listing are server-console-only: no player, addon
// message, hired bot or headless session can reach them.
static bool IsServerConsole(ChatHandler* handler)
{
    if (!handler || handler->GetSession())
        return false;
#ifdef MANGOSSERVER_CHAT_H
    // Remote-access and Discord commands also use null-session CliHandler
    // instances, carrying the calling account's security level. ChatHandler
    // keeps GetAccessLevel() protected, so the concrete handler type is the only
    // way to read it: only the stdin console (and operator-written
    // `pending_commands` rows, which the core also queues as SEC_CONSOLE) carries
    // SEC_CONSOLE, so a remote user of any account rank is rejected here.
    CliHandler* cli = dynamic_cast<CliHandler*>(handler);
    return cli && cli->GetAccessLevel() == SEC_CONSOLE;
#else
    return true; // lens stub: the real build always takes the branch above
#endif
}

static bool RequirePoolAdmin(ChatHandler* handler)
{
    if (IsServerConsole(handler))
        return true;
    WorldSession* session = handler ? handler->GetSession() : nullptr;
    if (!session || !session->GetPlayer())
    {
        handler->PSendSysMessage("You must be in-game or at the server console.");
        return false;
    }
    if (session->GetSecurity() < SEC_GAMEMASTER)
    {
        handler->PSendSysMessage("Your account may not inspect the bot pool.");
        return false;
    }
    return true;
}

static bool RequireServerConsole(ChatHandler* handler)
{
    if (IsServerConsole(handler))
        return true;
    handler->PSendSysMessage("This command is only available at the server console.");
    return false;
}

static bool HandlePoolStatus(ChatHandler* handler)
{
    if (!RequirePoolAdmin(handler))
        return true;

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    RandomBotPoolReset& reset = RandomBotPoolReset::Instance();

    // Parsed from the module's startup configuration (aiplayerbot.conf is read
    // once at startup, so `.reload config` cannot change what a reset does).
    // This reports the configured mode even when the service never planned one.
    PoolResetSetting setting = ParsePoolResetSetting(sPlayerbotAIConfig.randomBotPoolReset);
    std::string mode = PoolResetModeName(setting.mode);
    if (setting.mode == PoolResetMode::Once)
        mode += ":" + setting.token;
    if (setting.mode == PoolResetMode::Invalid)
        mode += " (" + setting.error + ")";

    // Live count of the characters on managed pool accounts. A failed count is
    // reported as unavailable rather than as zero.
    uint32 managedCharacters = 0;
    std::string countError;
    bool haveCharacterCount = registry.CountManagedCharacters(managedCharacters, countError);

    handler->PSendSysMessage("TortoiseBots pool status:");
    handler->PSendSysMessage("  reset setting      : %s", mode.c_str());
    if (setting.mode == PoolResetMode::Once)
        handler->PSendSysMessage("  requested generation: %s", setting.token.c_str());
    handler->PSendSysMessage("  applied generation : %s",
        reset.AppliedGeneration().empty() ? "(none)" : reset.AppliedGeneration().c_str());
    handler->PSendSysMessage("  managed accounts   : %u%s",
        static_cast<uint32>(registry.AccountCount()),
        registry.IsValidated() ? "" : " (registry NOT validated)");
    if (haveCharacterCount)
        handler->PSendSysMessage("  managed characters : %u", managedCharacters);
    else
        handler->PSendSysMessage("  managed characters : (count unavailable: %s)", countError.c_str());
    handler->PSendSysMessage("  reset phase        : %s", reset.PhaseName());
    if (reset.TargetCount() && reset.Phase() == PoolResetPhase::SettlingAuctions)
        handler->PSendSysMessage("  reset progress     : %u/%u characters' auctions settled",
            reset.SettledAuctionOwnerCount(), reset.TargetCount());
    else if (reset.TargetCount())
        handler->PSendSysMessage("  reset progress     : %u/%u characters deleted",
            reset.ProcessedCount(), reset.TargetCount());
    // The service is the single authority for availability: it combines the
    // registry state with the reset state.
    handler->PSendSysMessage("  pool available     : %s",
        RandomBotService::Instance().IsPoolAvailable() ? "yes" : "no");
    if (!reset.LastFailure().empty())
        handler->PSendSysMessage("  last failure       : %s", reset.LastFailure().c_str());
    return true;
}

static bool HandlePoolAccounts(ChatHandler* handler)
{
    if (!RequireServerConsole(handler))
        return true;

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    if (!registry.IsValidated())
    {
        handler->PSendSysMessage("The managed-account registry could not be read: %s",
            registry.LastError().empty() ? "unknown error" : registry.LastError().c_str());
        return true;
    }

    uint32 total = 0;
    std::string countError;
    bool haveTotal = registry.CountManagedCharacters(total, countError);
    handler->PSendSysMessage("Managed pool accounts: %u, characters: %s",
        static_cast<uint32>(registry.AccountCount()),
        haveTotal ? std::to_string(total).c_str() : "(count unavailable)");
    for (PoolAccount const& account : registry.Accounts())
    {
        uint32 characters = 0;
        auto count = QueryGuardCompat(CharacterPQueryCompat(
            "SELECT COUNT(*) FROM `characters` WHERE `deleteDate` IS NULL AND `account` = '%u'", account.accountId));
        if (count)
            characters = count->Fetch()[0].GetUInt32();
        handler->PSendSysMessage("  #%u %s [%s] - %u character(s)%s",
            account.accountId, account.username.c_str(), RegistrationSourceName(account.source), characters,
            count ? "" : " (character count unavailable)");
    }
    if (!registry.AccountCount())
        handler->PSendSysMessage("  (none; the pool is empty)");
    return true;
}

static bool HandlePoolAdopt(ChatHandler* handler, char const* args)
{
    if (!RequireServerConsole(handler))
        return true;

    std::string sub = Trim(args ? args : "");
    std::string subLower = sub;
    for (char& c : subLower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();

    if (subLower == "preview")
    {
        RandomBotAccountRegistry::AdoptionPreview preview = registry.PreviewLegacyAdoption();
        if (!preview.ok)
        {
            handler->PSendSysMessage("Adoption preview failed: %s", preview.error.c_str());
            return true;
        }

        handler->PSendSysMessage("Accounts matching prefix '%s': %u", preview.prefix.c_str(),
            static_cast<uint32>(preview.accounts.size()));
        for (RandomBotAccountRegistry::LegacyAccount const& account : preview.accounts)
            handler->PSendSysMessage("  #%u %s - %u character(s) %s",
                account.accountId, account.username.c_str(), account.characterCount,
                account.alreadyRegistered ? "[already managed]" : "[NOT managed]");

        if (preview.accounts.empty())
        {
            handler->PSendSysMessage("Nothing to adopt.");
            return true;
        }

        handler->PSendSysMessage("Total: %u account(s), %u character(s); %u account(s) still need adoption.",
            static_cast<uint32>(preview.accounts.size()), preview.totalCharacters, preview.pendingAccounts);
        handler->PSendSysMessage("Adoption registers accounts only; no character is changed or deleted.");
        if (preview.pendingAccounts)
            handler->PSendSysMessage("To enroll them, run exactly: bot pool adopt confirm %s (valid for 5 minutes)",
                preview.challenge.c_str());
        else
            handler->PSendSysMessage("Every matching account is already managed.");
        return true;
    }

    if (subLower.rfind("confirm", 0) == 0)
    {
        std::string challenge = Trim(subLower.substr(7));
        if (challenge.empty())
        {
            handler->PSendSysMessage("Usage: bot pool adopt confirm <challenge> (from 'bot pool adopt preview')");
            return true;
        }

        RandomBotAccountRegistry::AdoptionConfirm confirm = registry.ConfirmLegacyAdoption(challenge, time(nullptr));
        if (!confirm.ok)
        {
            handler->PSendSysMessage("Adoption aborted: %s", confirm.error.c_str());
            return true;
        }
        handler->PSendSysMessage("Adoption complete: %u account(s) registered, %u already managed. No character was changed.",
            confirm.adopted, confirm.alreadyRegistered);
        return true;
    }

    handler->PSendSysMessage("Usage: bot pool adopt preview / bot pool adopt confirm <challenge>");
    return true;
}

bool HandlePool(ChatHandler* handler, char const* args)
{
    std::string sub = Trim(args ? args : "");
    std::string subLower = sub;
    for (char& c : subLower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (subLower == "status")
        return HandlePoolStatus(handler);
    if (subLower == "accounts")
        return HandlePoolAccounts(handler);
    if (subLower.rfind("adopt", 0) == 0)
        return HandlePoolAdopt(handler, subLower.substr(5).c_str());

    handler->PSendSysMessage("Usage: bot pool status / accounts / adopt preview / adopt confirm <challenge>");
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
