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
#include "playerbot/BossStandMgr.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossTacticsStand.h"
#include "playerbot/BotAreaTriggers.h"
#include "playerbot/CastCancelLog.h"
#include "playerbot/ChatFilter.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/FankrissGauntletPolicy.h"
#include "playerbot/FleeManager.h"
#include "playerbot/GuildOrderEnchants.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "playerbot/PlayerbotTextMgr.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/Talentspec.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TravelNode.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/actions/CheatAction.h"
#include "playerbot/strategy/actions/EmoteAction.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/actions/FankrissRaidActions.h"
#include "playerbot/strategy/actions/FankrissTankActions.h"
#include "playerbot/strategy/actions/FishAction.h"
#include "playerbot/strategy/actions/PatchwerkSafety.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "playerbot/strategy/actions/PositionAction.h"
#include "playerbot/strategy/actions/UseMeetingStoneAction.h"
#include "playerbot/strategy/actions/ViscidusActions.h"
#include "playerbot/strategy/generic/ThreatStrategy.h"
#include "playerbot/strategy/triggers/EmeraldSanctumDungeonTriggers.h"
#include "playerbot/strategy/values/EnemyPlayerValue.h"
#include "playerbot/strategy/values/FankrissSpawnTarget.h"
#include "playerbot/strategy/values/GrindTargetValue.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/strategy/values/WorldPvpSeekChecks.h"

namespace
{
inline void Boss()      { tbcompat::Unported(tbcompat::Feature::BossTactics); }
inline void Travel()    { tbcompat::Unported(tbcompat::Feature::TravelMgr); }
inline void Loot()      { tbcompat::Unported(tbcompat::Feature::LootStack); }
inline void Items()     { tbcompat::Unported(tbcompat::Feature::RandomItems); }
inline void Texts()     { tbcompat::Unported(tbcompat::Feature::BotTexts); }
inline void Specs()     { tbcompat::Unported(tbcompat::Feature::TalentSpecs); }
inline void Pool()      { tbcompat::Unported(tbcompat::Feature::PoolServices); }
inline void Factory()   { tbcompat::Unported(tbcompat::Feature::BotFactory); }
inline void Filters()   { tbcompat::Unported(tbcompat::Feature::ChatFilters); }
inline void Flee()      { tbcompat::Unported(tbcompat::Feature::FleeManager); }
inline void Misc()      { tbcompat::Unported(tbcompat::Feature::MiscLegacyServices); }
}

// =====================================================================================================================
// Boss and raid tactics. Owner files: ai/playerbot/strategy/actions/BossTacticsActions.cpp, Fankriss*Actions.cpp,
// PatchwerkSoakerActions.cpp, PatchwerkSafety.cpp, ViscidusActions.cpp, EmeraldSanctumDungeon*.cpp, ThreatStrategy.cpp,
// FankrissGauntletPolicy (header), BossTacticsMgr.cpp, BossStandMgr.cpp, DungeonPlanMgr.cpp.
// Answer: no boss rule applies (false / null / 0 / empty); the table getters return an empty table.
// =====================================================================================================================
namespace ai
{
// BossTacticsMgr.cpp / DungeonPlanMgr.cpp: the tables stay empty (their Load* functions are stand-ins below).
BossTacticTable& GetBossTactics()
{
    Boss();
    static BossTacticTable table;
    return table;
}

DungeonPlanTable& GetDungeonPlans()
{
    Boss();
    static DungeonPlanTable table;
    return table;
}

void LoadBossTactics() { Boss(); }
void LoadBossStands() { Boss(); }
void LoadDungeonPlans() { Boss(); }

// BossTacticsActions.cpp
BossTacticsScanScope::BossTacticsScanScope() { Boss(); }
BossTacticsScanScope::~BossTacticsScanScope() {}
bool AoeTauntStillNeeded(PlayerbotAI*, Player*) { Boss(); return false; }
bool AreaTauntRefusedByPost(PlayerbotAI*, Player*, const char*) { Boss(); return false; }
bool ClampMoveToBossAnchor(PlayerbotAI*, Player*, float&, float&, float&) { Boss(); return false; }
bool DispelCastBlocked(PlayerbotAI*, Unit*, std::uint32_t) { Boss(); return false; }
BossStandTarget FindBossStand(PlayerbotAI*, Player*) { Boss(); return BossStandTarget{}; }
TankPostHealDuty FindTankPostHealDuty(PlayerbotAI*, Player*) { Boss(); return TankPostHealDuty{}; }
std::uint32_t GateHoldFighters(PlayerbotAI*, std::uint32_t, float) { Boss(); return 0; }
// "Whether a healer may top up the target": with no boss row the Turtle answer is today's mana rule; the slice's healer path is
// not reached by a warrior, and true (no reserve refuses) is the permissive end of it.
bool HealTopUpAllowedFor(PlayerbotAI*, Player*, Unit*) { Boss(); return true; }
bool HealReserveRefusesTopUp(PlayerbotAI*, Player*, Unit*, float) { Boss(); return false; }
std::uint32_t HoldKillBlockers(PlayerbotAI*, std::uint32_t) { Boss(); return 0; }
bool IsDpsHeld(PlayerbotAI*, Player*, Unit*, DpsHoldTrigger*) { Boss(); return false; }
bool IsLiveWalkoutPuller(Player*) { Boss(); return false; }
bool IsRangedOnlyHeld(PlayerbotAI*, Player*, Unit*, float*) { Boss(); return false; }
bool IsSeekSourceForTankPickup(PlayerbotAI*, Player*, Unit*) { Boss(); return false; }
bool IsSoughtCloudFor(PlayerbotAI*, Player*, DynamicObject*) { Boss(); return false; }
bool LiveBossAnchor(PlayerbotAI*, Player*, BossAnchorCircle&) { Boss(); return false; }
bool MeetSonChaseExempt(PlayerbotAI*, Player*, Unit*) { Boss(); return false; }
void ReleaseWalkoutPet(Player*) { Boss(); }
BossLinkedDeathScan ScanBossLinkedDeath(PlayerbotAI*, Player*) { Boss(); return BossLinkedDeathScan{}; }
std::uint64_t SeekMeetHealPuller(std::uint32_t) { Boss(); return 0; }
bool SeekMeetPrePlaceHoldsChase(PlayerbotAI*, Player*, Unit*, const char*) { Boss(); return false; }
std::uint64_t SoleKillGuid(PlayerbotAI*, std::uint32_t) { Boss(); return 0; }
bool TankIgnoresTarget(PlayerbotAI*, Player*, Unit*) { Boss(); return false; }
std::uint64_t TankPostLatchedUnit(Player*) { Boss(); return 0; }
bool TauntRefusedByGate(PlayerbotAI*, Player*, Unit*, const char*) { Boss(); return false; }
bool TauntTargetHeldByOtherTank(Player*, Unit*) { Boss(); return false; }

// The multipliers the slice's combat strategies push (CombatStrategy.cpp, DungeonStrategy.cpp, AvoidAoeStrategy): 1.0 = no change.
float CharmedAllyHoldMultiplier::GetValue(Action*) { Boss(); return 1.0f; }
float TrapHoldMultiplier::GetValue(Action*) { Boss(); return 1.0f; }

// Fankriss*Actions.cpp, FankrissSpawnTarget.cpp, FankrissGauntletPolicy
bool FankrissBlocksAoe(PlayerbotAI*, uint32) { Boss(); return false; }
bool FankrissBurnSpawn(PlayerbotAI*, Unit*) { Boss(); return false; }
bool FankrissDpsMayAttack(PlayerbotAI*, Unit*) { Boss(); return true; }   // "may attack": no Fankriss rule forbids it
bool FankrissHealReachable(PlayerbotAI*, Unit*) { Boss(); return true; }  // "reachable": no Fankriss rule says otherwise
bool FankrissHealingActive(PlayerbotAI*) { Boss(); return false; }
Unit* GetAssignedFankrissSpawnTankTarget(PlayerbotAI*) { Boss(); return nullptr; }
Unit* GetAssignedFankrissTankTarget(PlayerbotAI*) { Boss(); return nullptr; }
Unit* GetFankrissDpsTarget(PlayerbotAI*) { Boss(); return nullptr; }
Unit* GetFankrissSpawnTarget(PlayerbotAI*) { Boss(); return nullptr; }
bool IgnoreFankrissGauntlet(Player*, Unit*) { Boss(); return false; }
bool RetainFankrissFocusTarget(PlayerbotAI*, Unit*) { Boss(); return false; }
void UpdateFankrissFocus(PlayerbotAI*) { Boss(); }
void UpdateFankrissHealing(PlayerbotAI*) { Boss(); }

// PatchwerkSoakerActions.cpp, PatchwerkSafety.cpp
Unit* GetAssignedPatchwerkTankTarget(PlayerbotAI*) { Boss(); return nullptr; }
Unit* GetPatchwerkSoakBoss(PlayerbotAI*) { Boss(); return nullptr; }
Player* GetPatchwerkSoakPuller(PlayerbotAI*) { Boss(); return nullptr; }
Player* PatchwerkHealingTarget(PlayerbotAI*) { Boss(); return nullptr; }
bool PatchwerkDpsOpeningHold(PlayerbotAI*) { Boss(); return false; }
bool PatchwerkOpeningBlocksSpell(PlayerbotAI*, uint32, Unit*) { Boss(); return false; }
bool PatchwerkThreatTank(PlayerbotAI*, Unit*) { Boss(); return false; }
bool UpdatePatchwerkSafety(PlayerbotAI*) { Boss(); return false; }
void WakePatchwerkPullMain(PlayerbotAI*) { Boss(); }
// The three actions UpdateAI builds on the stack when a Patchwerk fight is on (GetPatchwerkSoakBoss non-null, never here).
bool PatchwerkMainEngageAction::Execute(Event&) { Boss(); return false; }
bool PatchwerkSoakStageAction::Execute(Event&) { Boss(); return false; }
bool PatchwerkSoakEngageAction::isUseful() { Boss(); return false; }
bool PatchwerkSoakEngageAction::Execute(Event&) { Boss(); return false; }

// ViscidusActions.cpp
Unit* GetViscidusCombatTarget(PlayerbotAI*) { Boss(); return nullptr; }
std::list<HazardPosition> GetViscidusToxinHazards(PlayerbotAI*) { Boss(); return {}; }
bool IsViscidusShatterTarget(Player*, Unit*) { Boss(); return false; }
void UpdateViscidus(PlayerbotAI*) { Boss(); }
bool ViscidusBlocksSpell(PlayerbotAI*, uint32, Unit*) { Boss(); return false; }

// EmeraldSanctumDungeonTriggers.cpp
Unit* FindSolniusAdd(PlayerbotAI*) { Boss(); return nullptr; }
bool IsSolniusAdd(Unit*) { Boss(); return false; }

// ThreatStrategy.cpp, PositionAction.cpp
bool ThreatResetHolds(PlayerbotAI*, Player*, Unit*) { Boss(); return false; }
bool StayWalkDueNow(PlayerbotAI*, Player*, PositionEntry const&) { Boss(); return false; }

// =====================================================================================================================
// Startup tables of other managers. Owner files: GuildOrderSources.cpp, GuildOrderEnchants.cpp. Nothing is built.
// =====================================================================================================================
void BuildGuildOrderEnchantIndex() { Pool(); }
void BuildGuildOrderSourceIndex() { Pool(); }
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t) { Pool(); return {}; }

// =====================================================================================================================
// Misc legacy services. Owner files named per line.
// =====================================================================================================================
// CastCancelLog.cpp: diagnostics only (a log line per cancelled cast), off.
void LogCastCancel(PlayerbotAI*, const char*, CurrentSpellTypes, float) { Misc(); }
void LogCastCancelNonMelee(PlayerbotAI*, const char*, bool, float) { Misc(); }
void LogCastCancelStop(PlayerbotAI*, const char*, float) { Misc(); }
// FishAction.cpp
void EndFishingSession(PlayerbotAI*, char const*) { Misc(); }
// BotAreaTriggers.cpp
void UpdateBotAreaTriggers(Player*, BotAreaTriggerState&, std::vector<uint32_t> const*) { Misc(); }

// CheatAction.cpp: the mask of cheats a config string names. 0 = none (the cheat actions are not in the slice).
BotCheatMask CheatAction::GetCheatMask(std::string) { Misc(); return BotCheatMask(0); }
// EmoteAction.cpp: how many chat variants a text emote has. 1 = only the plain one.
uint32 EmoteActionBase::GetNumberOfEmoteVariants(TextEmotes, uint8, uint8) { Misc(); return 1; }
// MountValues.cpp
bool MountValue::IsMountSpell(uint32) { Misc(); return false; }
// GrindTargetValue.cpp: "an instance target may be ground": no.
bool GrindTargetValue::IsAllowedInstanceTarget(PlayerbotAI*, Unit*) { Misc(); return false; }
// UseMeetingStoneAction.cpp: no spot found beside the summoner.
bool SummonAction::FindSpotBeside(Player*, Player*, float, float, float&, float&, float&) { Misc(); return false; }
// ItemUsageValue.cpp: the reagent and vendor tables are not built.
void ItemUsageValue::PopulateProfessionReagentIds() { Items(); }
void ItemUsageValue::PopulateReagentItemIdsForCraftableItemIds() { Items(); }
void ItemUsageValue::PopulateSoldByVendorItemIds() { Items(); }
// EnemyPlayerValue.cpp, WorldPvpSeekChecks.cpp: no open-world PvP seeking.
float EnemyPlayerValue::GetMaxAttackDistance(Player*) { Misc(); return 0.0f; }
bool WorldPvpSeekChecks::IsSeeking(PlayerbotAI*, Player*) { Misc(); return false; }
bool WorldPvpSeekChecks::IsSeekTarget(PlayerbotAI*, Player*, Player*) { Misc(); return false; }

// ChatFilter.cpp: the chain keeps no filter, a line is returned as it comes.
std::string ChatFilter::Filter(std::string message, std::string) { Filters(); return message; }
CompositeChatFilter::CompositeChatFilter(PlayerbotAI* ai) : ChatFilter(ai) {}
CompositeChatFilter::~CompositeChatFilter() {}
std::string CompositeChatFilter::Filter(std::string message) { Filters(); return message; }

// FleeManager.cpp: no destination is found, no mob veto.
IdleMobVeto::IdleMobVeto(PlayerbotAI*, Player*, std::list<ObjectGuid> const&, Unit*, float) { Flee(); }
bool IdleMobVeto::Wakes(float, float) const { Flee(); return false; }
bool FleeManager::CalculateDestination(float*, float*, float*, FleeFailureMemory const*, uint32) { Flee(); return false; }
bool FleeManager::isUseful() { Flee(); return false; }

// LootObjectStack.cpp: an empty stack that never loots.
LootObject::LootObject(const LootObject&) = default;
bool LootObject::IsLootPossible(Player*) { Loot(); return false; }
WorldObject* LootObject::GetWorldObject(Player*) { Loot(); return nullptr; }
bool LootObjectStack::Add(ObjectGuid, bool) { Loot(); return false; }
void LootObjectStack::Remove(ObjectGuid) { Loot(); }
void LootObjectStack::NoteApproachFailure(ObjectGuid) { Loot(); }
bool LootObjectStack::IsAbandoned(ObjectGuid) { Loot(); return false; }
}

// =====================================================================================================================
// Travel (owner files: ai/playerbot/TravelMgr.cpp, TravelNode.cpp). No node map, no destinations, no route.
// =====================================================================================================================
namespace ai
{
void TravelMgr::Clear() { Travel(); }
void TravelMgr::LoadAreaLevels() { Travel(); }
void TravelMgr::LoadQuestTravelTable() { Travel(); }
void TravelMgr::LoadTaxiNodeZones() { Travel(); }
int32 TravelMgr::GetAreaLevel(uint32) { Travel(); return 0; }
void TravelMgr::SetNullTravelTarget(TravelTarget*) const { Travel(); }
void TravelMgr::CollectMapTransferPortals(const WorldPosition&, uint32, std::vector<std::pair<WorldPosition, float>>&) const { Travel(); }
// Distance across a map change: "no portal known" is the length of a direct line is not knowable; 0 keeps the plain distance.
float TravelMgr::MapTransDistance(const WorldPosition&, const WorldPosition&, bool) const { Travel(); return 0.0f; }

TravelTarget::TravelTarget(PlayerbotAI* ai) : AiObject(ai) { Travel(); }
bool TravelTarget::IsActive() { Travel(); return false; }
void TravelTarget::SetStatus(TravelStatus) { Travel(); }

WorldPosition TravelNodeMap::getMapOffset(uint32) { Travel(); return WorldPosition(); }
TravelPath TravelNodeMap::getFullPath(WorldPosition, WorldPosition, Unit*) { Travel(); return TravelPath(); }
void TravelPath::ClipPath(PlayerbotAI*, Unit*, bool, PackHoldResult*) { Travel(); }
bool TravelPath::UpcommingSpecialMovement(WorldPosition, float, bool) { Travel(); return false; }
bool TravelPath::cutTo(PathNodePoint, bool) { Travel(); return false; }
void TravelPath::makeShortCut(WorldPosition, float, Unit*) { Travel(); }

// The vtables of the destination classes TravelMgr's members construct: the key functions, with the "nothing is possible"
// answers the base TravelDestination gives.
std::string EntryTravelDestination::GetShortName() const { Travel(); return ""; }
bool GrindTravelDestination::IsPossible(const PlayerTravelInfo&) const { Travel(); return false; }
bool GrindTravelDestination::IsActive(Player*, const PlayerTravelInfo&) const { Travel(); return false; }
std::string GrindTravelDestination::GetTitle() const { Travel(); return "grind"; }
bool GatherTravelDestination::IsPossible(const PlayerTravelInfo&) const { Travel(); return false; }
bool GatherTravelDestination::IsActive(Player*, const PlayerTravelInfo&) const { Travel(); return false; }
std::string GatherTravelDestination::GetTitle() const { Travel(); return "gather"; }
}

// =====================================================================================================================
// Managers with tables (owner files: ai/playerbot/RandomItemMgr.cpp, PlayerbotTextMgr.cpp, PlayerbotHelpMgr.cpp,
// Talentspec.cpp, PlayerbotFactory.cpp, PlayerbotDbStore.cpp, RandomBotFacade loaders in runtime/PlayerbotRuntimeFacade.cpp).
// =====================================================================================================================
RandomItemMgr::RandomItemMgr() {}
RandomItemMgr::~RandomItemMgr() {}
void RandomItemMgr::Init() { Items(); }
uint32 RandomItemMgr::GetStatWeight(Player*, uint32) { Items(); return 0; }
uint32 RandomItemMgr::GetStatWeight(uint32, uint32) { Items(); return 0; }
uint32 RandomItemMgr::GetPlayerSpecId(Player*) { Items(); return 0; }
uint32 RandomItemMgr::GetFood(uint32, uint32, uint32) { Items(); return 0; }

// The bot text lookups answer the text name itself (an empty text would make the caller say nothing; the name is at least
// visible in a log). The locale priority is "none".
PlayerbotTextMgr::PlayerbotTextMgr() {}
PlayerbotTextMgr::~PlayerbotTextMgr() {}
void PlayerbotTextMgr::LoadBotTexts() { Texts(); }
std::string PlayerbotTextMgr::GetBotText(std::string name) { Texts(); return name; }
std::string PlayerbotTextMgr::GetBotText(std::string name, std::map<std::string, std::string>) { Texts(); return name; }
int32 PlayerbotTextMgr::GetLocalePriority() { return 0; }

PlayerbotHelpMgr::PlayerbotHelpMgr() {}
PlayerbotHelpMgr::~PlayerbotHelpMgr() {}
void PlayerbotHelpMgr::LoadBotHelpTexts() { Texts(); }

// Talentspec.cpp: no spec is read, no spec checks pass or fail (the premade spec config is not loaded).
void TalentSpec::GetTalents(uint32) { Specs(); }
void TalentSpec::ReadTalents(std::string) { Specs(); }
void TalentSpec::ReadTalents(Player*) { Specs(); }
bool TalentSpec::CheckTalents(uint32, std::ostringstream*) { Specs(); return false; }
bool TalentSpec::CheckTalentLink(std::string, std::ostringstream*) { Specs(); return false; }
uint32 TalentSpec::LeveltoPoints(uint32) { Specs(); return 0; }
void TalentSpec::SetPublicNote(Player*) { Specs(); }

// PlayerbotFactory.cpp: no ammo and no pet set-up.
void PlayerbotFactory::InitAmmo() { Factory(); }
void PlayerbotFactory::InitPet(bool) { Factory(); }
void PlayerbotFactory::InitPetSpells() { Factory(); }
bool PlayerbotFactory::PetAutoTrainAllowed(Player*) { Factory(); return false; }
bool PlayerbotFactory::TrainHunterPet(Player*, Pet*, bool, std::function<bool(uint32)> const&) { Factory(); return false; }

// PlayerbotDbStore.cpp: nothing is stored on ForeverCore (the ai_playerbot tables do not exist), so a load finds nothing.
void PlayerbotDbStore::Load(PlayerbotAI*, std::string) { Misc(); }

// RandomBotFacade loaders (runtime/PlayerbotRuntimeFacade.cpp): the pool caches are not built.
void RandomBotFacade::LoadNamedLocationCache() { Pool(); }
void RandomBotFacade::LoadBattleMastersCache() { Pool(); }
void RandomBotFacade::LoadAuctionPrices() { Pool(); }
