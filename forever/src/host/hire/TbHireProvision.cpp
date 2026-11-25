// A <Mercenary Hire> companion on ForeverCore (issue #298): the gates, the fee, a new character on a
// hire account, its deferred level/spec/gear provisioning, the login bound to the hirer and the party
// intro. Contract: forever/src/host/hire/TbHire.h; decisions: TbHireProvisionPure.h.
//
// Creation is asynchronous (BotMgr::CreateCharacter commits the character on a later tick), so Hire()
// only asks for the character and Update() finishes the job: confirm the row, write the ledger, charge,
// log in, provision, group. Every failure on that road undoes the hire and returns the fee.
#include "host/hire/TbHire.h"
#include "host/hire/TbHireProvision.h"
#include "host/hire/TbHireProvisionPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/hire/TbHireAccounts.h"   // P1: a hire account with room for one more character
#include "host/gear/TbGearGen.h"
#include "host/TbTalents.h"

#include "runtime/HireLifecycle.h"
#include "runtime/policy/hire/HirePvpPolicy.h"
#include "runtime/PlayerbotAIStorage.h"
#include "runtime/BotNameService.h"

#include "host/TbAddonChannel.h"
#include "host/TbAddonInfoPure.h"

#include "TbBotMgr.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "ChatPackets.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

#include "playerbot/PlayerbotAIConfig.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace hire
{
namespace
{

// Straight to the core logger: the compat shim above replaces the legacy `sLog` the TC_LOG_ macros expand, so they do
// not compile in a file that includes it.
template <typename... Args>
void LogAt(char const* logger, LogLevel level, fmt::format_string<Args...> message, Args&&... args)
{
    Log::instance()->OutMessage(logger, level, message, std::forward<Args>(args)...);
}

// How long a mustering hire may take before it is undone. Creation and login both run on the world
// tick; two minutes covers a slow character commit and a busy login queue without pinning a slot.
constexpr time_t kPendingTimeoutSec = 120;
constexpr uint32_t kUpdateStepMs = 250;
constexpr uint32_t kProvisionPerStep = 2;

struct Pending
{
    ObjectGuid master;
    uint32_t ownerAccountId = 0;      // the hirer's game account, the ledger's owner
    uint32_t characterAccountId = 0;  // the hire account P1 made
    std::string name;
    uint8_t classId = 0;
    uint8_t race = 0;
    uint8_t gender = 0;
    uint8_t role = 0;
    int specIndex = -1;
    int specTab = -1;
    uint8_t gearTier = 0;
    uint8_t level = 1;
    uint32_t cost = 0;
    ObjectGuid botGuid;
    bool claimed = false;
    bool charged = false;
    bool provisioned = false;
    time_t started = 0;
};

std::vector<Pending> g_pending;
uint32_t g_elapsedMs = 0;

HireFeeConfig FeeConfig()
{
    HireFeeConfig config;
    config.cost.baseCopper = sPlayerbotAIConfig.hireBaseCostCopper;
    config.cost.mult2 = sPlayerbotAIConfig.hirePartyMult2;
    config.cost.mult3 = sPlayerbotAIConfig.hirePartyMult3;
    config.cost.mult4 = sPlayerbotAIConfig.hirePartyMult4;
    config.cost.raidFlatCopper = sPlayerbotAIConfig.hireRaidFlatCostCopper;
    for (uint8_t tier = 0; tier <= kHireGearTierMax; ++tier)
        config.tierMult[tier] = sPlayerbotAIConfig.hireGearTierCostMult[tier];
    return config;
}

bool IsGm(Player const* player)
{
    return player && player->GetSession() && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
}

int SecurityOf(Player const* player)
{
    return player && player->GetSession() ? static_cast<int>(player->GetSession()->GetSecurity()) : 0;
}

// Live hires of this master plus the ones still mustering (they hold a slot before they are in world).
uint32_t ActiveHires(ObjectGuid master)
{
    uint32_t count = 0;
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        if (BotMgr::Instance().MasterOf(guid) == master && HireLifecycle::Instance().IsHired(guid))
            ++count;
    for (Pending const& pending : g_pending)
        if (pending.master == master && (pending.botGuid.IsEmpty() || !BotMgr::Instance().IsBot(pending.botGuid)))
            ++count;
    return count;
}

std::string MoneyText(uint32_t copper)
{
    uint32_t const gold = copper / 10000;
    uint32_t const silver = (copper % 10000) / 100;
    uint32_t const rest = copper % 100;
    std::string text;
    if (gold)
        text += std::to_string(gold) + "g ";
    if (silver || gold)
        text += std::to_string(silver) + "s ";
    text += std::to_string(rest) + "c";
    return text;
}

std::string ClassName(uint8_t classId)
{
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
    if (entry && entry->Name[LOCALE_enUS])
        return entry->Name[LOCALE_enUS];
    return "class " + std::to_string(classId);
}

std::string RoleWord(uint8_t role)
{
    if (role == kHireRoleTank)
        return "tank";
    if (role == kHireRoleHealer)
        return "healer";
    return "damage";
}

bool CrossFactionHireEnabled()
{
    return sPlayerbotAIConfig.hireCrossFactionNeutralInns &&
        sWorld.getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP);
}

void Notify(ObjectGuid master, std::string const& text)
{
    Player* player = ObjectAccessor::FindPlayer(master);
    if (player && player->GetSession() && player->IsInWorld())
        ChatHandler(player->GetSession()).SendSysMessage(text);
}

// The module's own freshly created character, before any ledger row exists: deleting it is the only
// road back. The account is the one the character was made on, so nothing else can be hit.
void DeleteUnclaimed(ObjectGuid guid, uint32_t accountId)
{
    if (guid.IsEmpty() || !accountId)
        return;
    Player::DeleteFromDB(guid, accountId, true, true);
    LogAt("tortoisebots", LOG_LEVEL_INFO, "hire: unclaimed companion {} on account {} removed", guid.ToString(), accountId);
}

void Refund(Pending const& pending)
{
    if (!pending.charged || !pending.cost)
        return;
    Player* hirer = ObjectAccessor::FindPlayer(pending.master);
    if (hirer && hirer->IsInWorld() && hirer->GetSession())
    {
        hirer->ModifyMoney(int64_t(pending.cost));
        hirer->SaveToDB();
        return;
    }
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft("Your hire fee", "The hire of " + pending.name + " did not complete. Your fee is returned.")
        .AddMoney(pending.cost)
        .SendMailTo(trans, MailReceiver(pending.master.GetCounter()), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
    CharacterDatabase.CommitTransaction(trans);
}

// Undo a hire that cannot finish: dismiss the ledger-owned hire (its deletion is the lifecycle's, with
// the three guards), or delete the unclaimed character directly; refund once.
void Abort(Pending& pending, std::string const& why)
{
    LogAt("tortoisebots", LOG_LEVEL_INFO, "hire: {} of {} aborted: {}", pending.name,
        pending.master.ToString(), why);
    if (DecideHireRollback(pending.claimed) == HireRollback::RefundAndDismiss)
        HireLifecycle::Instance().DismissForRefund(pending.botGuid, "the hire could not be provisioned");
    else
        DeleteUnclaimed(pending.botGuid, pending.characterAccountId);
    Refund(pending);
    Notify(pending.master, "Your hire of " + pending.name + " could not be completed: " + why + "." +
        (pending.charged ? " Your fee was returned." : " No gold was taken."));
}

void Provision(Player* bot, Pending const& pending)
{
    if (bot->GetLevel() != pending.level)
    {
        if (pending.level > bot->GetLevel())
            bot->GiveLevel(pending.level);
        else
        {
            bot->SetLevel(pending.level);
            bot->InitStatsForLevel(false);
        }
    }
    bot->SetXP(0);
    // The forced tab was stored before the login, so the level's own talent sync already spent the
    // points on it; this pass is the idempotent catch-up.
    talents::Sync(bot, "hire");
    std::string const specArg = GearSpecWord(bot->GetClass(), pending.specTab, pending.role);
    gear::SetGearForTarget(bot, specArg, pending.level, -1);
    if (pending.gearTier > 0 && !EquipTierSet(bot, pending.gearTier, pending.specIndex))
        LogAt("tortoisebots", LOG_LEVEL_INFO, "hire: no tier {} set for {} ({}); the levelled kit stays",
            pending.gearTier, bot->GetName(), specArg.empty() ? "auto" : specArg);
    bot->SaveToDB();
}

void SendIntro(Player* bot, Pending const& pending)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;
    std::string text = bot->GetName() + " reporting for duty: " + RoleWord(pending.role) + " " +
        ClassName(bot->GetClass()) + ", level " + std::to_string(bot->GetLevel()) + ".";
    WorldPackets::Chat::Chat packet;
    packet.Initialize(CHAT_MSG_PARTY, LANG_UNIVERSAL, bot, nullptr, text);
    group->BroadcastPacket(packet.Write(), true);
}

// One step of a mustering hire. True when the entry is finished (joined or aborted).
bool StepCreate(Pending& pending)
{
    if (BotMgr::Instance().CreationInFlight(pending.name))
        return false;

    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(pending.name);
    if (guid.IsEmpty())
    {
        // The character commit failed: nothing was charged yet.
        LogAt("tortoisebots", LOG_LEVEL_ERROR, "hire: the character {} was never created on account {}",
            pending.name, pending.characterAccountId);
        Notify(pending.master, "Your companion could not be created. No gold was taken.");
        return true;
    }
    uint32_t const accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (accountId != pending.characterAccountId)
    {
        LogAt("tortoisebots", LOG_LEVEL_ERROR, "hire: {} exists on account {} instead of the hire account {}; not touched",
            pending.name, accountId, pending.characterAccountId);
        Notify(pending.master, "Your companion could not be created. No gold was taken.");
        return true;
    }
    pending.botGuid = guid;

    Player* hirer = ObjectAccessor::FindPlayer(pending.master);
    if (!hirer || !hirer->IsInWorld())
        return false;   // wait for the hirer (the timeout undoes the hire)
    if (pending.cost && hirer->GetMoney() < pending.cost)
    {
        DeleteUnclaimed(guid, pending.characterAccountId);
        Notify(pending.master, "You cannot afford that companion (" + MoneyText(pending.cost) + ").");
        return true;
    }
    if (!HireLifecycle::Instance().Claim(guid, pending.master, pending.ownerAccountId, pending.characterAccountId))
    {
        DeleteUnclaimed(guid, pending.characterAccountId);
        Notify(pending.master, "Could not record your hire. No gold was taken.");
        return true;
    }
    pending.claimed = true;
    HireLifecycle::Instance().SetGearTier(guid, pending.gearTier);

    // The brain the login builds reads the forced role and spec tab from the store.
    PlayerbotAIStorage::Instance().SetPlayerForcedRole(guid, pending.role);
    if (pending.specTab >= 0)
        PlayerbotAIStorage::Instance().SetPlayerForcedSpecTab(guid, static_cast<int8>(pending.specTab));

    // Charged only now, with the character confirmed and the ledger written.
    if (pending.cost)
    {
        hirer->ModifyMoney(-int64_t(pending.cost));
        hirer->SaveToDB();
        pending.charged = true;
    }
    std::string const error = BotMgr::Instance().Login(guid, pending.master, false, false, false, true);
    if (!error.empty())
    {
        Abort(pending, error);
        return true;
    }
    return false;
}

bool StepLogin(Pending& pending)
{
    Player* bot = BotMgr::Instance().FindBot(pending.botGuid);
    switch (DecideLoginWait(bot != nullptr, BotMgr::Instance().IsBot(pending.botGuid),
        time(nullptr) - pending.started > kPendingTimeoutSec))
    {
        case LoginWait::Failed:
            Abort(pending, "its login did not start");
            return true;
        case LoginWait::TimedOut:
            Abort(pending, "its login did not finish in time");
            return true;
        case LoginWait::Loading:
            return false;
        case LoginWait::InWorld:
            break;
    }
    if (!pending.provisioned)
    {
        Provision(bot, pending);
        pending.provisioned = true;
    }

    Player* master = ObjectAccessor::FindPlayer(pending.master);
    Group* group = bot->GetGroup();
    bool const grouped = group && master && master->GetGroup() == group;
    if (grouped)
    {
        HireLifecycle::Instance().MarkGrouped(pending.botGuid);
        SendIntro(bot, pending);
        return true;
    }
    if (time(nullptr) - pending.started > kPendingTimeoutSec)
    {
        Abort(pending, "it did not join your party");
        return true;
    }
    return false;
}

bool Step(Pending& pending)
{
    // Creation is not cancellable: the character commit answers on a later tick. A commit that never
    // answers leaves nothing charged; the character, if it lands afterwards, is on a hire account P1
    // reuses and has no ledger row, so it is never a companion.
    if (!pending.claimed && BotMgr::Instance().CreationInFlight(pending.name))
    {
        if (time(nullptr) - pending.started > kPendingTimeoutSec)
        {
            LogAt("tortoisebots", LOG_LEVEL_ERROR, "hire: the character {} did not commit in time; nothing was charged", pending.name);
            return true;
        }
        return false;
    }
    if (time(nullptr) - pending.started > kPendingTimeoutSec)
    {
        Abort(pending, "it did not arrive in time");
        return true;
    }
    return pending.claimed ? StepLogin(pending) : StepCreate(pending);
}

} // namespace

bool Enabled()
{
    return BotMgr::Instance().Enabled() && sPlayerbotAIConfig.hireEnabled;
}

bool HireAnywhere()
{
    return sPlayerbotAIConfig.hireAnywhere;
}

uint32_t QuoteFee(Player* hirer, uint8_t level, uint8_t gearTier)
{
    if (!hirer)
        return 0;
    HireLevelResult const resolved = ResolveHireLevel(hirer->GetLevel(), level,
        sPlayerbotAIConfig.hireMaxLevelAboveHirer, IsGm(hirer));
    uint8_t const tier = EffectiveGearTier(sPlayerbotAIConfig.hireGearTiersEnabled, resolved.level, gearTier);
    uint32_t const quoted = QuoteHireFee(FeeConfig(), ActiveHires(hirer->GetGUID()), resolved.level, tier);
    return ChargeableHireFee(quoted, SecurityOf(hirer), sPlayerbotAIConfig.hireFreeMinSecurity);
}

Outcome Hire(Player* hirer, Selection const& selection, Creature* recruiter)
{
    Outcome outcome;
    if (!hirer || !hirer->GetSession() || !hirer->IsInWorld())
    {
        outcome.status = Status::Failed;
        outcome.message = "You must be in-game to hire a companion.";
        return outcome;
    }
    if (BotMgr::Instance().IsBot(hirer->GetGUID()))
    {
        outcome.status = Status::Failed;
        outcome.message = "Only a player can hire a companion.";
        return outcome;
    }

    uint8_t const classId = selection.classId;
    if (!classId || !selection.race || selection.gender > 1)
    {
        outcome.status = Status::InvalidChoice;
        outcome.message = "Choose a class, race, and gender first.";
        return outcome;
    }
    if (!sObjectMgr.GetPlayerInfo(selection.race, classId))
    {
        outcome.status = Status::InvalidChoice;
        outcome.message = "That race cannot be that class.";
        return outcome;
    }
    Team const hireTeam = Player::TeamForRace(selection.race);
    if (hireTeam != hirer->GetTeam() && !(selection.anyFaction && CrossFactionHireEnabled()))
    {
        outcome.status = Status::InvalidChoice;
        outcome.message = "That race fights for the other faction.";
        return outcome;
    }
    uint8_t const role = selection.role ? selection.role : DefaultRoleForClass(classId);
    if (!ClassCanRole(classId, role))
    {
        outcome.status = Status::InvalidChoice;
        outcome.message = "That class cannot fill that role.";
        return outcome;
    }
    if (classId == kHireClassShaman && role == kHireRoleTank)
    {
        if (!sPlayerbotAIConfig.shamanTankEnabled)
        {
            outcome.status = Status::InvalidChoice;
            outcome.message = "The experimental shaman tank is turned off on this server.";
            return outcome;
        }
        if (hirer->GetLevel() < 10)
        {
            outcome.status = Status::InvalidChoice;
            outcome.message = "Enhancement (Tank) needs level 10 (Earthshaker Slam taunt). Level up and hire again.";
            return outcome;
        }
    }
    int const specTab = SpecTabForIndex(classId, selection.specIndex);

    HireLevelResult const levelChoice = ResolveHireLevel(hirer->GetLevel(), selection.level,
        sPlayerbotAIConfig.hireMaxLevelAboveHirer, IsGm(hirer));
    uint32_t const level = levelChoice.level;
    uint8_t const tier = EffectiveGearTier(sPlayerbotAIConfig.hireGearTiersEnabled, level, selection.gearTier);
    uint8_t highest = 0;
    if (tier > 0)
        highest = HighestUnlockedTier(hirer);

    Group* group = hirer->GetGroup();
    HireGateFacts facts;
    facts.realPlayer = true;
    facts.enabled = Enabled();
    facts.accountAllowed = SecurityOf(hirer) >= static_cast<int>(sPlayerbotAIConfig.hireMinAccountSecurity);
    facts.pvpAllowedNow = MayHireForPvpState(sPlayerbotAIConfig.hireAllowPvP,
        hirer->InBattleground(), hirer->InBattlegroundQueue());
    facts.hasRecruiter = recruiter != nullptr;
    facts.hireAnywhere = HireAnywhere();
    facts.tiersEnabled = sPlayerbotAIConfig.hireGearTiersEnabled;
    facts.requestedTier = tier;
    facts.highestUnlockedTier = highest;
    facts.hireLevel = level;
    facts.activeHires = ActiveHires(hirer->GetGUID());
    facts.maxHires = sPlayerbotAIConfig.hireMaxBotsPerPlayer;
    facts.groupMembers = group ? group->GetMembersCount() : 0;
    facts.groupIsRaid = group && group->isRaidGroup();
    uint32_t const cost = ChargeableHireFee(QuoteHireFee(FeeConfig(), facts.activeHires, level, tier),
        SecurityOf(hirer), sPlayerbotAIConfig.hireFreeMinSecurity);
    facts.canAfford = hirer->GetMoney() >= cost;

    HireGateResult const gate = DecideHireGates(facts);
    if (gate.verdict != HireVerdict::Ok)
    {
        switch (gate.verdict)
        {
            case HireVerdict::Disabled: outcome.status = Status::Disabled; break;
            case HireVerdict::NoPermission: outcome.status = Status::NoPermission; break;
            case HireVerdict::RecruiterRequired: outcome.status = Status::RecruiterRequired; break;
            case HireVerdict::GroupFull: outcome.status = Status::GroupFull; break;
            case HireVerdict::CapReached: outcome.status = Status::CapReached; break;
            case HireVerdict::Poor: outcome.status = Status::Poor; break;
            case HireVerdict::TierLocked: outcome.status = Status::TierLocked; break;
            default: outcome.status = Status::Failed; break;
        }
        outcome.message = gate.message;
        if (gate.verdict == HireVerdict::Poor)
            outcome.message = "You cannot afford that companion (" + MoneyText(cost) + ").";
        if (gate.verdict == HireVerdict::TierLocked)
            outcome.message = "That equipment is not unlocked yet. Your highest available tier is " +
                TierName(highest) + " (tier " + std::to_string(highest) + ").";
        outcome.cost = cost;
        return outcome;
    }

    std::string accountError;
    uint32_t const characterAccountId = AcquireHireAccount(accountError);
    if (!characterAccountId)
    {
        outcome.status = Status::Failed;
        outcome.message = "No mercenary account is available right now. Try again shortly.";
        LogAt("tortoisebots", LOG_LEVEL_ERROR, "hire: no hire account for {} ({}): {}", hirer->GetName(),
            hirer->GetGUID().ToString(), accountError.empty() ? "no reason given" : accountError);
        return outcome;
    }

    std::string name;
    std::string createError = "no usable character name came up";
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        name = PickBotCharacterName(selection.race, classId, selection.gender);
        if (name.empty())
            continue;
        createError = BotMgr::Instance().CreateCharacter(characterAccountId, name, selection.race, classId, selection.gender);
        if (createError.empty())
            break;
        name.clear();
    }
    if (name.empty())
    {
        outcome.status = Status::Failed;
        outcome.message = "No mercenary of that kind is available right now. Try again shortly.";
        LogAt("tortoisebots", LOG_LEVEL_ERROR, "hire: character creation for {} failed: {}", hirer->GetName(), createError);
        return outcome;
    }

    Pending pending;
    pending.master = hirer->GetGUID();
    pending.ownerAccountId = hirer->GetSession()->GetAccountId();
    pending.characterAccountId = characterAccountId;
    pending.name = name;
    pending.classId = classId;
    pending.race = selection.race;
    pending.gender = selection.gender;
    pending.role = role;
    pending.specIndex = selection.specIndex;
    pending.specTab = specTab;
    pending.gearTier = tier;
    pending.level = static_cast<uint8_t>(level);
    pending.cost = cost;
    pending.started = time(nullptr);
    g_pending.push_back(pending);

    outcome.status = Status::Ok;
    outcome.cost = cost;
    outcome.botName = name;
    outcome.message = "Hired " + name + " for " + (cost ? MoneyText(cost) : "free") +
        ". Your companion is mustering.";
    if (selection.level)
    {
        outcome.message += " Level " + std::to_string(level) + ".";
        if (levelChoice.clamped)
            outcome.message += " The level you asked for is above your limit (" +
                std::to_string(levelChoice.maxAllowed) + ").";
    }
    return outcome;
}

void Update(uint32_t diff)
{
    if (g_pending.empty())
        return;
    g_elapsedMs += diff;
    if (g_elapsedMs < kUpdateStepMs)
        return;
    g_elapsedMs = 0;

    uint32_t finished = 0;
    for (auto it = g_pending.begin(); it != g_pending.end() && finished < kProvisionPerStep;)
    {
        // The hire ended while mustering (dismissed, collapsed ledger): nothing left to provision.
        if (it->claimed && !HireLifecycle::Instance().IsHired(it->botGuid))
        {
            it = g_pending.erase(it);
            continue;
        }
        if (Step(*it))
        {
            it = g_pending.erase(it);
            ++finished;
        }
        else
            ++it;
    }
}

void AdvertiseAddonCaps()
{
    if (Enabled())
        addon::AdvertiseCap("hiring");
    if (HireAnywhere())
        addon::AdvertiseCap("hire-anywhere");
}

bool ShamanTankEnabled()
{
    return sPlayerbotAIConfig.shamanTankEnabled;
}

std::vector<std::string> FeatureLines()
{
    std::vector<std::string> lines;
    lines.push_back(addoninfo::FeatureLine("hiring", Enabled() ? "on" : "off",
        "Companion hiring"));
    lines.push_back(addoninfo::FeatureLine("hire-max",
        std::to_string(sPlayerbotAIConfig.hireMaxBotsPerPlayer), "Hires per player"));
    lines.push_back(addoninfo::FeatureLine("hire-pvp", sPlayerbotAIConfig.hireAllowPvP ? "on" : "off",
        "Hires in battlegrounds"));
    lines.push_back(addoninfo::FeatureLine("hire-anywhere", sPlayerbotAIConfig.hireAnywhere ? "on" : "off",
        "Hire anywhere"));
    lines.push_back(addoninfo::FeatureLine("hire-tier", sPlayerbotAIConfig.hireGearTiersEnabled ? "on" : "off",
        "Gear tiers"));
    return lines;
}

} // namespace hire
} // namespace TortoiseBots

#else

// Without the engine nothing is hired and the add-on hears no capability. The contract's declarations
// exist only for the slice build; no option-OFF translation unit calls them (CMakeLists.txt keeps this
// file out of that build).

#endif
