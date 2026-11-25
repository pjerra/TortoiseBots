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
// (discipline: the slice never reads talents), combat "discipline", "offdps", "dps assist", "ranged", "cure", "buff", "cast time"; non-combat
// "discipline", "dps assist", "follow", "cure", "buff", "food", "loot", "default"; reaction "discipline". The full factory's "flee",
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
// "cure", "totems", "buff", "food", "default"; reaction "restoration". A tank kit (bear, protection) returns before the threat rule, as
// the warrior's does (a tank never backs off the threat it holds; the spec's lists name "threat" in the two tank kits, but KeepsPartyThreatBackoff is false for a tank, so the strategy was
// never added to the warrior's; the kits here follow the warrior's). The three healer kits get "threat" from KeepsPartyThreatBackoff as the priest does. Left out on purpose (the spec's
// list): the full kits' "aoe", "cc", "boost", "flee", "pull back", "offheal"/"offdps", "stealth", "oom melee", the druid's under-level-10 "leveling" kit, every cat, balance, retribution,
// enhancement and elemental kit, and the Turtle shaman tank kit (a shaman ordered to tank keeps the restoration kit: `.tb role <shaman> tank` is not supported in this slice).
// Slice 11 (FOREVER-SLICE11-PETS-SPEC sections 2 and 10, lead decisions D1-D6; forever/docs/SLICE11-NOTES.md): a hunter and a warlock get their own contexts
// (HunterAiObjectContext, WarlockAiObjectContext, compiled as they are), a damage role, and these kits. Hunter on tab 1 (marksmanship, the tab the full factory's
// `else` gives it; the slice never reads talents): combat "marksmanship", "dps assist", "ranged", "aspect", "sting", "pet", "buff" (+ "threat" where
// KeepsPartyThreatBackoff says so); non-combat "marksmanship", "dps assist", "aspect", "pet", "buff", "follow", "food", "default"; reaction "marksmanship".
// Warlock on tab 0 (affliction): combat "affliction", "dps assist", "ranged", "pet", "curse", "buff" (+ "threat"); non-combat "affliction", "dps assist", "pet",
// "pet auto", "buff", "follow", "food", "default"; reaction "affliction". "pet auto" is the default demon choice (the strongest demon the warlock knows, the Imp at
// level 1); the manual "pet voidwalker" here would be read as a player order and would stand the spec pet rows down. Left out on purpose (the spec's list): the full
// kits' "cc" (Scare Beast, Freezing Trap through Feign Death, Fear), "aoe" (Multi-Shot, Rain of Fire), "boost", the warlock's "flee".
#include "compat/botpch_forever.h"
#include "compat/TbCompat_Unit.h"
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
#include "playerbot/GroupMembers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group.h"
#include "LFG.h"
#include "DB2Stores.h"
#include "TraitMgr.h"
#include "TraitPacketsCommon.h"
#include "SliceLog.h"
#include <functional>
#include <map>
#include <unordered_map>
#include "SliceHybridPolicy.h"
#include "SliceSpecPolicy.h"
#include "playerbot/policy/world/FreeKitPolicy.h"
#include "SliceMissingKeyGuard.h"

// Slice 10: the role policy (SliceHybridPolicy.h) names the core's classes by number so it stays core-free; these are the ids it means.
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassWarrior) == unsigned(CLASS_WARRIOR), "SliceHybridPolicy.h: warrior id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassPaladin) == unsigned(CLASS_PALADIN), "SliceHybridPolicy.h: paladin id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassShaman) == unsigned(CLASS_SHAMAN), "SliceHybridPolicy.h: shaman id");
static_assert(unsigned(TortoiseBots::slice::hybrid::ClassDruid) == unsigned(CLASS_DRUID), "SliceHybridPolicy.h: druid id");
static_assert(uint8(TortoiseBots::slice::hybrid::RoleTank) == uint8(BOT_ROLE_TANK), "SliceHybridPolicy.h: tank role");
static_assert(uint8(TortoiseBots::slice::hybrid::RoleHealer) == uint8(BOT_ROLE_HEALER), "SliceHybridPolicy.h: healer role");
static_assert(uint8(TortoiseBots::slice::hybrid::RoleDps) == uint8(BOT_ROLE_DPS), "SliceHybridPolicy.h: dps role");

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



namespace
{
std::unordered_map<int32, int> const& NodeTabMapForClass(uint8 classId)
{
    static std::map<uint8, std::unordered_map<int32, int>> cache;
    auto it = cache.find(classId);
    if (it != cache.end())
        return it->second;

    std::vector<TortoiseBots::slice::spec::TraitNodeLayout> layout;
    if (ChrSpecializationEntry const* spec = sDB2Manager.GetChrSpecializationByIndex(classId, 0))
    {
        WorldPackets::Traits::TraitConfig config;
        config.Type = TraitConfigType::Combat;
        config.ChrSpecializationID = spec->ID;
        for (TraitMgr::NodeEntryInfo const& e : TraitMgr::GetNodeEntriesForConfig(config))
        {
            if (TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(e.TraitNodeID))
                layout.push_back({ e.TraitNodeID, node->PosX, node->PosY });
        }
    }
    return cache.emplace(classId, TortoiseBots::slice::spec::BuildNodeTabMap(layout)).first->second;
}
}

std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(const Player* player)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;
    if (!player)
        return tabs;

    int32 const activeConfigId = *player->m_activePlayerData->ActiveCombatTraitConfigID;
    if (!activeConfigId)
        return tabs;
    UF::TraitConfig const* config = player->GetTraitConfig(activeConfigId);
    if (!config)
        return tabs;

    std::unordered_map<int32, int> const& nodeTab = NodeTabMapForClass(player->GetClass());
    for (UF::TraitEntry const& entry : config->Entries)
    {
        if (entry.Rank <= 0)
            continue;
        auto it = nodeTab.find(entry.TraitNodeID);
        if (it != nodeTab.end() && it->second >= 0 && it->second < 3)
            tabs[uint32(it->second)] += entry.Rank;
    }
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

bool HasTankSignal(Player const* player)
{
    if (!player)
        return false;
    switch (player->GetClass())
    {
        case CLASS_DRUID:
            return player->GetShapeshiftForm() == FORM_BEAR_FORM || player->GetShapeshiftForm() == FORM_DIRE_BEAR_FORM;
        case CLASS_PALADIN:
            return player->HasAura(25780);
        case CLASS_WARRIOR:
        {
            if (player->GetShapeshiftForm() == FORM_DEFENSIVE_STANCE)
                return true;
            Item const* offhand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            ItemPrototype const* proto = offhand ? GetItemProtoCompat(offhand) : nullptr;
            return proto && proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
        }
        default:
            return false;
    }
}

bool ClassCanHeal(uint8 classId)
{
    return classId == CLASS_PRIEST || classId == CLASS_SHAMAN || classId == CLASS_PALADIN || classId == CLASS_DRUID;
}

TortoiseBots::slice::hybrid::GroupMemberRoleInfo BuildMemberRoleInfo(Player const* player, uint8 forcedRole, Group const* group)
{
    TortoiseBots::slice::hybrid::GroupMemberRoleInfo info;
    info.guid = GuidKeyCompat(player->GetObjectGuid());
    info.classId = unsigned(player->GetClass());
    info.forcedRole = forcedRole;
    if (group)
    {
        uint8 const lfg = group->GetLfgRoles(player->GetGUID());
        info.lfgTank = (lfg & lfg::PLAYER_ROLE_TANK) != 0;
        info.lfgHealer = (lfg & lfg::PLAYER_ROLE_HEALER) != 0;
        info.lfgDps = (lfg & lfg::PLAYER_ROLE_DAMAGE) != 0;
    }
    info.tankSignal = HasTankSignal(player);
    info.canHeal = ClassCanHeal(player->GetClass());
    info.defaultRole = TortoiseBots::slice::spec::DefaultSpecRole(unsigned(player->GetClass()), player->GetLevel(), AiFactory::GetPlayerSpecTab(player));
    return info;
}

uint8 EffectiveRole(Player const* player, PlayerbotAI* const facade)
{
    if (!player)
        return static_cast<uint8>(BOT_ROLE_NONE);
    uint8 const ordered = facade ? OrderedRole(player, facade) : OrderedRoleNoFacade(player);
    if (ordered != static_cast<uint8>(BOT_ROLE_NONE))
        return ordered;

    Group* group = const_cast<Player*>(player)->GetGroup();
    TortoiseBots::slice::hybrid::GroupMemberRoleInfo const self = BuildMemberRoleInfo(player, ordered, group);
    std::vector<TortoiseBots::slice::hybrid::GroupMemberRoleInfo> members;
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            uint8 const mForced = member == player ? ordered : OrderedRoleNoFacade(member);
            members.push_back(BuildMemberRoleInfo(member, mForced, group));
        }
    }
    else
    {
        members.push_back(self);
    }
    return TortoiseBots::slice::hybrid::ResolveGroupRole(self, members, sPlayerbotAIConfig.substituteTank);
}

int KitSpecTab(Player const* player, PlayerbotAI* const facade)
{
    if (!player)
        return 0;
    int8 const forced = PlayerbotAIStorage::Instance().GetPlayerForcedSpecTab(player->GetObjectGuid());
    std::map<uint32, int32> const tabs = AiFactory::GetPlayerSpecTabs(player);
    uint8 const role = EffectiveRole(player, facade);
    return TortoiseBots::slice::spec::ResolveKitSpecTab(
        unsigned(player->GetClass()),
        player->GetLevel(),
        forced,
        role,
        tabs.at(0),
        tabs.at(1),
        tabs.at(2));
}
}

int AiFactory::GetPlayerSpecTab(const Player* player)
{
    if (!player)
        return 0;
    int8 const forced = PlayerbotAIStorage::Instance().GetPlayerForcedSpecTab(player->GetObjectGuid());
    std::map<uint32, int32> const tabs = GetPlayerSpecTabs(player);
    return TortoiseBots::slice::spec::PickSpecTab(
        unsigned(player->GetClass()),
        player->GetLevel(),
        forced,
        tabs.at(0),
        tabs.at(1),
        tabs.at(2));
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
    if (!player)
        return BOT_ROLE_NONE;
    return static_cast<BotRoles>(EffectiveRole(player, nullptr));
}

// BG2 (spec D9): inside a battleground the engines get the PvP kit on top of the class kit. Both kit functions below return from inside
// their class switches, so the additions run from a scope guard when the function ends, after the class kit has been added, whichever
// return it takes. The class kits themselves are not touched.
namespace
{
class AtScopeExit
{
public:
    explicit AtScopeExit(std::function<void()> action) : m_action(std::move(action)) {}
    ~AtScopeExit() { m_action(); }
    AtScopeExit(AtScopeExit const&) = delete;
    AtScopeExit& operator=(AtScopeExit const&) = delete;

private:
    std::function<void()> m_action;
};

// "pvp" lets the bot take an enemy player as a target (its "enemy player near" trigger), "duel" never belongs in a battleground. A
// free bot drops the chase-away strategies of the open world; a bound bot (a human master) keeps its escort movement.
void ApplyBattlegroundCombatKit(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    if (!player || !combatEngine || !player->InBattleground())
        return;
    combatEngine->addStrategy("pvp");
    combatEngine->removeStrategy("duel");
    combatEngine->removeStrategy("flee");
    combatEngine->removeStrategy("cast time");
    combatEngine->removeStrategy("threat");
    if (!(facade && facade->HasRealPlayerMaster()))
        combatEngine->removeStrategy("follow");
}

// "battleground" is the strategy PlayerbotAI::UpdateAIGroupMaster looks for in the non-combat engine of a bot inside a match (it resets the
// strategies on every tick without it). A free bot has no grind in a battleground.
void ApplyBattlegroundNonCombatKit(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    if (!player || !nonCombatEngine || !player->InBattleground())
        return;
    nonCombatEngine->addStrategies("pvp", "battleground", NULL);
    if (!(facade && facade->HasRealPlayerMaster()))
    {
        nonCombatEngine->removeStrategy("grind");
        nonCombatEngine->removeStrategy("follow");
    }
}
} // namespace

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    AtScopeExit const battlegroundKit([&] { ApplyBattlegroundCombatKit(player, facade, combatEngine); });
    if (!player)
        return;
    combatEngine->addStrategy("dungeon");
    combatEngine->addStrategy("racials");
    // TB gives every bot `pvp` (AiFactory.cpp:664-671). Outside a battleground its `enemy player near` row only sees
    // players already attacking the bot unless it holds the world-PvP seek role (EnemyPlayersValue.cpp).
    combatEngine->addStrategy("pvp");
    // A duel is fought in the combat state, so the combat engine needs the duel strategy too: TB adds it to the
    // combat kit of every bot outside a battleground (AiFactory.cpp:664-671).
    if (!player->InBattleGround())
        combatEngine->addStrategy("duel");
    int const tab = KitSpecTab(player, facade);

    switch (player->GetClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 2)
                combatEngine->addStrategies("shadow", "dps assist", "ranged", "flee", "cure", "buff", "cast time", "cc", "aoe", "boost", NULL);
            else
                combatEngine->addStrategies(tab == 1 ? "holy" : "discipline", "offdps", "dps assist", "ranged", "flee", "cure", "buff", "cast time", "heal interrupt", "cc", "aoe", "boost", NULL);
            break;
        }
        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                combatEngine->addStrategies("leveling", "dps assist", "cure", "cc", "aoe", "boost", NULL);
                break;
            }
            if (tab == 1)
            {
                if (EffectiveRole(player, facade) == static_cast<uint8>(BOT_ROLE_TANK))
                {
                    combatEngine->addStrategies("tank feral", "tank assist", "pull", "close", "tank face", "cure", "buff", "cc", "aoe", "boost", NULL);
                    return;
                }
                combatEngine->addStrategies("dps feral", "dps assist", "close", "behind", "cure", "buff", "cc", "aoe", "boost", NULL);
            }
            else if (tab == 0)
                combatEngine->addStrategies("balance", "dps assist", "ranged", "flee", "oom melee", "cure", "buff", "cc", "aoe", "boost", NULL);
            else
                combatEngine->addStrategies("restoration", "dps assist", "ranged", "flee", "cure", "buff", "cast time", "heal interrupt", "cc", "aoe", "boost", NULL);
            break;
        }
        case CLASS_PALADIN:
        {
            if (tab == 1)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "close", "tank face", "cure", "aura", "blessing", "buff protection pve", "cc", "aoe", "boost", NULL);
                return;
            }
            if (tab == 2)
                combatEngine->addStrategies("retribution", "dps assist", "close", "behind", "cure", "buff", "aura", "blessing", "cc", "aoe", "boost", NULL);
            else
                combatEngine->addStrategies("holy", "dps assist", "ranged", "flee", "cure", "buff", "aura", "blessing", "cast time", "heal interrupt", "cc", "aoe", "boost", NULL);
            break;
        }
        case CLASS_SHAMAN:
        {
            if (tab == 0)
                combatEngine->addStrategies("elemental", "dps assist", "ranged", "flee", "cure", "totems", "buff", "cc", "aoe", "boost", NULL);
            else if (tab == 1)
                combatEngine->addStrategies("enhancement", "dps assist", "close", "behind", "cure", "totems", "buff", "cc", "aoe", "boost", NULL);
            else
                combatEngine->addStrategies("restoration", "dps assist", "ranged", "flee", "cure", "totems", "buff", "cast time", "heal interrupt", "cc", "aoe", "boost", NULL);
            break;
        }
        case CLASS_MAGE:
            combatEngine->addStrategies(tab == 0 ? "arcane" : (tab == 2 ? "frost" : "fire"), "dps assist", "ranged", "flee", "cc", "buff", "boost", "aoe", "cure", NULL);
            break;
        case CLASS_ROGUE:
            combatEngine->addStrategies(tab == 0 ? "assassination" : (tab == 2 ? "subtlety" : "combat"), "dps assist", "close", "behind", "cc", "buff", "boost", "aoe", "stealth", "poisons", NULL);
            break;
        case CLASS_HUNTER:
            combatEngine->addStrategies(tab == 0 ? "beast mastery" : (tab == 2 ? "survival" : "marksmanship"), "dps assist", "ranged", "flee", "aspect", "sting", "pet", "buff", "cc", "aoe", "boost", NULL);
            break;
        case CLASS_WARLOCK:
            combatEngine->addStrategies(tab == 1 ? "demonology" : (tab == 2 ? "destruction" : "affliction"), "dps assist", "ranged", "flee", "pet", "curse", "buff", "cc", "aoe", "boost", NULL);
            break;
        case CLASS_WARRIOR:
        default:
        {
            if (tab == 2)
            {
                combatEngine->addStrategies("protection", "tank assist", "pull", "close", "tank face", "cc", "aoe", "boost", NULL);
                return;
            }
            combatEngine->addStrategies(tab == 1 ? "fury" : "arms", "dps assist", "close", "behind", "buff", "cc", "aoe", "boost", NULL);
            break;
        }
    }

    // Applied after the class kit, as AiFactory.cpp:1009 does.
    if (KeepsPartyThreatBackoff(player, facade) && !combatEngine->HasStrategy("threat"))
        combatEngine->addStrategy("threat");
}

// W1 (spec D4): a free bot's non-combat kit is its class spec strategy plus the world kit, both from the pure policy
// (policy/world/FreeKitPolicy.h; forever/tools/test_free_kit_policy.cpp). No follow, no wander, no rpg, quests, vendors,
// or group/guild strategy; travel is the one world move a free bot makes.
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
    AtScopeExit const battlegroundKit([&] { ApplyBattlegroundNonCombatKit(player, facade, nonCombatEngine); });
    nonCombatEngine->addStrategy("dungeon");
    if (nonCombatEngine)
    {
        nonCombatEngine->addStrategy("maintenance");
        nonCombatEngine->addStrategy("racials");
        // The mount toggle: without it CheckMountStateAction::isUseful refuses every
        // unmounted bot, so a bot that owns a mount would never ride it.
        nonCombatEngine->addStrategy("mount");
        // TB gives every bot outside a battleground the duel strategy (AiFactory.cpp:1489-1495): its rows answer the
        // challenge and take the duel's own opponent as the target, for a free bot and a hired one alike.
        if (player && !player->InBattleGround())
            nonCombatEngine->addStrategy("duel");
    }
    // A free bot plays the world on its own: grind targets, food, loot, rolls, the class spec and the engine's default handler.
    if (player && sRandomBotFacade.IsFreeBot(player) && !facade->HasRealPlayerMaster())
    {
        nonCombatEngine->addStrategy(TortoiseBots::world::FreeSpecStrategy(player->GetClass()));
        for (char const* const* kit = TortoiseBots::world::FreeWorldKit(); *kit; ++kit)
            nonCombatEngine->addStrategy(*kit);
        // Questing is the owner's config: with AiPlayerbot.AutoDoQuests off a free bot only grinds and travels, exactly
        // as the full build gates its rpg strategy.
        if (sPlayerbotAIConfig.autoDoQuests)
            nonCombatEngine->addStrategy("rpg");
        return;
    }
    if (!player)
        return;
    // Commanded bots greet a real player; a free bot gets the strategy from its own kit, and GreetAction gates pool-like
    // bots behind AiPlayerbot.RandomBotGreet.
    nonCombatEngine->addStrategy("emote");
    int const tab = KitSpecTab(player, facade);

    switch (player->GetClass())
    {
        case CLASS_PRIEST:
            nonCombatEngine->addStrategies(tab == 2 ? "shadow" : (tab == 1 ? "holy" : "discipline"), "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", NULL);
            return;
        case CLASS_DRUID:
        {
            if (player->GetLevel() < 10)
            {
                nonCombatEngine->addStrategies("leveling", "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", NULL);
                return;
            }
            if (tab == 1)
            {
                bool const tank = EffectiveRole(player, facade) == static_cast<uint8>(BOT_ROLE_TANK);
                nonCombatEngine->addStrategies(tank ? "tank feral" : "dps feral", tank ? "tank assist" : "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", "aoe", "cc", NULL);
                return;
            }
            nonCombatEngine->addStrategies(tab == 0 ? "balance" : "restoration", "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", "aoe", "cc", NULL);
            return;
        }
        case CLASS_PALADIN:
        {
            if (tab == 1)
                nonCombatEngine->addStrategies("protection", "tank assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "aura", "blessing", "buff protection pve", "boost", NULL);
            else
                nonCombatEngine->addStrategies(tab == 2 ? "retribution" : "holy", "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "aura", "blessing", "boost", NULL);
            return;
        }
        case CLASS_SHAMAN:
            nonCombatEngine->addStrategies(tab == 0 ? "elemental" : (tab == 1 ? "enhancement" : "restoration"), "dps assist", "follow", "cure", "totems", "buff", "food", "loot", "roll", "default", "boost", "aoe", NULL);
            return;
        case CLASS_MAGE:
            nonCombatEngine->addStrategies(tab == 0 ? "arcane" : (tab == 2 ? "frost" : "fire"), "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", NULL);
            return;
        case CLASS_ROGUE:
            nonCombatEngine->addStrategies(tab == 0 ? "assassination" : (tab == 2 ? "subtlety" : "combat"), "dps assist", "follow", "buff", "food", "loot", "roll", "default", "boost", "stealth", "poisons", NULL);
            return;
        case CLASS_HUNTER:
            nonCombatEngine->addStrategies(tab == 0 ? "beast mastery" : (tab == 2 ? "survival" : "marksmanship"), "dps assist", "aspect", "pet", "buff", "follow", "food", "loot", "roll", "default", "boost", "cc", "aoe", NULL);
            return;
        case CLASS_WARLOCK:
            nonCombatEngine->addStrategies(tab == 1 ? "demonology" : (tab == 2 ? "destruction" : "affliction"), "dps assist", "pet", "pet auto", "buff", "follow", "food", "loot", "roll", "default", "boost", NULL);
            return;
        case CLASS_WARRIOR:
        default:
        {
            if (tab == 2)
                nonCombatEngine->addStrategies("protection", "tank assist", "follow", "food", "loot", "roll", "default", "boost", NULL);
            else
                nonCombatEngine->addStrategies(tab == 1 ? "fury" : "arms", "dps assist", "follow", "food", "loot", "roll", "default", "boost", NULL);
            return;
        }
    }
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
    // The class strategy's reaction triggers are the
    // ones that move the bot between the engines ("combat start" -> "set combat state", "combat end" -> "set non combat state",
    // "death" -> "set dead state": ClassStrategy::InitReactionTriggers), exactly as the full factory adds it (AiFactory.cpp:2085,
    // a warrior on the arms tab). Without it the bot entered the combat engine (AttackAction calls OnCombatStarted itself) and
    // never left it (R8 observation d).
    // The tank's spec strategy, as AiFactory.cpp:2085 (a warrior on the protection tab).
    // Slice 8: the mage's "fire" and the rogue's "combat" (AiFactory.cpp:2015-2060, tab 1).
    // Slice 10: the bear's "tank feral", the restoration druid's and shaman's "restoration", the paladin's "holy" or "protection" (AiFactory.cpp:1990-2125).
    // Slice 11: the hunter's "marksmanship" and the warlock's "affliction" (AiFactory.cpp:2034-2060 and :2138-2152, tab 1 and tab 0).
    // S1-R2: the chat strategy runs beside the class kit on every class (the full factory's reaction set, AiFactory.cpp:1988, with "react",
    // "avoid aoe" and "potions"). Its triggers are the commands a player whispers or says in party chat
    // (ChatCommandHandlerStrategy.cpp, chat/PlayerbotAI::HandleCommands); the command the host queued reaches it at the next reaction pass.
    reactionEngine->addStrategy("chat");
    reactionEngine->addStrategies("react", "avoid aoe", "potions", NULL);
    if (!player)
        return;
    int const tab = KitSpecTab(player, facade);
    char const* spec = "arms";
    switch (player->GetClass())
    {
        case CLASS_PRIEST:  spec = tab == 2 ? "shadow" : (tab == 1 ? "holy" : "discipline"); break;
        case CLASS_MAGE:    spec = tab == 0 ? "arcane" : (tab == 2 ? "frost" : "fire"); break;
        case CLASS_ROGUE:   spec = tab == 0 ? "assassination" : (tab == 2 ? "subtlety" : "combat"); break;
        case CLASS_HUNTER:  spec = tab == 0 ? "beast mastery" : (tab == 2 ? "survival" : "marksmanship"); break;
        case CLASS_WARLOCK: spec = tab == 1 ? "demonology" : (tab == 2 ? "destruction" : "affliction"); break;
        case CLASS_PALADIN: spec = tab == 1 ? "protection" : (tab == 2 ? "retribution" : "holy"); break;
        case CLASS_SHAMAN:  spec = tab == 0 ? "elemental" : (tab == 1 ? "enhancement" : "restoration"); break;
        case CLASS_DRUID:
            if (player->GetLevel() < 10)
                spec = "leveling";
            else if (tab == 1)
                spec = EffectiveRole(player, facade) == static_cast<uint8>(BOT_ROLE_TANK) ? "tank feral" : "dps feral";
            else
                spec = tab == 0 ? "balance" : "restoration";
            break;
        case CLASS_WARRIOR:
        default:
            spec = tab == 2 ? "protection" : (tab == 1 ? "fury" : "arms");
            break;
    }
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
