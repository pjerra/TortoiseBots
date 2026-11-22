// Slice 1 (package R6B): the link stand-ins. Compiled only with TORTOISEBOTS_SLICE1 (forever/src/slice/*.cpp).
//
// The 127 slice files reference code that lives in files outside the slice (boss tactics, the travel manager, the loot stack,
// the random item and text managers, ...). Each definition below answers for one such symbol. They are NOT empty bodies that
// silence the linker: every one of them
//   * answers the value the caller already treats as "this feature is not there" (false, null, empty, 0, the neutral
//     multiplier 1.0), the same value the Turtle code returns when its tables hold nothing;
//   * counts itself under a feature of the unported list (ai/playerbot/UnportedFeatures.h), so `.tb unported` and the boot line
//     show which of them the engine actually reached and how often;
//   * is tagged with the file that owns the real definition, and listed in forever/docs/SLICE1-R6B-NOTES.md (section 3).
// If a file named here is ported into the slice, its stand-ins must go: a duplicate symbol at link says so.
//
// Choice (c) of the R6B brief in the notes: the slice does not need the call on the warrior's follow / assist / melee path
// (raid boss rules, travel, looting, pets, trade, guild orders), and routing every call site through a feature helper would
// edit about two hundred sites of shared files, so the symbol is answered here once instead.
#include "compat/botpch_forever.h"

#include "playerbot/playerbot.h"
#include "../../../behavior/PlayerConvenience.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossTacticsStand.h"
#include "playerbot/BotAreaTriggers.h"
#include "playerbot/CastCancelLog.h"
#include "playerbot/ChatFilter.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/FankrissGauntletPolicy.h"
#include "playerbot/GuildOrderEnchants.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "playerbot/PlayerbotTextMgr.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/Talentspec.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/actions/CheatAction.h"
#include "playerbot/strategy/actions/EquipAction.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/actions/FankrissRaidActions.h"
#include "playerbot/strategy/actions/FankrissTankActions.h"
#include "playerbot/strategy/actions/FishAction.h"
#include "playerbot/strategy/actions/PatchwerkSafety.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "playerbot/strategy/actions/PositionAction.h"
#include "playerbot/strategy/actions/QueryItemUsageAction.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/strategy/values/MaintenanceValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "../../../runtime/GuildOrderStore.h"
#include "../../../runtime/GuildOrderAccess.h"
#include "playerbot/strategy/actions/GuildCreateActions.h"
#include "playerbot/strategy/actions/GuildOrderBuyAction.h"
#include "../../../runtime/DowntimeService.h"
#include "playerbot/strategy/triggers/RpgTriggers.h"
#include "playerbot/strategy/actions/TellLosAction.h"
#include "playerbot/strategy/actions/UseMeetingStoneAction.h"
#include "playerbot/strategy/actions/WorldBuffAction.h"
#include "playerbot/strategy/actions/ViscidusActions.h"
#include "playerbot/strategy/generic/ThreatStrategy.h"
#include "playerbot/strategy/triggers/EmeraldSanctumDungeonTriggers.h"
#include "playerbot/strategy/values/FankrissSpawnTarget.h"
#include "playerbot/strategy/values/GrindTargetValue.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/policy/combat/TauntHeldTargetPolicy.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <limits>
#include <cmath>
#include "GameTime.h"

namespace
{
// Boss and raid tactics is reached by per-tick checks in about 40 shared files (target filters, the taunt and hold gates, the anchor clamp), several
// hundred times per bot per tick: 1.9 million calls in 5 minutes with two bots (R8). The checks sit in shared code, so a guard at the call sites would be a
// shared change (and a Turtle proof for each); the stand-ins instead count once per second of world time, so the counter reads "seconds in which a check
// reached the feature" and no longer buries the other rows. (Counting once per world tick, about every millisecond, still gave 1.27 million in 22 minutes.)
inline void NoteBoss()
{
    static std::atomic<uint32_t> lastSecond{ 0xFFFFFFFFu };
    uint32_t const now = GameTime::GetGameTimeMS() / 1000u;
    if (lastSecond.exchange(now) != now)
        tbcompat::Unported(tbcompat::Feature::BossTactics);
}
inline void NoteItems()     { tbcompat::Unported(tbcompat::Feature::RandomItems); }
inline void NoteTexts()     { tbcompat::Unported(tbcompat::Feature::BotTexts); }
inline void NoteSpecs()     { tbcompat::Unported(tbcompat::Feature::TalentSpecs); }
inline void NotePool()      { tbcompat::Unported(tbcompat::Feature::PoolServices); }
inline void NoteFactory()   { tbcompat::Unported(tbcompat::Feature::BotFactory); }
inline void NoteFilters()   { tbcompat::Unported(tbcompat::Feature::ChatFilters); }
inline void NoteMisc()      { tbcompat::Unported(tbcompat::Feature::MiscLegacyServices); }
}

// =====================================================================================================================
// Boss and raid tactics. Owner files: ai/playerbot/strategy/actions/BossTacticsActions.cpp, Fankriss*Actions.cpp,
// PatchwerkSoakerActions.cpp, PatchwerkSafety.cpp, ViscidusActions.cpp, EmeraldSanctumDungeon*.cpp,
// FankrissGauntletPolicy (header), BossTacticsMgr.cpp, BossStandMgr.cpp, DungeonPlanMgr.cpp.
// Answer: no boss rule applies (false / null / 0 / empty); the table getters return an empty table.
// =====================================================================================================================
namespace ai
{
// BossTacticsMgr.cpp / DungeonPlanMgr.cpp are real files now (slice 5): GetBossTactics, GetDungeonPlans, LoadBossTactics and LoadDungeonPlans
// and BossStandMgr.cpp now own their real loaders.

// BossTacticsActions.cpp
// The per-update bracket PlayerbotAI::UpdateAI opens around the scan (PlayerbotAI.cpp:383): it opens no scan slot here, so it is not counted
// (it was the hottest unported counter: once per bot per tick, no feature behind it; R8).
BossTacticsScanScope::BossTacticsScanScope() {}
BossTacticsScanScope::~BossTacticsScanScope() {}
bool AoeTauntStillNeeded(PlayerbotAI*, Player*) { NoteBoss(); return false; }
bool AreaTauntRefusedByPost(PlayerbotAI*, Player*, const char*) { NoteBoss(); return false; }
// R9: the real function answers true when there is no live anchor row (the move is allowed, the point is left as it is); false means "the clamped
// point has no floor", which refuses the move. There is never an anchor here.
bool ClampMoveToBossAnchor(PlayerbotAI*, Player*, float&, float&, float&) { NoteBoss(); return true; }
bool DispelCastBlocked(PlayerbotAI*, Unit*, std::uint32_t) { NoteBoss(); return false; }
BossStandTarget FindBossStand(PlayerbotAI*, Player*) { NoteBoss(); return BossStandTarget{}; }
TankPostHealDuty FindTankPostHealDuty(PlayerbotAI*, Player*) { NoteBoss(); return TankPostHealDuty{}; }
std::uint32_t GateHoldFighters(PlayerbotAI*, std::uint32_t, float) { NoteBoss(); return 0; }
// "Whether a healer may top up the target": with no boss row (no live heal reserve) the real answer is today's mana rule, `mana >= mediumMana`
// (BossTacticsActions.cpp:9550), not "yes" (R9: the old stand-in answered true at any mana).
bool HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit*)
{
    NoteBoss();
    if (!ai || !bot)
        return true;
    std::uint8_t const mediumManaPct = static_cast<std::uint8_t>(std::min<std::uint32_t>(100, sPlayerbotAIConfig.mediumMana));
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE2(std::uint8_t, "mana", "self target") >= mediumManaPct;
}
bool HealReserveRefusesTopUp(PlayerbotAI*, Player*, Unit*, float) { NoteBoss(); return false; }
std::uint32_t HoldKillBlockers(PlayerbotAI*, std::uint32_t) { NoteBoss(); return 0; }
bool IsDpsHeld(PlayerbotAI*, Player*, Unit*, DpsHoldTrigger*) { NoteBoss(); return false; }
bool IsLiveWalkoutPuller(Player*) { NoteBoss(); return false; }
bool IsRangedOnlyHeld(PlayerbotAI*, Player*, Unit*, float*) { NoteBoss(); return false; }
bool IsSeekSourceForTankPickup(PlayerbotAI*, Player*, Unit*) { NoteBoss(); return false; }
bool IsSoughtCloudFor(PlayerbotAI*, Player*, DynamicObject*) { NoteBoss(); return false; }
bool LiveBossAnchor(PlayerbotAI*, Player*, BossAnchorCircle&) { NoteBoss(); return false; }
bool MeetSonChaseExempt(PlayerbotAI*, Player*, Unit*) { NoteBoss(); return false; }
void ReleaseWalkoutPet(Player*) { NoteBoss(); }
BossLinkedDeathScan ScanBossLinkedDeath(PlayerbotAI*, Player*) { NoteBoss(); return BossLinkedDeathScan{}; }
std::uint64_t SeekMeetHealPuller(std::uint32_t) { NoteBoss(); return 0; }
bool SeekMeetPrePlaceHoldsChase(PlayerbotAI*, Player*, Unit*, const char*) { NoteBoss(); return false; }
std::uint64_t SoleKillGuid(PlayerbotAI*, std::uint32_t) { NoteBoss(); return 0; }
bool TankIgnoresTarget(PlayerbotAI*, Player*, Unit*) { NoteBoss(); return false; }
std::uint64_t TankPostLatchedUnit(Player*) { NoteBoss(); return 0; }
bool TauntRefusedByGate(PlayerbotAI*, Player*, Unit*, const char*) { NoteBoss(); return false; }
// Not boss logic (R9): the real body (BossTacticsActions.cpp:6865) is a victim check through the shared policy; a taunt stands down when another tank holds the target.
bool TauntTargetHeldByOtherTank(Player* bot, Unit* target)
{
    NoteBoss();
    if (!bot || !target)
        return false;
    Unit* victim = target->GetVictim();
    Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
    TauntHeldInputs in;
    in.victimIsPlayer = victimPlayer != nullptr;
    in.victimIsSelf = victimPlayer == bot;
    in.victimIsTank = victimPlayer && PlayerbotAI::IsTank(victimPlayer);
    return TauntStandsDownForTank(in);
}

// The multipliers the slice's combat strategies push (CombatStrategy.cpp, DungeonStrategy.cpp, AvoidAoeStrategy): 1.0 = no change.
// The engine evaluates every multiplier for every action candidate on every tick, so counting them made them the hottest boss-tactics
// counter by far (R8); they answer 1.0 without a count, the rows above and below stay counted.
float CharmedAllyHoldMultiplier::GetValue(Action*) { return 1.0f; }
float TrapHoldMultiplier::GetValue(Action*) { return 1.0f; }

// Fankriss*Actions.cpp, FankrissSpawnTarget.cpp, FankrissGauntletPolicy
bool FankrissBlocksAoe(PlayerbotAI*, uint32) { NoteBoss(); return false; }
bool FankrissBurnSpawn(PlayerbotAI*, Unit*) { NoteBoss(); return false; }
bool FankrissDpsMayAttack(PlayerbotAI*, Unit*) { NoteBoss(); return true; }   // "may attack": no Fankriss rule forbids it
// The real function is a plain reach check (alive, in map, hurt, friendly, a heal in range), not a boss rule; its only caller here is behind FankrissHealingActive (false),
// and "not reachable" is the answer that does less (R9: was true).
bool FankrissHealReachable(PlayerbotAI*, Unit*) { NoteBoss(); return false; }
bool FankrissHealingActive(PlayerbotAI*) { NoteBoss(); return false; }
Unit* GetAssignedFankrissSpawnTankTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
Unit* GetAssignedFankrissTankTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
Unit* GetFankrissDpsTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
Unit* GetFankrissSpawnTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
bool IgnoreFankrissGauntlet(Player*, Unit*) { NoteBoss(); return false; }
bool RetainFankrissFocusTarget(PlayerbotAI*, Unit*) { NoteBoss(); return false; }
void UpdateFankrissFocus(PlayerbotAI*) { NoteBoss(); }
void UpdateFankrissHealing(PlayerbotAI*) { NoteBoss(); }

// PatchwerkSoakerActions.cpp, PatchwerkSafety.cpp
Unit* GetAssignedPatchwerkTankTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
Unit* GetPatchwerkSoakBoss(PlayerbotAI*) { NoteBoss(); return nullptr; }
Player* GetPatchwerkSoakPuller(PlayerbotAI*) { NoteBoss(); return nullptr; }
Player* PatchwerkHealingTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
bool PatchwerkDpsOpeningHold(PlayerbotAI*) { NoteBoss(); return false; }
bool PatchwerkOpeningBlocksSpell(PlayerbotAI*, uint32, Unit*) { NoteBoss(); return false; }
bool PatchwerkThreatTank(PlayerbotAI*, Unit*) { NoteBoss(); return false; }
bool UpdatePatchwerkSafety(PlayerbotAI*) { NoteBoss(); return false; }
void WakePatchwerkPullMain(PlayerbotAI*) { NoteBoss(); }
// The three actions UpdateAI builds on the stack when a Patchwerk fight is on (GetPatchwerkSoakBoss non-null, never here).
bool PatchwerkMainEngageAction::Execute(Event&) { NoteBoss(); return false; }
bool PatchwerkSoakStageAction::Execute(Event&) { NoteBoss(); return false; }
bool PatchwerkSoakEngageAction::isUseful() { NoteBoss(); return false; }
bool PatchwerkSoakEngageAction::Execute(Event&) { NoteBoss(); return false; }

// ViscidusActions.cpp
Unit* GetViscidusCombatTarget(PlayerbotAI*) { NoteBoss(); return nullptr; }
std::list<HazardPosition> GetViscidusToxinHazards(PlayerbotAI*) { NoteBoss(); return {}; }
bool IsViscidusShatterTarget(Player*, Unit*) { NoteBoss(); return false; }
void UpdateViscidus(PlayerbotAI*) { NoteBoss(); }
bool ViscidusBlocksSpell(PlayerbotAI*, uint32, Unit*) { NoteBoss(); return false; }

// EmeraldSanctumDungeonTriggers.cpp
Unit* FindSolniusAdd(PlayerbotAI*) { NoteBoss(); return nullptr; }
bool IsSolniusAdd(Unit*) { NoteBoss(); return false; }

// ThreatResetHolds is the real function now (slice 3: ThreatStrategy.cpp is in the slice; its stand-in answered "never held" and is removed). Its
// readers of the boss table still end in the stand-ins above (GetBossTactics: no row for any map and entry), so on a target without a row it answers
// false exactly as before. StayWalkDueNow is the shared stay-walk rule and is no longer a stand-in either (R9): PositionAction.cpp is in the slice.

// =====================================================================================================================
// Startup tables of other managers. Owner file: GuildOrderSources.cpp. Nothing is built.
// (The enchant catalogue is real now: host/TbHostGuildOrderEnchantIndex.cpp.)
void BuildGuildOrderSourceIndex() { NotePool(); }
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t) { NotePool(); return {}; }

// =====================================================================================================================
// Misc legacy services. Owner files named per line.
// =====================================================================================================================
// FishAction.cpp
void EndFishingSession(PlayerbotAI*, char const*) { NoteMisc(); }
// BotAreaTriggers.cpp
void UpdateBotAreaTriggers(Player*, BotAreaTriggerState&, std::vector<uint32_t> const*) { NoteMisc(); }

// CheatAction.cpp: the mask of cheats a config string names. 0 = none (the cheat actions are not in the slice).
BotCheatMask CheatAction::GetCheatMask(std::string) { NoteMisc(); return BotCheatMask(0); }
// UseMeetingStoneAction.cpp: no spot found beside the summoner.
bool SummonAction::FindSpotBeside(Player*, Player*, float, float, float&, float&, float&) { NoteMisc(); return false; }
// ItemUsageValue.cpp: the reagent and vendor tables are not built.

// ChatFilter.cpp: the base filter is the real body (ChatFilter.cpp:11, it strips the "@..." prefix). The chain of role, class, race, rti, level and group filters
// is not ported, so a line that opens with "@" (a command for a role or a group the bot cannot check) is refused (R9): the empty text means "not for this bot".
// Any other line is returned as it comes, as the real chain does for a line no filter touches.
std::string ChatFilter::Filter(std::string message, std::string filter)
{
    NoteFilters();
    if (message.find("@") == std::string::npos)
        return message;
    if (filter.empty())
        return message.substr(message.find(" ") + 1);
    return message.substr(message.find(filter) + filter.size() + 1);
}
CompositeChatFilter::CompositeChatFilter(PlayerbotAI* ai) : ChatFilter(ai) {}
CompositeChatFilter::~CompositeChatFilter() {}
std::string CompositeChatFilter::Filter(std::string message)
{
    NoteFilters();
    return !message.empty() && message[0] == '@' ? std::string() : message;
}


// LootObjectStack.cpp (slice 2): the real file is in the slice now; its seven stand-ins and the `loot object stack` row are gone (R3).

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Slice 2 (R3), link stand-ins of the priest, loot and item-use files. Each one names: the real function's "absent" answer, the value given here, and the
// effect at its callers (the findings rule: a fallback makes the bot do LESS).
// ---------------------------------------------------------------------------------------------------------------------------------------------------

// BossTacticsActions.cpp (feature "boss and raid tactics": counted once per second, like the slice-1 stand-ins above).
// ScanBossTactics: absent = a fight with no boss row. Value: no units, no rows. Effect (PartyMemberToDispel.cpp): `rows` is empty, so the dispel pick is the
// plain tier search (`ranked` false); CollectDispelAuras / NearestGroupMemberYd / LogDispel* below are only reached when rows exist, so never.
BossTacticsFight ScanBossTactics(PlayerbotAI*, Player*) { NoteBoss(); return BossTacticsFight{}; }
// Absent = the unit carries no dispellable aura. Value: `out` left as the caller passed it (empty). Effect: no aura is offered to the dispel verdict (LESS).
void CollectDispelAuras(Unit*, uint32, std::vector<AuraOnMember>&) { NoteBoss(); }
// Absent = "a large value when the unit has none" (BossTacticsActions.h:55): the carrier counts as alone, i.e. clear. Value: the largest float, the same
// "no group member near" the caller's own non-ranked branch uses. Effect: unreachable without rows; if reached, a `clear` row would release the dispel
// (the only case where this is MORE than the real answer, and it needs a boss row, which the slice never has).
float NearestGroupMemberYd(Unit*) { NoteBoss(); return std::numeric_limits<float>::max(); }
// Absent = nothing to log. Value: nothing. Effect: one bot_events.csv row not written.
void LogDispelPick(PlayerbotAI*, Player*, DispelVerdict const&, Unit*, float) {}
void LogDispelExcluded(PlayerbotAI*, Player*, DispelVerdict const&, Unit*, float) {}
// Absent = no live spread row names a potion for this bot (BossTacticsActions.h:743: "Nothing unless ... the map has a spread row"). Value: kNoProtectionPotion
// (item 0). Effect (UseItemAction.cpp:1171): no boss protection potion is chosen or drunk (LESS).
ProtectionPotion BossRowProtectionPotion(PlayerbotAI*, Player*) { NoteBoss(); return kNoProtectionPotion; }

// WorldBuffAction.cpp (feature "other legacy services"): absent = the unit needs no world buff. Value: empty list. Effect: NeedWorldBuffTrigger (CureTriggers.cpp)
// is never active; the trigger key itself is not registered in the slice either (LESS: no buff is asked for).
std::vector<uint32> WorldBuffAction::NeedWorldBuffs(Unit*) { NoteMisc(); return {}; }

// TellLosAction.cpp's three stand-ins (ParseLosModifiers, GoGuidListToObjList, FilterGameObjects) are gone: S1-R2 compiles the real file for
// the `los` chat command, so the "use gos <modifiers>" form of the use command reads the real ones too. The `GameObjectUse` counter itself
// stays: the use command still refuses a game object on ForeverCore (`GameObjectUseUnportedCompat`).

// ItemUsageValue.cpp is in the slice since slice 9 (package L9): its four stand-ins (IsNeededForQuest, IsFutureUpgrade, QueryItemUsageForEquip, GetRecipeSpell) are gone.
}


// =====================================================================================================================
// Slice 9 (package L9): ItemUsageValue.cpp and LootRollAction.cpp are in the slice, and what they call outside it is answered here. Every answer is
// the "not there" value of the legacy code, so the item usage value classifies LESS: no equip upgrade is ever found (the stat weights are 0 and no
// spec may wear anything), so nothing is looted or rolled for equipping, and no mount, auction listing or repair price is known.
// =====================================================================================================================
// RandomItemMgr.cpp (feature "random item tables"): weights and spec rules now live in RandomItemWeights.cpp.
// RandomBotFacade (runtime/PlayerbotRuntimeFacade.cpp): the pool's price multipliers, trade discounts, auction prices and the unseen-item equip check. Absent =
// neutral multipliers (1.0), no discount, no listing, the equip is refused.
double RandomBotFacade::GetBuyMultiplier(Player*) { NotePool(); return 1.0; }
double RandomBotFacade::GetSellMultiplier(Player*) { NotePool(); return 1.0; }
void RandomBotFacade::AddTradeDiscount(Player*, Player*, int32) { NotePool(); }
int32 RandomBotFacade::GetValueValidTime(uint32, std::string) { NotePool(); return 0; }
std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32, Player*) const { NotePool(); return {}; }
// The core checks an item it has not created through a temporary Item (Player::CanEquipNewItem); swap=true as on Turtle:
// the caller compares old and new itself, and an occupied slot must not reject every upgrade.
InventoryResult RandomBotFacade::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    return player ? player->CanEquipNewItem(slot, dest, item, true) : EQUIP_ERR_ITEM_NOT_FOUND;
}
// GuildOrderStore.cpp (feature "pool and hire services"): no guild order exists, so nothing is protected and no bag-full note is kept.
namespace TortoiseBots { namespace GuildOrders {
void RefreshOrderProtectedSet(Player*) { NotePool(); }
void NoteBagsFull(uint32_t) { NotePool(); }
} }
// GuildOrderSources.cpp (same feature): no order is planned, so no bot carries a farm item, waits for a mail or uses a source.
std::pair<uint32_t, uint32_t> ai::GuildOrderFarmItemFor(Player*) { NotePool(); return {}; }
bool ai::GuildOrderMailWaiting(Player*) { NotePool(); return false; }
bool ai::GuildOrderEntryUsableFor(uint32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool) { NotePool(); return false; }
// RandomBotFacade (runtime/PlayerbotRuntimeFacade.cpp): no teleport cache is built, so the overlay export writes nothing.
void RandomBotFacade::PrintTeleportCache() { NotePool(); }
// QueryItemUsageAction.cpp: the `query item usage` chat reply (a debug answer) is not built; the class is the base of the roll actions, which override Execute.
bool ai::QueryItemUsageAction::Execute(Event&) { NoteMisc(); return false; }

// =====================================================================================================================
// Managers with tables (owner files: ai/playerbot/RandomItemMgr.cpp, PlayerbotTextMgr.cpp, PlayerbotHelpMgr.cpp,
// Talentspec.cpp, PlayerbotFactory.cpp, PlayerbotDbStore.cpp, RandomBotFacade loaders in runtime/PlayerbotRuntimeFacade.cpp).
// =====================================================================================================================
RandomItemMgr::RandomItemMgr() {}
RandomItemMgr::~RandomItemMgr() {}
void RandomItemMgr::Init() { LoadWeightScales(); }
uint32 RandomItemMgr::GetStatWeight(Player*, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetStatWeight(uint32, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetFood(uint32, uint32, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetRandomPotion(uint32, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetLiveStatWeight(Player*, uint32, uint32) { NoteItems(); return 0; }
uint8 RandomItemMgr::GetSourceFlags(uint32) { NoteItems(); return ITEM_SOURCE_FLAG_NONE; }

// With no text table the real GetBotText(name) answers the empty text (PlayerbotTextMgr.cpp:65; R9: the old stand-in answered the key, so a bot whispered raw
// keys such as "hello"); every caller of it skips an empty text. The placeholder form falls back to the name with the placeholders replaced, as the real one does.
// The locale priority is "none".
PlayerbotTextMgr::PlayerbotTextMgr() {}
PlayerbotTextMgr::~PlayerbotTextMgr() {}
void PlayerbotTextMgr::LoadBotTexts() { NoteTexts(); }
std::string PlayerbotTextMgr::GetBotText(std::string) { NoteTexts(); return std::string(); }
std::string PlayerbotTextMgr::GetBotText(std::string name, std::map<std::string, std::string> placeholders)
{
    NoteTexts();
    std::string text = std::move(name);
    for (auto const& placeholder : placeholders)
        for (std::size_t at = text.find(placeholder.first); at != std::string::npos && !placeholder.first.empty(); at = text.find(placeholder.first, at + placeholder.second.size()))
            text.replace(at, placeholder.first.size(), placeholder.second);
    return text;
}
// -1 = no locale has priority: the default (English) text and names. (The real function answers -1 too whenever a real player is online and no text locale was counted;
// it answers 0 only when no session is online, and index 0 is a real client locale, never "none".) R9: was 0.
int32 PlayerbotTextMgr::GetLocalePriority() { return -1; }

PlayerbotHelpMgr::PlayerbotHelpMgr() {}
PlayerbotHelpMgr::~PlayerbotHelpMgr() {}
void PlayerbotHelpMgr::LoadBotHelpTexts() { NoteTexts(); }

// S1-R2 (chat commands in): the text side of the "help" chat command (HelpAction.cpp is in the build now). The texts are the "bot texts and help"
// row (ai_playerbot_texts and help topics are not ported, R6B), so every lookup answers what the real manager answers with no rows: no text,
// no topic, and the format helpers are unreachable (HelpAction calls them only for a text that came back). LESS: `help` whispers the command
// and strategy lists, never the text topics.
void PlayerbotHelpMgr::replace(std::string&, const std::string, const std::string) { NoteTexts(); }
std::string PlayerbotHelpMgr::makeList(std::vector<std::string> const, std::string, uint32) { NoteTexts(); return std::string(); }
void PlayerbotHelpMgr::FormatHelpTopic(std::string&) { NoteTexts(); }
void PlayerbotHelpMgr::FormatHelpTopics() { NoteTexts(); }
std::string PlayerbotHelpMgr::GetBotText(std::string) { NoteTexts(); return std::string(); }
bool PlayerbotHelpMgr::GetBotText(std::string, std::string&) { NoteTexts(); return false; }
std::vector<std::string> PlayerbotHelpMgr::FindBotText(std::string) { NoteTexts(); return {}; }

// Talentspec.cpp: no spec is read, no spec checks pass or fail (the premade spec config is not loaded).
void TalentSpec::GetTalents(uint32) { NoteSpecs(); }
void TalentSpec::ReadTalents(std::string) { NoteSpecs(); }
void TalentSpec::ReadTalents(Player*) { NoteSpecs(); }
bool TalentSpec::CheckTalents(uint32, std::ostringstream*) { NoteSpecs(); return false; }
bool TalentSpec::CheckTalentLink(std::string, std::ostringstream*) { NoteSpecs(); return false; }
uint32 TalentSpec::LeveltoPoints(uint32) { NoteSpecs(); return 0; }
void TalentSpec::SetPublicNote(Player*) { NoteSpecs(); }

// PlayerbotFactory.cpp: the five pet and ammo members are real since slice 11 (forever/src/host/TbHostPetFactory.cpp).

// PlayerbotDbStore.cpp: real implementation lives in forever/src/slice/SliceDbStore.cpp.

// RandomBotFacade loaders (runtime/PlayerbotRuntimeFacade.cpp): the pool caches are not built.
void RandomBotFacade::LoadNamedLocationCache() { NotePool(); }
void RandomBotFacade::LoadBattleMastersCache() { NotePool(); }
void RandomBotFacade::LoadAuctionPrices() { NotePool(); }

// =====================================================================================================================
// Slice 3 (porting worker): the three symbols the ten new files (SLICE3.txt) reference that no slice file defines. Found by the option-ON
// module build of the slice-3 list in slot 2 (`nm -u` of the module objects minus every symbol a module object defines; the rest of the
// undefined names are the core's). Each is the real function's answer when its feature is absent, read in the owner file.
// =====================================================================================================================
namespace ai
{
// PatchwerkSoakerActions.cpp:212 (read by PullAction::Execute, PullActions.cpp:455): "may the puller fire?". The real body is
// `if (!PatchwerkMainPull(puller)) return true;` and PatchwerkMainPull needs map 533, the strategy `patchwerk main` (unregistered here) and an
// active command pull: on every other pull the answer is true, which is also the value here ("no soaker to wait for"; the pull window still ends
// the pull). A Patchwerk fight is not in the slice, so there is nothing a `false` could protect.
bool PatchwerkSoakersReadyForPull(PlayerbotAI*) { NoteBoss(); return true; }

// BossTacticsActions.cpp:10085 (read by ThreatResetHolds, behind AiPlayerbot.DungeonRunThreatResetWindow = off and a boss-table row that never
// exists here): the real function empties `out` and returns false when it holds no record of the fight, which is always the case without the
// seek-cycle bookkeeping of the boss tactics. Same value, same effect ("no snapshot": the reset hold clears its latch and holds nothing).
bool SeekCycleSnapshot(std::uint32_t, std::uint64_t, SeekCycleView& out) { NoteBoss(); out = SeekCycleView(); return false; }

}

// behavior/PlayerConvenience.cpp:207 (read by the stay / guard chat shortcuts): tells the pending-summon bookkeeping that the bot was ordered to hold, so the
// summon's restore does not put it back on follow. The real function returns false when the bot has no summon record, which is always
// the case here (ForeverCore keeps no summon bookkeeping): the host's BotMgr::Think places a far bot next to its master itself.
// If the host adds it next to the other PlayerConvenience stand-ins in host/TbHostOwnership.cpp, delete this one (duplicate symbol at link).
bool TortoiseBots::PlayerConvenience::NoteHoldOrder(ObjectGuid) { return false; }

// =====================================================================================================================
// W2 (town: sell, buy, repair, trainer, mail read): symbols referenced by the W2 action files whose owners (MaintenanceValues,
// GuildOrders, TravelMgr) are not in the slice yet (waits for W1/W4).
// =====================================================================================================================
#include "playerbot/strategy/values/MaintenanceValues.h"
#include "compat/TbCompat_W2Town.h"
#include "playerbot/strategy/values/TrainerValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
namespace ai
{
// TrainerValues.cpp (its spell map scans the 1.12 trainer templates) is not in the slice; these are its plain helpers.
uint32 TrainerSpellPrice(TrainerSpell const* tSpell, float discountMod) { return uint32(floor(tSpell->spellCost * discountMod)); }
bool TrainerTrainsForFree(PlayerbotAI* ai) { return sPlayerbotAIConfig.autoTrainSpells == "free" || ai->HasCheat(BotCheatMask::gold); }
// The visit's green-row gate. On ForeverCore the visit asks the host seam instead (TrainerAction checks
// UsesForeverTrainerCompat and CanTeachTrainerSpellForeverCompat), so this body is only reached on a core without the
// seam: no row is teachable then, which keeps the visit from paying for one.
bool TrainerSpellLearnableNow(Player* bot, TrainerSpell const* tSpell)
{
    (void)bot;
    (void)tSpell;
    NoteMisc();
    return false;
}
uint32 TrainerSpendableMoney(PlayerbotAI* ai, TrainerType type)
{
    if (TrainerTrainsForFree(ai))
        return UINT32_MAX;
    NeedMoneyFor budget = NeedMoneyFor::anything;
    if (type == TRAINER_TYPE_CLASS)
        budget = NeedMoneyFor::spells;
    else if (type == TRAINER_TYPE_MOUNTS)
        budget = NeedMoneyFor::mount;
    else if (type == TRAINER_TYPE_TRADESKILLS)
        budget = NeedMoneyFor::skilltraining;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE2(uint32, "free money for", (uint32)budget);
}
}

// =====================================================================================================================
// The travel chooser and walker call the order store, the guild bank access and the trainer trigger helpers. None is
// ported, so each answers "nothing exists": no order (Find/RelistEntries are the host's), no bank, no trainer.
// =====================================================================================================================
namespace TortoiseBots
{
namespace GuildOrders
{
void SetState(uint32_t, OrderState, OrderReason) { NotePool(); }
void SetFarmZone(uint32_t, uint32_t, uint8_t, uint32_t, uint32_t) { NotePool(); }
void SetSupply(uint32_t, SupplyStep, uint32_t, uint32_t, SupplyStep, OrderReason, uint32_t) { NotePool(); }
void CommitAuctionZone(uint32_t, uint32_t) { NotePool(); }
void NoteSearchFailed(uint32_t, uint32_t) { NotePool(); }
void NoteSearchLanded(uint32_t) { NotePool(); }
void NoteFarmZoneTargetFailed(uint32_t, uint32_t, uint32_t, float, float, float) { NotePool(); }
uint32_t GuildBankTabCount(uint32_t) { NotePool(); return 0; }
void NoteBankEmpty(uint32_t, uint32_t) { NotePool(); }
void NoteMailEmpty(uint32_t, uint32_t) { NotePool(); }
uint32_t NoteSupplySearchFailed(uint32_t, SupplyStep, uint32_t) { NotePool(); return 0; }
uint32_t NoteArrivedAuctionVisit(uint32_t, bool) { NotePool(); return 0; }
bool AuctionReentryAllowed(uint32_t, uint32_t) { NotePool(); return true; }
}
}

namespace ai
{
// FishValues.cpp (the fishing order session) is not in the slice: no order fishes, so the rpg walk never stands down for
// one. Owner: ai/playerbot/strategy/values/FishValues.cpp.
bool IsOrderFishing(PlayerbotAI*) { NoteMisc(); return false; }
// GuildCreateActions.cpp (the charter errands) is not in the slice: no petition can be bought. Owner:
// ai/playerbot/strategy/actions/GuildCreateActions.cpp.
bool BuyPetitionAction::canBuyPetition(Player*) { NoteMisc(); return false; }
Unit* GuildOrderAuctioneerNear(PlayerbotAI*, Player*) { NotePool(); return nullptr; }
uint32 OrderMailboxItems(Player*, uint32) { NotePool(); return 0; }
std::vector<GuildOrderZoneSlice> GuildOrderZonesFor(int32_t) { NotePool(); return {}; }
bool GuildOrderPreyHostileToBot(Player*, int32_t) { NotePool(); return false; }
// No spot is screened: every fishing destination counts as guarded and is skipped (LESS: none is walked).
bool IsFishingSpotGuarded(Player*, WorldPosition const&, float) { NoteMisc(); return true; }
TortoiseBots::GuildOrders::SourcePlan PlanGuildOrderSources(Player*, uint32_t, TortoiseBots::GuildOrders::OrderType, bool)
{
    NotePool();
    TortoiseBots::GuildOrders::SourcePlan plan;
    plan.verdict = TortoiseBots::GuildOrders::OrderReason::NoSource;
    return plan;
}
void AdvanceGuildOrderAfterAuction(PlayerbotAI*, Player*, TortoiseBots::GuildOrders::OrderRecord const&, TortoiseBots::GuildOrders::OrderReason) { NotePool(); }
}

// DowntimeService (runtime/DowntimeService.cpp) is not in the slice: no errand window is ever open, so the rpg quest
// verbs stay on (the same answer a server without `.tb downtime` gives).
namespace TortoiseBots
{
bool DowntimeService::IsDowntimeActive(PlayerbotAI*) { NoteMisc(); return false; }
}

// RandomBotFacade (runtime/PlayerbotRuntimeFacade.cpp): no per-bot value store and no pinned-bot set.
std::string RandomBotFacade::GetData(uint32, std::string) { NotePool(); return std::string(); }
bool RandomBotFacade::IsPinnedBot(uint32) { NotePool(); return false; }

// =====================================================================================================================
// W3 (quests): the alt-bot service and the random-bot value store are not ported, so the quest actions record nothing in
// them: no quest is "taken by an alt" and a drop leaves no pool value behind.
// =====================================================================================================================
#include "runtime/AltBotService.h"
namespace TortoiseBots
{
void AltBotService::AddTakenQuest(Player*, uint32_t) { NotePool(); }
bool AltBotService::IsTakenQuest(uint32_t, uint32_t) const { NotePool(); return false; }
}
void RandomBotFacade::SetValue(Player*, std::string, uint32, std::string, int32) { NotePool(); }

// =====================================================================================================================
// Free-bot life: the shared value context the rpg/quest/town values reach also builds the trainer table and the
// battlemaster list. Neither is ported (no trainer travel, no battleground queue from the open world), so both are empty.
// =====================================================================================================================
#include "playerbot/strategy/values/PvpValues.h"
ai::trainableSpellMap* ai::TrainableSpellMapValue::Calculate() { NoteMisc(); return new trainableSpellMap(); }
std::list<CreatureDataPair const*> ai::BgMastersValue::Calculate() { NoteMisc(); return {}; }
