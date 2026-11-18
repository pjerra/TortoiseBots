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

#include <atomic>
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
inline void NoteTravel()    { tbcompat::Unported(tbcompat::Feature::TravelMgr); }
inline void NoteLoot()      { tbcompat::Unported(tbcompat::Feature::LootStack); }
inline void NoteItems()     { tbcompat::Unported(tbcompat::Feature::RandomItems); }
inline void NoteTexts()     { tbcompat::Unported(tbcompat::Feature::BotTexts); }
inline void NoteSpecs()     { tbcompat::Unported(tbcompat::Feature::TalentSpecs); }
inline void NotePool()      { tbcompat::Unported(tbcompat::Feature::PoolServices); }
inline void NoteFactory()   { tbcompat::Unported(tbcompat::Feature::BotFactory); }
inline void NoteFilters()   { tbcompat::Unported(tbcompat::Feature::ChatFilters); }
inline void NoteFlee()      { tbcompat::Unported(tbcompat::Feature::FleeManager); }
inline void NoteMisc()      { tbcompat::Unported(tbcompat::Feature::MiscLegacyServices); }
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
    NoteBoss();
    static BossTacticTable table;
    return table;
}

DungeonPlanTable& GetDungeonPlans()
{
    NoteBoss();
    static DungeonPlanTable table;
    return table;
}

void LoadBossTactics() { NoteBoss(); }
void LoadBossStands() { NoteBoss(); }
void LoadDungeonPlans() { NoteBoss(); }

// BossTacticsActions.cpp
// The per-update bracket PlayerbotAI::UpdateAI opens around the scan (PlayerbotAI.cpp:383): it opens no scan slot here, so it is not counted
// (it was the hottest unported counter: once per bot per tick, no feature behind it; R8).
BossTacticsScanScope::BossTacticsScanScope() {}
BossTacticsScanScope::~BossTacticsScanScope() {}
bool AoeTauntStillNeeded(PlayerbotAI*, Player*) { NoteBoss(); return false; }
bool AreaTauntRefusedByPost(PlayerbotAI*, Player*, const char*) { NoteBoss(); return false; }
bool ClampMoveToBossAnchor(PlayerbotAI*, Player*, float&, float&, float&) { NoteBoss(); return false; }
bool DispelCastBlocked(PlayerbotAI*, Unit*, std::uint32_t) { NoteBoss(); return false; }
BossStandTarget FindBossStand(PlayerbotAI*, Player*) { NoteBoss(); return BossStandTarget{}; }
TankPostHealDuty FindTankPostHealDuty(PlayerbotAI*, Player*) { NoteBoss(); return TankPostHealDuty{}; }
std::uint32_t GateHoldFighters(PlayerbotAI*, std::uint32_t, float) { NoteBoss(); return 0; }
// "Whether a healer may top up the target": with no boss row the Turtle answer is today's mana rule; the slice's healer path is
// not reached by a warrior, and true (no reserve refuses) is the permissive end of it.
bool HealTopUpAllowedFor(PlayerbotAI*, Player*, Unit*) { NoteBoss(); return true; }
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
bool TauntTargetHeldByOtherTank(Player*, Unit*) { NoteBoss(); return false; }

// The multipliers the slice's combat strategies push (CombatStrategy.cpp, DungeonStrategy.cpp, AvoidAoeStrategy): 1.0 = no change.
// The engine evaluates every multiplier for every action candidate on every tick, so counting them made them the hottest boss-tactics
// counter by far (R8); they answer 1.0 without a count, the rows above and below stay counted.
float CharmedAllyHoldMultiplier::GetValue(Action*) { return 1.0f; }
float TrapHoldMultiplier::GetValue(Action*) { return 1.0f; }

// Fankriss*Actions.cpp, FankrissSpawnTarget.cpp, FankrissGauntletPolicy
bool FankrissBlocksAoe(PlayerbotAI*, uint32) { NoteBoss(); return false; }
bool FankrissBurnSpawn(PlayerbotAI*, Unit*) { NoteBoss(); return false; }
bool FankrissDpsMayAttack(PlayerbotAI*, Unit*) { NoteBoss(); return true; }   // "may attack": no Fankriss rule forbids it
bool FankrissHealReachable(PlayerbotAI*, Unit*) { NoteBoss(); return true; }  // "reachable": no Fankriss rule says otherwise
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

// ThreatStrategy.cpp, PositionAction.cpp
bool ThreatResetHolds(PlayerbotAI*, Player*, Unit*) { NoteBoss(); return false; }
bool StayWalkDueNow(PlayerbotAI*, Player*, PositionEntry const&) { NoteBoss(); return false; }

// =====================================================================================================================
// Startup tables of other managers. Owner files: GuildOrderSources.cpp, GuildOrderEnchants.cpp. Nothing is built.
// =====================================================================================================================
void BuildGuildOrderEnchantIndex() { NotePool(); }
void BuildGuildOrderSourceIndex() { NotePool(); }
std::vector<GuildOrderEntrySource> GuildOrderSourcesFor(uint32_t) { NotePool(); return {}; }

// =====================================================================================================================
// Misc legacy services. Owner files named per line.
// =====================================================================================================================
// CastCancelLog.cpp: diagnostics only (a log line per cancelled cast), off.
void LogCastCancel(PlayerbotAI*, const char*, CurrentSpellTypes, float) { NoteMisc(); }
void LogCastCancelNonMelee(PlayerbotAI*, const char*, bool, float) { NoteMisc(); }
void LogCastCancelStop(PlayerbotAI*, const char*, float) { NoteMisc(); }
// FishAction.cpp
void EndFishingSession(PlayerbotAI*, char const*) { NoteMisc(); }
// BotAreaTriggers.cpp
void UpdateBotAreaTriggers(Player*, BotAreaTriggerState&, std::vector<uint32_t> const*) { NoteMisc(); }

// CheatAction.cpp: the mask of cheats a config string names. 0 = none (the cheat actions are not in the slice).
BotCheatMask CheatAction::GetCheatMask(std::string) { NoteMisc(); return BotCheatMask(0); }
// EmoteAction.cpp: how many chat variants a text emote has. 1 = only the plain one.
uint32 EmoteActionBase::GetNumberOfEmoteVariants(TextEmotes, uint8, uint8) { NoteMisc(); return 1; }
// MountValues.cpp
bool MountValue::IsMountSpell(uint32) { NoteMisc(); return false; }
// GrindTargetValue.cpp: "an instance target may be ground": no.
bool GrindTargetValue::IsAllowedInstanceTarget(PlayerbotAI*, Unit*) { NoteMisc(); return false; }
// UseMeetingStoneAction.cpp: no spot found beside the summoner.
bool SummonAction::FindSpotBeside(Player*, Player*, float, float, float&, float&, float&) { NoteMisc(); return false; }
// ItemUsageValue.cpp: the reagent and vendor tables are not built.
void ItemUsageValue::PopulateProfessionReagentIds() { NoteItems(); }
void ItemUsageValue::PopulateReagentItemIdsForCraftableItemIds() { NoteItems(); }
void ItemUsageValue::PopulateSoldByVendorItemIds() { NoteItems(); }
// EnemyPlayerValue.cpp, WorldPvpSeekChecks.cpp: no open-world PvP seeking.
float EnemyPlayerValue::GetMaxAttackDistance(Player*) { NoteMisc(); return 0.0f; }
bool WorldPvpSeekChecks::IsSeeking(PlayerbotAI*, Player*) { NoteMisc(); return false; }
bool WorldPvpSeekChecks::IsSeekTarget(PlayerbotAI*, Player*, Player*) { NoteMisc(); return false; }

// ChatFilter.cpp: the chain keeps no filter, a line is returned as it comes.
std::string ChatFilter::Filter(std::string message, std::string) { NoteFilters(); return message; }
CompositeChatFilter::CompositeChatFilter(PlayerbotAI* ai) : ChatFilter(ai) {}
CompositeChatFilter::~CompositeChatFilter() {}
std::string CompositeChatFilter::Filter(std::string message) { NoteFilters(); return message; }

// FleeManager.cpp: no destination is found, no mob veto.
IdleMobVeto::IdleMobVeto(PlayerbotAI*, Player*, std::list<ObjectGuid> const&, Unit*, float) { NoteFlee(); }
bool IdleMobVeto::Wakes(float, float) const { NoteFlee(); return false; }
bool FleeManager::CalculateDestination(float*, float*, float*, FleeFailureMemory const*, uint32) { NoteFlee(); return false; }
bool FleeManager::isUseful() { NoteFlee(); return false; }

// LootObjectStack.cpp: an empty stack that never loots.
LootObject::LootObject(const LootObject&) = default;
bool LootObject::IsLootPossible(Player*) { NoteLoot(); return false; }
WorldObject* LootObject::GetWorldObject(Player*) { NoteLoot(); return nullptr; }
bool LootObjectStack::Add(ObjectGuid, bool) { NoteLoot(); return false; }
void LootObjectStack::Remove(ObjectGuid) { NoteLoot(); }
void LootObjectStack::NoteApproachFailure(ObjectGuid) { NoteLoot(); }
bool LootObjectStack::IsAbandoned(ObjectGuid) { NoteLoot(); return false; }
}

// =====================================================================================================================
// Travel (owner files: ai/playerbot/TravelMgr.cpp, TravelNode.cpp). No node map, no destinations, no route.
// =====================================================================================================================
namespace ai
{
void TravelMgr::Clear() { NoteTravel(); }
void TravelMgr::LoadAreaLevels() { NoteTravel(); }
void TravelMgr::LoadQuestTravelTable() { NoteTravel(); }
void TravelMgr::LoadTaxiNodeZones() { NoteTravel(); }
int32 TravelMgr::GetAreaLevel(uint32) { NoteTravel(); return 0; }
void TravelMgr::SetNullTravelTarget(TravelTarget*) const { NoteTravel(); }
void TravelMgr::CollectMapTransferPortals(const WorldPosition&, uint32, std::vector<std::pair<WorldPosition, float>>&) const { NoteTravel(); }
// Distance across a map change: "no portal known" is the length of a direct line is not knowable; 0 keeps the plain distance.
float TravelMgr::MapTransDistance(const WorldPosition&, const WorldPosition&, bool) const { NoteTravel(); return 0.0f; }

TravelTarget::TravelTarget(PlayerbotAI* ai) : AiObject(ai) { NoteTravel(); }
TravelTarget::~TravelTarget() {}
bool TravelTarget::IsActive() { NoteTravel(); return false; }
void TravelTarget::SetStatus(TravelStatus) { NoteTravel(); }

WorldPosition TravelNodeMap::getMapOffset(uint32) { NoteTravel(); return WorldPosition(); }
TravelPath TravelNodeMap::getFullPath(WorldPosition, WorldPosition, Unit*) { NoteTravel(); return TravelPath(); }
void TravelPath::ClipPath(PlayerbotAI*, Unit*, bool, PackHoldResult*) { NoteTravel(); }
bool TravelPath::UpcommingSpecialMovement(WorldPosition, float, bool) { NoteTravel(); return false; }
bool TravelPath::cutTo(PathNodePoint, bool) { NoteTravel(); return false; }
void TravelPath::makeShortCut(WorldPosition, float, Unit*) { NoteTravel(); }

// The vtables of the destination classes TravelMgr's members construct: the key functions, with the "nothing is possible"
// answers the base TravelDestination gives.
std::string EntryTravelDestination::GetShortName() const { NoteTravel(); return ""; }
bool GrindTravelDestination::IsPossible(const PlayerTravelInfo&) const { NoteTravel(); return false; }
bool GrindTravelDestination::IsActive(Player*, const PlayerTravelInfo&) const { NoteTravel(); return false; }
std::string GrindTravelDestination::GetTitle() const { NoteTravel(); return "grind"; }
bool GatherTravelDestination::IsPossible(const PlayerTravelInfo&) const { NoteTravel(); return false; }
bool GatherTravelDestination::IsActive(Player*, const PlayerTravelInfo&) const { NoteTravel(); return false; }
std::string GatherTravelDestination::GetTitle() const { NoteTravel(); return "gather"; }
}

// =====================================================================================================================
// Managers with tables (owner files: ai/playerbot/RandomItemMgr.cpp, PlayerbotTextMgr.cpp, PlayerbotHelpMgr.cpp,
// Talentspec.cpp, PlayerbotFactory.cpp, PlayerbotDbStore.cpp, RandomBotFacade loaders in runtime/PlayerbotRuntimeFacade.cpp).
// =====================================================================================================================
RandomItemMgr::RandomItemMgr() {}
RandomItemMgr::~RandomItemMgr() {}
void RandomItemMgr::Init() { NoteItems(); }
uint32 RandomItemMgr::GetStatWeight(Player*, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetStatWeight(uint32, uint32) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetPlayerSpecId(Player*) { NoteItems(); return 0; }
uint32 RandomItemMgr::GetFood(uint32, uint32, uint32) { NoteItems(); return 0; }

// The bot text lookups answer the text name itself (an empty text would make the caller say nothing; the name is at least
// visible in a log). The locale priority is "none".
PlayerbotTextMgr::PlayerbotTextMgr() {}
PlayerbotTextMgr::~PlayerbotTextMgr() {}
void PlayerbotTextMgr::LoadBotTexts() { NoteTexts(); }
std::string PlayerbotTextMgr::GetBotText(std::string name) { NoteTexts(); return name; }
std::string PlayerbotTextMgr::GetBotText(std::string name, std::map<std::string, std::string>) { NoteTexts(); return name; }
int32 PlayerbotTextMgr::GetLocalePriority() { return 0; }

PlayerbotHelpMgr::PlayerbotHelpMgr() {}
PlayerbotHelpMgr::~PlayerbotHelpMgr() {}
void PlayerbotHelpMgr::LoadBotHelpTexts() { NoteTexts(); }

// Talentspec.cpp: no spec is read, no spec checks pass or fail (the premade spec config is not loaded).
void TalentSpec::GetTalents(uint32) { NoteSpecs(); }
void TalentSpec::ReadTalents(std::string) { NoteSpecs(); }
void TalentSpec::ReadTalents(Player*) { NoteSpecs(); }
bool TalentSpec::CheckTalents(uint32, std::ostringstream*) { NoteSpecs(); return false; }
bool TalentSpec::CheckTalentLink(std::string, std::ostringstream*) { NoteSpecs(); return false; }
uint32 TalentSpec::LeveltoPoints(uint32) { NoteSpecs(); return 0; }
void TalentSpec::SetPublicNote(Player*) { NoteSpecs(); }

// PlayerbotFactory.cpp: no ammo and no pet set-up.
void PlayerbotFactory::InitAmmo() { NoteFactory(); }
void PlayerbotFactory::InitPet(bool) { NoteFactory(); }
void PlayerbotFactory::InitPetSpells() { NoteFactory(); }
bool PlayerbotFactory::PetAutoTrainAllowed(Player*) { NoteFactory(); return false; }
bool PlayerbotFactory::TrainHunterPet(Player*, Pet*, bool, std::function<bool(uint32)> const&) { NoteFactory(); return false; }

// PlayerbotDbStore.cpp: nothing is stored on ForeverCore (the ai_playerbot tables do not exist), so a load finds nothing.
void PlayerbotDbStore::Load(PlayerbotAI*, std::string) { NoteMisc(); }

// RandomBotFacade loaders (runtime/PlayerbotRuntimeFacade.cpp): the pool caches are not built.
void RandomBotFacade::LoadNamedLocationCache() { NotePool(); }
void RandomBotFacade::LoadBattleMastersCache() { NotePool(); }
void RandomBotFacade::LoadAuctionPrices() { NotePool(); }
