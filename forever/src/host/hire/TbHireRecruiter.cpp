// The <Mercenary Hire> recruiter: a CreatureAI (not a CreatureScript, which on this core has no gossip hooks)
// that builds the whole wizard in code. The menu item we add carries a sender/action pair; the core hands the
// click back as a menu id and an order index, so OnGossipSelect reads the pair off the clicked item. Sender
// names the page, action packs the choice(s) made so far; every page re-validates the packed selection
// because a menu can outlive a level, a config reload or an unlock change.
#include "host/hire/TbHireRecruiter.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/hire/TbHireRecruiterPure.h"
#include "host/hire/TbHire.h"
#include "runtime/HireFeePolicy.h"
#include "runtime/HireSpecPolicy.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DB2Stores.h"
#include "GossipDef.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "compat/botpch_forever.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/ChatHelper.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <sstream>
#include <string>

namespace TortoiseBots::hire
{
namespace
{
using wizard::Choice;

constexpr uint32 kMaxMenuItems = 30;
// The gear-tier page's own "< Back". 540 is the confirm page's back (TbHire.h); a page cannot share it with
// its parent or the last page could never be left. 541 is above every sender the contract allocates.
constexpr uint32 kSenderTierBackToSpec = 541;

static_assert(wizard::kSenderRoot < kSenderWizardFirst, "the root sender must stay below the wizard range");
static_assert(wizard::kSenderTierBack > kSenderContractsLast, "tier senders must stay above the contracts range");

char const* ClassName(uint8 classId)
{
    char const* name = DB2Manager::GetChrClassName(classId);
    return name ? name : "Unknown";
}

char const* RaceName(uint8 race)
{
    char const* name = DB2Manager::GetChrRaceName(race);
    return name ? name : "Unknown";
}

wizard::OfferSide OfferSideFor(Player* player, Creature* recruiter)
{
    if (!player)
        return wizard::OfferSide::Both;
    // AiPlayerbot.HireCrossFactionNeutralInns: a recruiter whose faction belongs to neither side hires out
    // both factions' classes and races. Cross-faction grouping itself is the server's rule and the hire's.
    if (sPlayerbotAIConfig.hireCrossFactionNeutralInns && recruiter)
    {
        FactionTemplateEntry const* faction = recruiter->GetFactionTemplateEntry();
        if (faction && !(faction->FactionGroup & (FACTION_MASK_ALLIANCE | FACTION_MASK_HORDE)))
            return wizard::OfferSide::Both;
    }
    switch (player->GetTeam())
    {
        case ALLIANCE: return wizard::OfferSide::Alliance;
        case HORDE: return wizard::OfferSide::Horde;
        default: return wizard::OfferSide::Both;
    }
}

bool RaceOffered(Player* player, Creature* recruiter, uint8 classId, uint8 race)
{
    if (!sObjectMgr.GetPlayerInfo(race, classId))
        return false;
    switch (OfferSideFor(player, recruiter))
    {
        case wizard::OfferSide::Alliance: return Player::TeamForRace(race) == ALLIANCE;
        case wizard::OfferSide::Horde: return Player::TeamForRace(race) == HORDE;
        default: return true;
    }
}

uint32 GossipText(Creature const* recruiter)
{
    uint32 const entry = recruiter ? recruiter->GetEntry() : 0;
    if (!wizard::IsRecruiterEntry(entry))
        return DEFAULT_GOSSIP_MESSAGE;
    return wizard::GossipTextId(entry, sPlayerbotAIConfig.hireAllowPvP);
}

void AddItem(Player* player, GossipOptionNpc icon, std::string const& text, uint32 sender, uint32 action)
{
    AddGossipItemFor(player, icon, text, sender, action);
}

void Send(Player* player, Creature* recruiter)
{
    SendGossipMenuFor(player, GossipText(recruiter), recruiter);
}

void CloseWithHint(Player* player, char const* hint)
{
    CloseGossipMenuFor(player);
    if (hint && *hint)
        ChatHandler(player->GetSession()).SendSysMessage(hint);
}

bool FeeWaived(Player* player)
{
    return HireFeeWaived(int(player->GetSession()->GetSecurity()), sPlayerbotAIConfig.hireFreeMinSecurity);
}

std::string FeeLabel(Player* player, uint32 copper)
{
    if (FeeWaived(player))
        return "free (GM)";
    return ai::ChatHelper::formatMoney(copper);
}

void ShowClassMenu(Player* player, Creature* recruiter);
void ShowRecruiterRoot(Player* player, Creature* recruiter);

void ShowRaceMenu(Player* player, Creature* recruiter, uint8 classId)
{
    uint32 shown = 0;
    for (uint8 race = 1; race <= wizard::kMaxRace && shown + 1 < kMaxMenuItems; ++race)
    {
        if (!RaceOffered(player, recruiter, classId, race))
            continue;
        AddItem(player, GossipOptionNpc::None, RaceName(race),
            kSenderWizardFirst + 1, wizard::PackClassRace(classId, race));
        ++shown;
    }
    AddItem(player, GossipOptionNpc::None, "< Back",
        kSenderWizardFirst + 1, wizard::PackClassRace(classId, uint8(wizard::kActionBack)));
    Send(player, recruiter);
}

void ShowGenderMenu(Player* player, Creature* recruiter, uint8 classId, uint8 race)
{
    AddItem(player, GossipOptionNpc::None, "Male", kSenderWizardFirst + 2,
        wizard::PackClassRaceGender(classId, race, GENDER_MALE));
    AddItem(player, GossipOptionNpc::None, "Female", kSenderWizardFirst + 2,
        wizard::PackClassRaceGender(classId, race, GENDER_FEMALE));
    AddItem(player, GossipOptionNpc::None, "< Back", kSenderWizardFirst + 2,
        wizard::PackClassRaceGender(classId, race, uint8(wizard::kActionBack)));
    Send(player, recruiter);
}

void ShowSpecMenu(Player* player, Creature* recruiter, uint8 classId, uint8 race, uint8 gender)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(classId, specCount);
    uint32 const hireLevel = player->GetLevel();
    for (uint32 i = 0; i < specCount && i + 1 < kMaxMenuItems; ++i)
    {
        if (!HireSpecOffered(classId, int(i), hireLevel, sPlayerbotAIConfig.shamanTankEnabled))
            continue;
        AddItem(player, GossipOptionNpc::Battlemaster, specs[i].label, kSenderWizardFirst + 3,
            wizard::PackChoice({ classId, race, gender, uint8(i) }));
    }
    AddItem(player, GossipOptionNpc::None, "< Back", kSenderWizardFirst + 3,
        wizard::BackFrom(classId, race, gender));
    Send(player, recruiter);
}

void ShowConfirmMenu(Player* player, Creature* recruiter, Choice choice, uint8 gearTier)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(choice.classId, specCount);
    if (!specs || choice.specIndex >= specCount)
    {
        ShowSpecMenu(player, recruiter, choice.classId, choice.race, choice.gender);
        return;
    }
    uint32 const cost = QuoteFee(player, player->GetLevel(), gearTier);
    std::ostringstream label;
    label << "Hire " << RaceName(choice.race) << " " << ClassName(choice.classId)
          << " (" << specs[choice.specIndex].label << ")";
    if (gearTier > 0)
        label << " in " << TierName(gearTier) << " kit";
    label << " for " << FeeLabel(player, cost) << ". Confirm?";
    uint32 const packed = wizard::PackChoice(choice);
    AddItem(player, GossipOptionNpc::Vendor, label.str(), wizard::TierConfirmSender(gearTier), packed);
    AddItem(player, GossipOptionNpc::None, "< Back", wizard::kSenderTierBack, packed);
    Send(player, recruiter);
}

void ShowTierMenu(Player* player, Creature* recruiter, Choice choice, uint8 highestTier)
{
    uint32 const packed = wizard::PackChoice(choice);
    for (uint8 tier = 0; tier <= highestTier && tier <= wizard::kMaxTier; ++tier)
    {
        std::string const name = TierName(tier);
        if (name.empty())
            break;
        std::ostringstream label;
        label << name << " kit (" << FeeLabel(player, QuoteFee(player, player->GetLevel(), tier)) << ")";
        AddItem(player, GossipOptionNpc::Battlemaster, label.str(), wizard::TierStepSender(tier), packed);
    }
    AddItem(player, GossipOptionNpc::None, "< Back", kSenderTierBackToSpec, packed);
    Send(player, recruiter);
}

void ShowClassMenu(Player* player, Creature* recruiter)
{
    wizard::OfferSide const side = OfferSideFor(player, recruiter);
    for (uint32 i = 0; i < wizard::kHireClassCount; ++i)
    {
        uint8 const classId = wizard::kHireClasses[i];
        if (!wizard::ClassOffered(classId, side))
            continue;
        AddItem(player, GossipOptionNpc::None, ClassName(classId), kSenderWizardFirst, wizard::PackClass(classId));
    }
    Send(player, recruiter);
}

bool ShowTierOrConfirm(Player* player, Creature* recruiter, Choice choice)
{
    uint8 const highest = HighestUnlockedTier(player);
    if (wizard::TierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled, player->GetLevel(), highest))
    {
        ShowTierMenu(player, recruiter, choice, highest);
        return true;
    }
    ShowConfirmMenu(player, recruiter, choice, 0);
    return true;
}

// The click's choices are re-validated here, not only where the menu was drawn: a stale gossip window must
// not hire a class/race/gender the recruiter no longer offers, or above the unlocked tier.
bool ValidSelection(Player* player, Creature* recruiter, Choice const& choice, uint32& specCount)
{
    if (!wizard::ClassOffered(choice.classId, OfferSideFor(player, recruiter)))
        return false;
    if (!RaceOffered(player, recruiter, choice.classId, choice.race))
        return false;
    HireSpecOption const* specs = HireSpecOptions(choice.classId, specCount);
    return specs && choice.specIndex < specCount;
}

void HireNow(Player* player, Creature* recruiter, Choice const& choice, uint8 gearTier)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(choice.classId, specCount);
    Selection selection;
    selection.classId = choice.classId;
    selection.race = choice.race;
    selection.gender = choice.gender;
    selection.role = specs[choice.specIndex].role;
    selection.specIndex = int(choice.specIndex);
    selection.gearTier = gearTier;
    selection.anyFaction = OfferSideFor(player, recruiter) == wizard::OfferSide::Both;
    Outcome const outcome = Hire(player, selection, recruiter);
    CloseWithHint(player, outcome.message.c_str());
}

bool GatesOpen(Player* player)
{
    if (!Enabled())
    {
        CloseWithHint(player, "No mercenaries are mustering right now.");
        return false;
    }
    if (uint32(player->GetSession()->GetSecurity()) < sPlayerbotAIConfig.hireMinAccountSecurity)
    {
        CloseWithHint(player, "The recruiter sizes you up and shakes their head. (Your account may not hire.)");
        return false;
    }
    return true;
}

bool HandleSelect(Player* player, Creature* recruiter, uint32 sender, uint32 action)
{
    if (!player || !recruiter)
        return false;
    if (!GatesOpen(player))
        return true;

    if (sender == wizard::kSenderRoot)
    {
        if (action == wizard::kRootWorldBuffs && WorldBuffsVisible(player, recruiter))
            ShowWorldBuffMenu(player, recruiter);
        else if (action == wizard::kRootContracts && ContractsVisible(player, recruiter))
            ShowContractsMenu(player, recruiter);
        else
            ShowClassMenu(player, recruiter);
        return true;
    }

    // The contracts and world-buff pages own their senders; a page that is no longer visible falls back to
    // this recruiter's front page instead of handling the click.
    if (sender >= kSenderContractsFirst && sender <= kSenderContractsLast)
    {
        if (ContractsVisible(player, recruiter) && OnContractsSelect(player, recruiter, sender, action))
            return true;
        ShowRecruiterRoot(player, recruiter);
        return true;
    }
    if (sender >= kSenderWorldBuffFirst && sender <= kSenderWorldBuffLast)
    {
        if (WorldBuffsVisible(player, recruiter) && OnWorldBuffSelect(player, recruiter, sender, action))
            return true;
        ShowRecruiterRoot(player, recruiter);
        return true;
    }

    // Class step.
    if (sender == kSenderWizardFirst)
    {
        uint8 classId = 0;
        if (!wizard::DecodeClass(action, classId) ||
            !wizard::ClassOffered(classId, OfferSideFor(player, recruiter)))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        ShowRaceMenu(player, recruiter, classId);
        return true;
    }

    // Race step: "< Back" carries only the class.
    if (sender == kSenderWizardFirst + 1)
    {
        if (wizard::IsBack(action))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        uint8 classId = 0, race = 0;
        if (!wizard::DecodeClassRace(action, classId, race) ||
            !wizard::ClassOffered(classId, OfferSideFor(player, recruiter)))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        if (!RaceOffered(player, recruiter, classId, race))
        {
            ShowRaceMenu(player, recruiter, classId);
            return true;
        }
        ShowGenderMenu(player, recruiter, classId, race);
        return true;
    }

    // Gender step.
    if (sender == kSenderWizardFirst + 2)
    {
        if (wizard::IsBack(action))
        {
            uint8 const classId = uint8((action >> 16) & 0xFF);
            if (!wizard::ValidClass(classId))
            {
                ShowClassMenu(player, recruiter);
                return true;
            }
            ShowRaceMenu(player, recruiter, classId);
            return true;
        }
        uint8 classId = 0, race = 0, gender = 0;
        if (!wizard::DecodeClassRaceGender(action, classId, race, gender) ||
            !RaceOffered(player, recruiter, classId, race))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        ShowSpecMenu(player, recruiter, classId, race, gender);
        return true;
    }

    // Spec step.
    if (sender == kSenderWizardFirst + 3)
    {
        if (wizard::IsBack(action))
        {
            uint8 const classId = uint8((action >> 24) & 0xFF);
            uint8 const race = uint8((action >> 16) & 0xFF);
            uint8 const gender = uint8((action >> 8) & 0xFF);
            if (!wizard::ValidClass(classId) || !wizard::ValidRace(race) || !wizard::ValidGender(gender) ||
                !RaceOffered(player, recruiter, classId, race))
            {
                ShowClassMenu(player, recruiter);
                return true;
            }
            ShowGenderMenu(player, recruiter, classId, race);
            return true;
        }
        Choice choice;
        if (!wizard::DecodeChoice(action, choice))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        uint32 specCount = 0;
        if (!ValidSelection(player, recruiter, choice, specCount))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        if (!HireSpecOffered(choice.classId, int(choice.specIndex), player->GetLevel(),
                sPlayerbotAIConfig.shamanTankEnabled))
        {
            CloseWithHint(player, "Enhancement (Tank) is not available: it needs level 10 (Earthshaker Slam taunt), and the server may have it turned off.");
            return true;
        }
        ShowTierOrConfirm(player, recruiter, choice);
        return true;
    }

    // Gear-tier step and its confirm: the tier rides in the sender, the selection in the action.
    uint8 tierPick = 0;
    bool const isTierStep = wizard::DecodeTierStep(sender, tierPick);
    bool const isTierConfirm = !isTierStep && wizard::DecodeTierConfirm(sender, tierPick);
    if (isTierStep || isTierConfirm)
    {
        Choice choice;
        uint32 specCount = 0;
        if (!wizard::DecodeChoice(action, choice) || !ValidSelection(player, recruiter, choice, specCount))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        if (!HireSpecOffered(choice.classId, int(choice.specIndex), player->GetLevel(),
                sPlayerbotAIConfig.shamanTankEnabled))
        {
            CloseWithHint(player, "That specialisation is not available any more.");
            return true;
        }
        uint8 const highest = HighestUnlockedTier(player);
        bool const stepVisible =
            wizard::TierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled, player->GetLevel(), highest);
        if (tierPick > highest || (!stepVisible && tierPick > 0))
        {
            ShowConfirmMenu(player, recruiter, choice, 0);
            return true;
        }
        if (isTierConfirm)
        {
            HireNow(player, recruiter, choice, tierPick);
            return true;
        }
        ShowConfirmMenu(player, recruiter, choice, tierPick);
        return true;
    }

    // Back from the equipment page: to the spec page, or to the equipment page from its confirm.
    if (sender == kSenderTierBackToSpec || sender == wizard::kSenderTierBack)
    {
        Choice choice;
        uint32 specCount = 0;
        if (!wizard::DecodeChoice(action, choice) || !ValidSelection(player, recruiter, choice, specCount))
        {
            ShowClassMenu(player, recruiter);
            return true;
        }
        if (sender == kSenderTierBackToSpec)
        {
            ShowSpecMenu(player, recruiter, choice.classId, choice.race, choice.gender);
            return true;
        }
        uint8 const highest = HighestUnlockedTier(player);
        if (wizard::TierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled, player->GetLevel(), highest))
            ShowTierMenu(player, recruiter, choice, highest);
        else
            ShowSpecMenu(player, recruiter, choice.classId, choice.race, choice.gender);
        return true;
    }

    ShowClassMenu(player, recruiter);
    return true;
}

// The front page: the wizard at every inn, plus the world-buff and contracts pages on top where they apply.
// OnGossipHello builds it, and the other pages' back links call OnGossipHello again.
void ShowRecruiterRoot(Player* player, Creature* recruiter)
{
    if (!player || !recruiter)
        return;
    ClearGossipMenuFor(player);
    bool const buffs = WorldBuffsVisible(player, recruiter);
    bool const contracts = ContractsVisible(player, recruiter);
    if (!buffs && !contracts)
    {
        ShowClassMenu(player, recruiter);
        return;
    }
    AddGossipItemFor(player, GossipOptionNpc::None, "Hire bots", wizard::kSenderRoot, wizard::kRootHire);
    if (buffs)
        AddGossipItemFor(player, GossipOptionNpc::Vendor, "World buffs", wizard::kSenderRoot, wizard::kRootWorldBuffs);
    if (contracts)
        AddGossipItemFor(player, GossipOptionNpc::Battlemaster, "Mercenary contracts",
            wizard::kSenderRoot, wizard::kRootContracts);
    Send(player, recruiter);
}

class RecruiterAI : public CreatureAI
{
public:
    explicit RecruiterAI(Creature* creature) : CreatureAI(creature) {}

    bool OnGossipHello(Player* player) override
    {
        if (!player)
            return false;
        if (!GatesOpen(player))
            return true;
        ShowRecruiterRoot(player, me);
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        // Read the pair before the menu is cleared: the core passes only the clicked item's order index.
        uint32 const sender = GetGossipSenderFor(player, gossipListId);
        uint32 const action = GetGossipActionFor(player, gossipListId);
        ClearGossipMenuFor(player);
        return HandleSelect(player, me, sender, action);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        // Startup creatures are added before the module's config is read, so a recruiter spawns even when
        // hiring is off. Hide it once, after startup, instead of trusting the flag inside GetAI.
        if (m_checked)
            return;
        m_checked = true;
        if (!Enabled())
            me->AddObjectToRemoveList();
    }

private:
    bool m_checked = false;
};
} // namespace

void RegisterRecruiterScripts()
{
    // The name is what creature_template.ScriptName carries (the SQL's rows); RegisterCreatureAI would name the
    // script after the C++ class instead.
    new GenericCreatureScript<RecruiterAI>("tortoisebots_hire_recruiter");
}
} // namespace TortoiseBots::hire

#else

namespace TortoiseBots::hire
{
void RegisterRecruiterScripts() {}
}

#endif
