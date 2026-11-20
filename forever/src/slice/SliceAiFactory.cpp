// Slice 1 (slice 2 additions: the priest, R3): the AiFactory of the slice build (FOREVER-SLICE1-SPEC section 4), compiled INSTEAD of ai/playerbot/AiFactory.cpp.
//
// ai/playerbot/AiFactory.cpp is 2,200 lines of per-class kits, config strings, hire / commander / world-PvP rules and
// the context augmenters of other modules. The slice has one warrior that follows its master, takes the master's target
// and fights with the under-30 arms kit, so this file defines only the members the slice's sources call (found with
// `grep AiFactory::` over forever/SLICE1.txt: PlayerbotAI.cpp, ChatHelper.cpp):
//
//   createAiObjectContext, createCombatEngine, createNonCombatEngine, createDeadEngine, createReactionEngine,
//   AddDefaultCombatStrategies, AddDefaultNonCombatStrategies, AddDefaultDeadStrategies, AddDefaultReactionStrategies,
//   GetPlayerSpecTab, GetPlayerSpecTabs, GetPlayerRoles(const Player*)
//
// Everything else the header declares stays undefined: a reference to it fails at link time, which is the point (R6).
//
// Defaults, from the spec: combat "arms", "dps assist", "close", "buff"; non-combat "arms", "dps assist", "follow";
// the reaction engine gets only the class strategy and the dead engine only "dead" (slice 4); the talent tab is 0 (arms) whatever the bot's talents.
// Slice 2 (FOREVER-SLICE2-SPEC section 3): a priest gets its own context (PriestAiObjectContext, compiled as it is) and, on tab 0
// (discipline: the slice never reads talents), combat "discipline", "offdps", "dps assist", "ranged", "buff", "cast time"; non-combat
// "discipline", "dps assist", "follow", "buff", "food", "loot", "default"; reaction "discipline". The full factory's "flee", "cure",
// "cc", "aoe" and "boost" are left out on purpose (the spec's list). The warrior's non-combat set gains "loot" and "default" (the
// money looting and its "loot response" event; live test observation k has both bots open their corpse), its combat set is unchanged. A strategy name the
// context does not know is skipped by the engine, and logged once by the guard (SliceMissingKeyGuard.h).
// Slice 3 (FOREVER-SLICE3-SPEC section 3, lead decisions D1/D2): the warrior has a ROLE, read from the forced role (`.tb role` sets the player-wide
// stored role and the bot's own; PlayerbotAI::ResetStrategies re-runs this file afterwards). tank (AiFactory.cpp:727, :1445, without "pull back"):
// combat "protection", "tank assist", "pull", "close", "tank face"; non-combat "protection", "tank assist", "follow", "food", "default". Any other
// warrior (dps): the arms kit as before plus "threat" (where KeepsPartyThreatBackoff says so) and non-combat "food". The priest gets "threat" the same
// way. Left out on purpose (spec): "pull back", "behind", "aoe", "cc", "boost", "should pull" (autonomous pulls), "chat".
// Slice 8 (FOREVER-SLICE-MAGE-ROGUE-SPEC sections 2 and 6, lead decisions D1-D5; forever/docs/SLICE8-H-NOTES.md): a mage and a rogue get their own contexts
// (MageAiObjectContext, RogueAiObjectContext, compiled as they are), the talent tab 1 the full factory gives them without talents (AiFactory.cpp:169-185: mage fire,
// rogue combat), a damage role, and these kits. Mage: combat "fire", "dps assist", "ranged", "cc", "buff", "boost" (+ "threat" where KeepsPartyThreatBackoff says so);
// non-combat "fire", "dps assist", "follow", "buff", "food", "default"; reaction "fire". Rogue: combat "combat", "dps assist", "close", "behind", "cc", "buff", "boost" (+ "threat");
// non-combat "combat", "dps assist", "follow", "buff", "food", "default"; reaction "combat". Left out on purpose (the spec's list): the full kits' "flee", "cure", "aoe" (both),
// the mage's "loot", and for the rogue "stealth", "poisons" (D4: stealth is measured by a toggle, poisons need level 20). The rogue KEEPS "cc" (G8 kit decision, M8 live test e): Kick and
// Kick on enemy healer live in RogueCcStrategy; its Sap rows need a real player master (D3: no Sap in this slice) and stay silent in a bot-led run.
// Slice 10 (FOREVER-SLICE10-HYBRIDS-SPEC sections 2 and 7, lead decisions D1-D6; forever/docs/SLICE10-H-NOTES.md): a druid, a paladin and a shaman get their own contexts
// (DruidAiObjectContext, PaladinAiObjectContext, ShamanAiObjectContext, compiled as they are), a talent tab read from the ORDERED role (the slice never reads talents:
// druid tank 1 feral / otherwise 2 restoration; paladin tank 1 protection / otherwise 0 holy; shaman 2 restoration), a healer role when none was ordered, and these kits
// (HybridKitOf below). Bear druid (forced role tank): combat "tank feral", "tank assist", "pull", "close", "tank face", "cure", "buff"; non-combat "tank feral", "tank assist",
// "follow", "cure", "buff", "food", "default"; reaction "tank feral". Restoration druid: combat "restoration", "dps assist", "ranged", "cure", "buff", "cast time", "heal interrupt";
// non-combat "restoration", "dps assist", "follow", "cure", "buff", "food", "default"; reaction "restoration". Holy paladin: combat "holy", "dps assist", "ranged", "cure", "buff", "aura",
// "blessing", "cast time", "heal interrupt"; non-combat "holy", "dps assist", "follow", "cure", "buff", "food", "default", "aura", "blessing"; reaction "holy". Protection paladin
// (recorded only, D1): combat "protection", "tank assist", "pull", "close", "tank face", "cure", "aura", "blessing", "buff"; non-combat "protection", "tank assist" + the same tail; reaction
// "protection". Restoration shaman: combat "restoration", "dps assist", "ranged", "cure", "totems", "buff", "cast time", "heal interrupt"; non-combat "restoration", "dps assist", "follow",
// "cure", "buff", "food", "default" (no "totems" out of combat: D5, the totems drop in combat only); reaction "restoration". A tank kit (bear, protection) returns before the threat rule, as
// the warrior's does (a tank never backs off the threat it holds; the spec's lists name "threat" in the two tank kits, but KeepsPartyThreatBackoff is false for a tank, so the strategy was
// never added to the warrior's; the kits here follow the warrior's). The three healer kits get "threat" from KeepsPartyThreatBackoff as the priest does. Left out on purpose (the spec's
// list): the full kits' "aoe", "cc", "boost", "flee", "pull back", "offheal"/"offdps", "stealth", "oom melee", the druid's under-level-10 "leveling" kit, every cat, balance, retribution,
// enhancement and elemental kit, and the Turtle shaman tank kit (a shaman ordered to tank keeps the restoration kit: `.tb role <shaman> tank` is not supported in this slice).
// Slice 11 (FOREVER-SLICE11-PETS-SPEC sections 2 and 10, lead decisions D1-D6; forever/docs/SLICE11-NOTES.md): a hunter and a warlock get their own contexts
// (HunterAiObjectContext, WarlockAiObjectContext, compiled as they are), a damage role, and these kits. Hunter on tab 1 (marksmanship, the tab the full factory's
// `else` gives it; the slice never reads talents): combat "marksmanship", "dps assist", "ranged", "aspect", "sting", "pet", "buff" (+ "threat" where
// KeepsPartyThreatBackoff says so); non-combat "marksmanship", "dps assist", "aspect", "pet", "buff", "follow", "food", "default"; reaction "marksmanship".
// Warlock on tab 0 (affliction): combat "affliction", "dps assist", "ranged", "pet", "curse", "buff" (+ "threat"); non-combat "affliction", "dps assist", "pet",
// "pet voidwalker", "buff", "follow", "food", "default"; reaction "affliction". Left out on purpose (the spec's list): the full kits' "cc" (Scare Beast, Freezing
// Trap through Feign Death, Fear), "aoe" (Multi-Shot, Rain of Fire), "boost", the warlock's "flee".
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/ReactionEngine.h"
#include "playerbot/strategy/warrior/WarriorAiObjectContext.h"
#include "playerbot/strategy/priest/PriestAiObjectContext.h"
#include "playerbot/strategy/mage/MageAiObjectContext.h"
#include "playerbot/strategy/rogue/RogueAiObjectContext.h"
#include "playerbot/strategy/druid/DruidAiObjectContext.h"
#include "playerbot/strategy/paladin/PaladinAiObjectContext.h"
#include "playerbot/strategy/shaman/ShamanAiObjectContext.h"
#include "playerbot/strategy/hunter/HunterAiObjectContext.h"
#include "playerbot/strategy/warlock/WarlockAiObjectContext.h"
#include "../../../runtime/PlayerbotAIStorage.h"
#include "../../../runtime/CommandedGroupQuery.h"
#include "../../../runtime/RemoteCommanderPolicy.h"
#include "playerbot/RandomBotFacade.h"
#include "SliceLog.h"
#include <map>
#include "SliceHybridPolicy.h"
#include "playerbot/policy/world/FreeKitPolicy.h"
#include "SliceMissingKeyGuard.h"

// Slice 10: the role policy (SliceHybridPolicy.h) names the core's classes by number so it stays core-free; these are the ids it means.
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassWarrior) == unsigned(CLASS_WARRIOR), "SliceHybridPolicy.h: warrior id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassPaladin) == unsigned(CLASS_PALADIN), "SliceHybridPolicy.h: paladin id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassShaman) == unsigned(CLASS_SHAMAN), "SliceHybridPolicy.h: shaman id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassDruid) == unsigned(CLASS_DRUID), "SliceHybridPolicy.h: druid id");

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    // The slice registers the warrior's, the priest's, the mage's, the rogue's, the druid's, the paladin's and the shaman's class strategies (SliceAiObjectContext.cpp + the
    // class context compiled as it is); another class would run on the warrior's kit, so say so once.
    if (player && player->GetClass() == CLASS_PRIEST)
        return new slice::GuardedContext<PriestAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_MAGE)
        return new slice::GuardedContext<MageAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_ROGUE)
        return new slice::GuardedContext<RogueAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_DRUID)
        return new slice::GuardedContext<DruidAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_PALADIN)
        return new slice::GuardedContext<PaladinAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_SHAMAN)
        return new slice::GuardedContext<ShamanAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_HUNTER)
        return new slice::GuardedContext<HunterAiObjectContext>(ai);
    if (player && player->GetClass() == CLASS_WARLOCK)
        return new slice::GuardedContext<WarlockAiObjectContext>(ai);

    static bool warned = false;
    if (player && player->GetClass() != CLASS_WARRIOR && !warned)
    {
        warned = true;
        slice::LogWarn("slice: " + player->GetName() + " is not a warrior, priest, mage, rogue, druid, paladin, shaman, hunter or warlock; the slice build gives every other bot the warrior context");
    }
    return new slice::GuardedContext<WarriorAiObjectContext>(ai);
}



std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(const Player* /*player*/)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;
    return tabs;
}

namespace
{
// The role the bot was ordered to play (`.tb role`): the bot's own forced role, else the player-wide stored one (PlayerbotAI::IsTank reads the
// stored one first, PlayerbotAI.cpp:4339). 0 = none. PlayerbotAI::SetForcedRole stores 1 = tank, 2 = healer, anything else = damage.
uint8 OrderedRole(Player const* player, PlayerbotAI* const facade)
{
    if (!player)
        return 0;
    uint8 const stored = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
    if (stored != static_cast<uint8>(BOT_ROLE_NONE))
        return stored;
    uint8 const own = facade ? facade->GetForcedRole() : 0;       // 1 = tank, 2 = healer, anything else = damage (PlayerbotAI.cpp:4263)
    return own == 0 ? 0 : (own == 1 ? static_cast<uint8>(BOT_ROLE_TANK) : (own == 2 ? static_cast<uint8>(BOT_ROLE_HEALER) : static_cast<uint8>(BOT_ROLE_DPS)));
}

bool WarriorIsTank(Player const* player, PlayerbotAI* const facade)
{
    return player && player->GetClass() == CLASS_WARRIOR && OrderedRole(player, facade) == static_cast<uint8>(BOT_ROLE_TANK);
}

// The ordered role as GetPlayerRoles reads it, for callers that have no PlayerbotAI at hand (GetPlayerSpecTab): the player-wide stored role, else the bot's own
// forced role when its AI is already stored (it is not while the AI is being constructed: then only the stored role counts).
uint8 OrderedRoleNoFacade(Player const* player)
{
    if (!player)
        return static_cast<uint8>(BOT_ROLE_NONE);
    uint8 ordered = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
    if (ordered == static_cast<uint8>(BOT_ROLE_NONE))
        if (PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player->GetObjectGuid()))
            ordered = botAi->GetForcedRole() == 1 ? static_cast<uint8>(BOT_ROLE_TANK) :
                      botAi->GetForcedRole() == 2 ? static_cast<uint8>(BOT_ROLE_HEALER) :
                      botAi->GetForcedRole() ? static_cast<uint8>(BOT_ROLE_DPS) : static_cast<uint8>(BOT_ROLE_NONE);
    return ordered;
}

// Slice 10: the kit a druid, paladin or shaman plays (spec section 2, D1/D2). Only the tank order changes it: a druid ordered to tank is the bear and a paladin the protection
// paladin (recorded only); every other order or none is the healer kit (restoration druid, holy paladin, restoration shaman). A shaman never tanks here (D2).
enum class HybridKit { None, Bear, RestoDruid, HolyPaladin, ProtPaladin, RestoShaman };

HybridKit HybridKitOf(Player const* player, uint8 orderedRole)
{
    if (!player)
        return HybridKit::None;
    bool const tank = orderedRole == static_cast<uint8>(BOT_ROLE_TANK);
    switch (player->GetClass())
    {
        case CLASS_DRUID: return tank ? HybridKit::Bear : HybridKit::RestoDruid;
        case CLASS_PALADIN: return tank ? HybridKit::ProtPaladin : HybridKit::HolyPaladin;
        case CLASS_SHAMAN: return HybridKit::RestoShaman;
        default: return HybridKit::None;
    }
}

bool IsTankKit(HybridKit kit)
{
    return kit == HybridKit::Bear || kit == HybridKit::ProtPaladin;
}

// The class strategy that names the kit (the reaction engine's one strategy, and the spec strategy of the combat and non-combat kits).
char const* HybridSpecStrategy(HybridKit kit)
{
    switch (kit)
    {
        case HybridKit::Bear: return "tank feral";
        case HybridKit::RestoDruid: return "restoration";
        case HybridKit::HolyPaladin: return "holy";
        case HybridKit::ProtPaladin: return "protection";
        case HybridKit::RestoShaman: return "restoration";
        default: return nullptr;
    }
}
}

int AiFactory::GetPlayerSpecTab(const Player* player)
{
    // The slice never reads talents, so the tab comes from the class and the ordered role. A mage is fire and a rogue combat (tab 1: AiFactory.cpp:169-185 gives both that tab
    // without talents; GenericMageStrategy.cpp:119 and :140 read it); a hunter is marksmanship (tab 1: AiFactory.cpp:858-873). A druid, paladin or shaman gets the tab its kit
    // belongs to (the full factory's tabs: DruidAiObjectContext / PaladinAiObjectContext / ShamanAiObjectContext strategies read it, e.g. GenericDruidStrategy.cpp:68): druid
    // feral 1 for the bear, restoration 2 otherwise; paladin protection 1 for the protection kit, holy 0 otherwise; shaman restoration 2. Every other class keeps tab 0
    // (warrior arms, priest discipline, warlock affliction).
    if (player && (player->GetClass() == CLASS_MAGE || player->GetClass() == CLASS_ROGUE))
        return 1;
    if (player && player->GetClass() == CLASS_HUNTER)
        return 1;
    // A warrior ordered to tank is protection (tab 2), so the item scoring picks the protection scale and keeps the shield.
    if (player && player->GetClass() == CLASS_WARRIOR && OrderedRoleNoFacade(player) == static_cast<uint8>(BOT_ROLE_TANK))
        return 2;
    if (player)
    {
        switch (HybridKitOf(player, OrderedRoleNoFacade(player)))
        {
            case HybridKit::Bear: return 1;
            case HybridKit::RestoDruid: return 2;
            case HybridKit::HolyPaladin: return 0;
            case HybridKit::ProtPaladin: return 1;
            case HybridKit::RestoShaman: return 2;
            default: break;
        }
    }
    return 0; // arms
}

// The full factory's text (AiFactory.cpp:633-642), compiled here because AiFactory.cpp is not in the slice: non-tank bots of a party with a real
// player master or of a commanded party back off the tank's threat. The full factory also calls BotCommands::NoteCommanderThreat (the commander's
// "this strategy came from the binding" mark, undone when the commander unbinds); the slice has no unbind, so the mark is not kept (NEEDS FROM
// host: none; SLICE3-P-NOTES.md section 6).
bool AiFactory::KeepsPartyThreatBackoff(Player* player, PlayerbotAI* const facade)
{
    if (!player || !facade || player->InBattleGround())
        return false;
    bool const isTank = PlayerbotAI::IsTank(player);
    if (facade->HasRealPlayerMaster())
        return !isTank;
    bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(player);
    bool const keeps = TortoiseBots::RemoteCommander::KeepsCommandedThreatBackoff(commanded, isTank);
    // N8: one line per bot per CHANGE of the answer (the kit is rebuilt on every `.tb role`, `.tb master` and `free`), not one per rebuild.
    static std::map<ObjectGuid, int> logged;
    int const state = (commanded ? 1 : 0) | (isTank ? 2 : 0);
    auto itr = logged.find(player->GetObjectGuid());
    if (itr == logged.end() || itr->second != state)
    {
        logged[player->GetObjectGuid()] = state;
        ai::slice::LogInfo(std::string("slice: threat back-off for ") + player->GetName() + ": commanded=" + (commanded ? "yes" : "no") + " tank=" + (isTank ? "yes" : "no"));
    }
    return keeps;
}

BotRoles AiFactory::GetPlayerRoles(const Player* player)
{
    // The role the bot was ordered to play comes first (spec section 3: "the forced role first"), as PlayerbotAI::IsTank reads it.
    if (player)
    {
        uint8 ordered = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
        // The bot's own (AI-level) forced role too: the full GetPlayerRoles(player) ignores it (AiFactory.cpp:586-592), but a host that sets only
        // `SetForcedRole` must still be heard. The AI is not stored yet while it is being constructed: then there is none to read.
        if (ordered == static_cast<uint8>(BOT_ROLE_NONE))
            if (PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player->GetObjectGuid()))
                ordered = botAi->GetForcedRole() == 1 ? static_cast<uint8>(BOT_ROLE_TANK) :
                          botAi->GetForcedRole() == 2 ? static_cast<uint8>(BOT_ROLE_HEALER) :
                          botAi->GetForcedRole() ? static_cast<uint8>(BOT_ROLE_DPS) : static_cast<uint8>(BOT_ROLE_NONE);
        if (ordered != static_cast<uint8>(BOT_ROLE_NONE))
            return static_cast<BotRoles>(ordered);
    }
    // Spec tab 0 of a warrior is a damage role (AiFactory.cpp GetPlayerRoles(cls, tab): tab 2 tanks, the others dps).
    if (player && player->GetClass() == CLASS_WARRIOR)
        return BOT_ROLE_DPS;
    // A priest on tab 0 or 1 (discipline, holy: the only tabs the slice gives it) heals (AiFactory.cpp GetPlayerRoles(cls, tab): tab 2 is dps).
    if (player && player->GetClass() == CLASS_PRIEST)
        return BOT_ROLE_HEALER;
    // Slice 8: a mage and a rogue are damage (spec section 2: GetPlayerRoles dps for both).
    if (player && (player->GetClass() == CLASS_MAGE || player->GetClass() == CLASS_ROGUE))
        return BOT_ROLE_DPS;
    // Slice 10: with no ordered role a druid (restoration, tab 2), a paladin (holy, tab 0) and a shaman (restoration, tab 2) heal, as the tabs above give
    // (AiFactory.cpp:267-330); a tank order came first above (a bear druid is a tank, the run's lead reads it through PlayerbotAI::IsTank).
    if (player && (player->GetClass() == CLASS_DRUID || player->GetClass() == CLASS_PALADIN || player->GetClass() == CLASS_SHAMAN))
        return BOT_ROLE_HEALER;
    // Slice 11: a hunter and a warlock are damage (the Voidwalker off-tanks by order only: spec decision D4).
    if (player && (player->GetClass() == CLASS_HUNTER || player->GetClass() == CLASS_WARLOCK))
        return BOT_ROLE_DPS;
    return BOT_ROLE_NONE;
}

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    if (player && player->GetClass() == CLASS_PRIEST)
    {
        // "heal interrupt": the overheal cancel (a heal in its cast bar is cancelled when the patient no longer needs it), added by the full factory
        // to every healer's combat engine (AiFactory.cpp:1021-1022); R5 item 3 of slice 2. The priest falls through to the `threat` block below
        // (slice 3: a commanded healer backs off the tank's threat).
        combatEngine->addStrategies("discipline", "offdps", "dps assist", "ranged", "buff", "cast time", "heal interrupt", NULL);
    }
    else if (HybridKitOf(player, OrderedRole(player, facade)) != HybridKit::None)
    {
        switch (HybridKitOf(player, OrderedRole(player, facade)))
        {
            case HybridKit::Bear:
                // AiFactory.cpp:807-853 (tab 1, tanking) without "pull back" and "aoe", "cc", "boost".
                combatEngine->addStrategies("tank feral", "tank assist", "pull", "close", "tank face", "cure", "buff", NULL);
                return;                             // a tank never backs off the threat it holds
            case HybridKit::ProtPaladin:
                combatEngine->addStrategies("protection", "tank assist", "pull", "close", "tank face", "cure", "aura", "blessing", "buff", NULL);
                return;
            case HybridKit::RestoDruid:
                combatEngine->addStrategies("restoration", "dps assist", "ranged", "cure", "buff", "cast time", "heal interrupt", NULL);
                break;
            case HybridKit::HolyPaladin:
                combatEngine->addStrategies("holy", "dps assist", "ranged", "cure", "buff", "aura", "blessing", "cast time", "heal interrupt", NULL);
                break;
            case HybridKit::RestoShaman:
                combatEngine->addStrategies("restoration", "dps assist", "ranged", "cure", "totems", "buff", "cast time", "heal interrupt", NULL);
                break;
            default:
                break;
        }
    }
    else if (player && player->GetClass() == CLASS_MAGE)
        combatEngine->addStrategies("fire", "dps assist", "ranged", "cc", "buff", "boost", NULL);
    else if (player && player->GetClass() == CLASS_ROGUE)
        // "cc" gives the rogue Kick (RogueCcStrategy: kick, kick on enemy healer; its Sap rows need a real player master and stay silent): M8 live test.
        combatEngine->addStrategies("combat", "dps assist", "close", "behind", "cc", "buff", "boost", NULL);
    else if (player && player->GetClass() == CLASS_HUNTER)
        combatEngine->addStrategies("marksmanship", "dps assist", "ranged", "aspect", "sting", "pet", "buff", NULL);
    else if (player && player->GetClass() == CLASS_WARLOCK)
        combatEngine->addStrategies("affliction", "dps assist", "ranged", "pet", "curse", "buff", NULL);
    else if (WarriorIsTank(player, facade))
    {
        // AiFactory.cpp:727 without "pull back" (the return leg of a pull is out of the slice); "close" is the melee approach the damage kit has too.
        combatEngine->addStrategies("protection", "tank assist", "pull", "close", "tank face", NULL);
        return;                                     // a tank never backs off the threat it holds (KeepsPartyThreatBackoff is false for a tank)
    }
    else
        combatEngine->addStrategies("arms", "dps assist", "close", "buff", NULL);

    // Applied after the class kit, as AiFactory.cpp:1009 does.
    if (KeepsPartyThreatBackoff(player, facade) && !combatEngine->HasStrategy("threat"))
        combatEngine->addStrategy("threat");
}

// W1 (spec D4): a free bot's non-combat kit is its class spec strategy plus the world kit, both from the pure policy
// (policy/world/FreeKitPolicy.h; forever/tools/test_free_kit_policy.cpp). No follow, no wander, no rpg, travel, quests, vendors,
// or group/guild strategy.
static_assert(TortoiseBots::world::ClassWarrior == CLASS_WARRIOR, "FreeKitPolicy.h: warrior id");
static_assert(TortoiseBots::world::ClassPaladin == CLASS_PALADIN, "FreeKitPolicy.h: paladin id");
static_assert(TortoiseBots::world::ClassHunter == CLASS_HUNTER, "FreeKitPolicy.h: hunter id");
static_assert(TortoiseBots::world::ClassRogue == CLASS_ROGUE, "FreeKitPolicy.h: rogue id");
static_assert(TortoiseBots::world::ClassPriest == CLASS_PRIEST, "FreeKitPolicy.h: priest id");
static_assert(TortoiseBots::world::ClassShaman == CLASS_SHAMAN, "FreeKitPolicy.h: shaman id");
static_assert(TortoiseBots::world::ClassMage == CLASS_MAGE, "FreeKitPolicy.h: mage id");
static_assert(TortoiseBots::world::ClassWarlock == CLASS_WARLOCK, "FreeKitPolicy.h: warlock id");
static_assert(TortoiseBots::world::ClassDruid == CLASS_DRUID, "FreeKitPolicy.h: druid id");

void AiFactory::AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    if (nonCombatEngine)
        nonCombatEngine->addStrategy("maintenance");
    // A free bot plays the world on its own: grind targets, food, loot, rolls, the class spec and the engine's default handler.
    if (player && sRandomBotFacade.IsFreeBot(player) && !facade->HasRealPlayerMaster())
    {
        nonCombatEngine->addStrategy(TortoiseBots::world::FreeSpecStrategy(player->GetClass()));
        for (char const* const* kit = TortoiseBots::world::FreeWorldKit(); *kit; ++kit)
            nonCombatEngine->addStrategy(*kit);
        return;
    }
    if (player && player->GetClass() == CLASS_PRIEST)
    {
        nonCombatEngine->addStrategies("discipline", "dps assist", "follow", "buff", "food", "loot", "roll", "default", NULL);
        return;
    }
    if (HybridKit const kit = HybridKitOf(player, OrderedRole(player, facade)); kit != HybridKit::None)
    {
        // AiFactory.cpp:1284-1300, :1322-1345, :1408-1440: the spec strategy, the assist strategy of the role, then follow, cure, buff, food and the engine's own "default"
        // (+ the paladin's aura and blessing). No "totems" out of combat (D5), no "aoe", "cc", "boost", "loot".
        nonCombatEngine->addStrategies(HybridSpecStrategy(kit), IsTankKit(kit) ? "tank assist" : "dps assist", "follow", "cure", "buff", "food", "default", NULL);
        if (kit == HybridKit::HolyPaladin || kit == HybridKit::ProtPaladin)
            nonCombatEngine->addStrategies("aura", "blessing", NULL);
        return;
    }
    if (player && player->GetClass() == CLASS_MAGE)
    {
        nonCombatEngine->addStrategies("fire", "dps assist", "follow", "buff", "food", "default", NULL);
        return;
    }
    if (player && player->GetClass() == CLASS_ROGUE)
    {
        nonCombatEngine->addStrategies("combat", "dps assist", "follow", "buff", "food", "default", NULL);
        return;
    }
    if (player && player->GetClass() == CLASS_HUNTER)
    {
        nonCombatEngine->addStrategies("marksmanship", "dps assist", "aspect", "pet", "buff", "follow", "food", "default", NULL);
        return;
    }
    if (player && player->GetClass() == CLASS_WARLOCK)
    {
        nonCombatEngine->addStrategies("affliction", "dps assist", "pet", "pet voidwalker", "buff", "follow", "food", "default", NULL);
        return;
    }
    if (WarriorIsTank(player, facade))
    {
        // AiFactory.cpp:1445 plus the follow the slice's movement mode needs, food (the rest between pulls) and the engine's own "default".
        // Slice 9 (L9): the tank (the run's leader and the carrier of plan items) loots and rolls like the others: "loot" (corpses, chests) and "roll"
        // (group loot, greed only). The legacy dungeon-run feature has its own loot steps, DungeonRunLootFfa stays 0 (FOREVER-SLICE9-SPEC 5d).
        nonCombatEngine->addStrategies("protection", "tank assist", "follow", "food", "loot", "roll", "default", NULL);
        return;
    }
    nonCombatEngine->addStrategies("arms", "dps assist", "follow", "food", "loot", "roll", "default", NULL);
}

void AiFactory::AddDefaultDeadStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, Engine* deadEngine)
{
    // Slice 4 (FOREVER-SLICE4-SPEC section 2): the dead engine runs "dead" only (the full factory adds "dead stay default follow group",
    // AiFactory.cpp:1776; the stay, follow and group parts are the corpse run, which is out of the slice). "dead" fires `auto release`
    // (the dungeon-crew rule: runtime/RunReleaseHold.h); `accept resurrect` is called by the host when the core's resurrection request arrives
    // (the `resurrect request` trigger is not registered: spec section 2); every other trigger of DeadStrategy.cpp names a key the slice
    // does not register on purpose (repop, self resurrect, spirit healer, find corpse, revive from corpse, corpse near, falling far, move
    // long stuck, resurrect request, bg reset objective force, the `val` triggers): the engine skips them, each is logged once as
    // `slice: missing ...`, and each is a thing the bot then does LESS (forever/docs/SLICE4-P-NOTES.md section 4).
    deadEngine->addStrategy("dead");
}

void AiFactory::AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine)
{
    // Reactions (AoE avoidance, potions, chat) are out of the slice. The class strategy is not: its reaction triggers are the
    // ones that move the bot between the engines ("combat start" -> "set combat state", "combat end" -> "set non combat state",
    // "death" -> "set dead state": ClassStrategy::InitReactionTriggers), exactly as the full factory adds it (AiFactory.cpp:2085,
    // a warrior on the arms tab). Without it the bot entered the combat engine (AttackAction calls OnCombatStarted itself) and
    // never left it (R8 observation d).
    // The tank's spec strategy, as AiFactory.cpp:2085 (a warrior on the protection tab).
    // Slice 8: the mage's "fire" and the rogue's "combat" (AiFactory.cpp:2015-2060, tab 1).
    // Slice 10: the bear's "tank feral", the restoration druid's and shaman's "restoration", the paladin's "holy" or "protection" (AiFactory.cpp:1990-2125).
    // Slice 11: the hunter's "marksmanship" and the warlock's "affliction" (AiFactory.cpp:2034-2060 and :2138-2152, tab 1 and tab 0).
    if (char const* hybrid = HybridSpecStrategy(HybridKitOf(player, OrderedRole(player, facade))))
    {
        reactionEngine->addStrategy(hybrid);
        return;
    }
    char const* spec = "arms";
    if (player && player->GetClass() == CLASS_PRIEST)
        spec = "discipline";
    else if (player && player->GetClass() == CLASS_MAGE)
        spec = "fire";
    else if (player && player->GetClass() == CLASS_ROGUE)
        spec = "combat";
    else if (player && player->GetClass() == CLASS_HUNTER)
        spec = "marksmanship";
    else if (player && player->GetClass() == CLASS_WARLOCK)
        spec = "affliction";
    else if (WarriorIsTank(player, facade))
        spec = "protection";
    reactionEngine->addStrategy(spec);
}

Engine* AiFactory::createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* engine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_COMBAT, engine);
    AddDefaultCombatStrategies(player, facade, engine);
    return engine;
}

Engine* AiFactory::createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* nonCombatEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_NON_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_NON_COMBAT, nonCombatEngine);
    AddDefaultNonCombatStrategies(player, facade, nonCombatEngine);
    return nonCombatEngine;
}

Engine* AiFactory::createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* deadEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_DEAD);
    facade->SetEngine(BotState::BOT_STATE_DEAD, deadEngine);
    AddDefaultDeadStrategies(player, facade, deadEngine);
    return deadEngine;
}

ReactionEngine* AiFactory::createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    ReactionEngine* reactionEngine = new ReactionEngine(facade, AiObjectContext, BotState::BOT_STATE_REACTION);
    facade->SetEngine(BotState::BOT_STATE_REACTION, reactionEngine);
    AddDefaultReactionStrategies(player, facade, reactionEngine);
    return reactionEngine;
}
