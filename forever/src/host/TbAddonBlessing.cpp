// The `blessing` addon verb (TBF #133 piece c-blessing): per-class and
// per-player paladin blessing orders, `auto` reset, and `status` (`TBM:BLESSING`),
// gated by `TbPlayerAuthPure.h` (`Tier::Owner` for changes, `Tier::Tactical` for
// status) and persisted via `manual saved string` + `sPlayerbotDbStore.Save(ai)`.
#include "host/TbAddonBlessing.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonBlessingPure.h"

#include "compat/botpch_forever.h"
#include "host/TbAddonChannel.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"
#include "runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/strategy/AiObjectContext.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace blessing
{
namespace
{
std::uint32_t CallerBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

bool BotAllowed(player::Caller const& caller, std::uint32_t bnet, Player* requester, Player* bot, player::Tier tier)
{
    return bot && bot != requester && bot->IsInWorld() &&
        player::Decide(caller, FactsFor(requester, bot, bnet), tier) == player::Verdict::Allowed;
}

std::string ReadSavedString(AiObjectContext* ctx, std::string const& key)
{
    if (!ctx)
        return std::string();
    auto* const value = ctx->GetValue<std::string>("manual saved string", key);
    return value ? value->Get() : std::string();
}

void EmitBlessingLine(Player* requester, Player* bot, PlayerbotAI* ai)
{
    AiObjectContext* const ctx = ai->GetAiObjectContext();
    std::array<std::string, ai::kBlessingOrderClassCount> classStored{};
    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
        classStored[i] = ReadSavedString(ctx, ai::BlessingOrderKey(i));

    std::array<bool, ai::kBlessingOrderBlessingCount> known{};
    for (int i = 0; i < ai::kBlessingOrderBlessingCount; ++i)
        known[i] = ai->HasSpell(ai::BlessingOrderSpell(ai::BlessingOrderWord(i)));

    std::string const playersStored = ReadSavedString(ctx, ai::BlessingOrderPlayersKey());
    addon::Send(requester, FormatBlessingLine(addon::Safe(bot->GetName()), classStored, known, playersStored));
}

void HandleBlessingVerb(Player* requester, std::string_view args)
{
    if (!requester || !requester->GetSession())
        return;

    ParsedBlessingArgs const parsed = ParseBlessingArgs(args);
    char const* const intent = BlessingIntent(parsed);
    if (!parsed.valid)
    {
        addon::Err(requester, intent, "invalid", BlessingUsage());
        return;
    }

    WorldSession* const session = requester->GetSession();
    player::Caller caller;
    caller.gm = session->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    std::uint32_t const bnet = CallerBnet(requester);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();
    player::Tier const tier = parsed.status ? player::Tier::Tactical : player::Tier::Owner;

    std::vector<Player*> bots;
    if (parsed.scopeIsParty)
    {
        if (Group* group = requester->GetGroup())
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                Player* bot = ObjectAccessor::FindConnectedPlayer(slot.guid);
                if (BotAllowed(caller, bnet, requester, bot, tier))
                    bots.push_back(bot);
            }
        }
        std::sort(bots.begin(), bots.end(),
            [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
        if (bots.empty())
        {
            addon::Err(requester, intent, "no-bot", "No live owned party bots are controllable.");
            return;
        }
    }
    else
    {
        Player* bot = ObjectAccessor::FindPlayerByName(parsed.scope);
        if (!BotAllowed(caller, bnet, requester, bot, tier))
        {
            addon::Err(requester, intent, "no-bot", "Unknown or uncontrollable bot name.");
            return;
        }
        bots.push_back(bot);
    }

    std::string const ackScope = parsed.scopeIsParty ? "party" : std::string("bot:") + bots.front()->GetName();
    std::uint32_t paladins = 0;
    bool refused = false;
    bool notInGroupReported = false;
    std::string ackPlayerName = parsed.targetName;
    std::string const spell = ai::BlessingOrderSpell(parsed.blessingWord);

    for (Player* bot : bots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || bot->GetClass() != CLASS_PALADIN)
            continue;
        if (!parsed.status)
        {
            AiObjectContext* const ctx = ai->GetAiObjectContext();
            if (parsed.playerMode)
            {
                std::string canonicalName;
                if (Group* group = bot->GetGroup())
                {
                    for (Group::MemberSlot const& slot : group->GetMemberSlots())
                    {
                        if (ai::BlessingOrderNamesEqual(slot.name, parsed.targetName))
                        {
                            canonicalName = slot.name;
                            break;
                        }
                    }
                }
                if (canonicalName.empty())
                {
                    if (!notInGroupReported)
                    {
                        addon::Err(requester, intent, "not-in-group",
                            parsed.targetName + " is not in the paladin's group.");
                        notInGroupReported = true;
                    }
                    refused = true;
                    continue;
                }
                if (!spell.empty() && !ai->HasSpell(spell))
                {
                    addon::Err(requester, intent, "unknown-spell",
                        bot->GetName() + " has not trained " + spell + ".");
                    refused = true;
                    continue;
                }
                auto* const value = ctx ? ctx->GetValue<std::string>("manual saved string", ai::BlessingOrderPlayersKey()) : nullptr;
                if (!value)
                {
                    addon::Err(requester, intent, "unsupported", "Choice store is not registered.");
                    return;
                }
                value->Set(ai::SetBlessingPlayerOrder(value->Get(), canonicalName, parsed.blessingWord));
                ackPlayerName = canonicalName;
            }
            else
            {
                if (!spell.empty() && !ai->HasSpell(spell))
                {
                    addon::Err(requester, intent, "unknown-spell",
                        bot->GetName() + " has not trained " + spell + ".");
                    refused = true;
                    continue;
                }
                auto* const value = ctx ? ctx->GetValue<std::string>("manual saved string", ai::BlessingOrderKey(parsed.classIndex)) : nullptr;
                if (!value)
                {
                    addon::Err(requester, intent, "unsupported", "Choice store is not registered.");
                    return;
                }
                value->Set(parsed.blessingWord);
            }
            sPlayerbotDbStore.Save(ai);
        }
        EmitBlessingLine(requester, bot, ai);
        ++paladins;
    }

    if (parsed.status)
    {
        addon::Ack(requester, intent, ackScope, paladins, "-");
        return;
    }
    if (!paladins)
    {
        if (!refused)
            addon::Err(requester, intent, "no-paladin", "No paladin bots in scope could take that order.");
        return;
    }
    addon::Ack(requester, intent, ackScope, paladins, FormatBlessingAckDetail(parsed, ackPlayerName));
}
} // namespace

void RegisterAddonVerbsBlessing()
{
    addon::RegisterVerb("blessing", &HandleBlessingVerb);
    addon::AdvertiseCap("blessing");
    addon::AdvertiseCap("blessing-player");
}

bool HandleBlessingChat(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    Player* requester = handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        if (handler)
            handler->SendSysMessage("TortoiseBots: .tb blessing requires an in-game player session.");
        return false;
    }
    HandleBlessingVerb(requester, std::string_view(args));
    return true;
}

} // namespace blessing
} // namespace TortoiseBots

#else

#include "Chat.h"

namespace TortoiseBots
{
namespace blessing
{
void RegisterAddonVerbsBlessing() { }

bool HandleBlessingChat(ChatHandler* handler, Trinity::ChatCommands::Tail)
{
    if (handler)
        handler->SendSysMessage("error code=engine-not-built");
    return false;
}
} // namespace blessing
} // namespace TortoiseBots

#endif
