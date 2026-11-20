// Slice 11, package P11: the pet host (forever/docs/SLICE11-NOTES.md). Compiled only with TORTOISEBOTS_SLICE1 (forever/src/host/TbHost*.cpp).
//
// The legacy PlayerbotFactory made a hunter's first pet and trained its spells through the 1.12 pet classes; on ForeverCore those five members were link stand-ins
// that did nothing (SliceLinkStubs.cpp, slice 1). They are real here, written against the core's own pet API: a stable of active slots (PetDefines.h:40-48), the classic
// hunter loyalty and training-point rules (Pet.h:108-124), `Unit::CreateTamedPetFrom` (Unit.cpp:11656) and the four steps of `Spell::EffectTameCreature`
// (SpellEffects.cpp:2757-2774). Core read only at the pin; no core change. The ForeverCore team's own bot setup does the same for its hunters (~/ref-wfb,
// BotSetupPipeline.cpp:735-830) and is the reference for the order of the steps and for why the pet number is written at once: read, never copied.
//
// Rules this file keeps (forever/docs/SLICE1-FINDINGS.md): a call that cannot be made does LESS (no pet, nothing learned), never something else, and the answer a
// caller gets is read back from the world after the change.
#include "compat/botpch_forever.h"

#include "host/TbPetFactory.h"

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/TbCompatP11.h"
#include "compat/TbCompat_V.h"
#include "runtime/HunterPetPolicy.h"
#include "runtime/policy/pet/PetSpellRankPolicy.h"

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

namespace TortoiseBots
{
namespace p11
{
namespace
{
// One starter beast per race, picked from the world database of the forever test server by a SELECT (forever/data/slice11/hunter-starter-pets.csv, the file's
// header says how): a tameable, non-exotic beast of a common family whose wild level is 5-12. The entry is checked again at run time (TameableBeast): an id the
// data does not hold as a tameable beast is never used.
struct Starter
{
    uint8 race;
    uint32 entry;
};
constexpr Starter kStarters[] = {
    { RACE_HUMAN,    1128 },    // Young Black Bear
    { RACE_ORC,      3121 },    // Durotar Tiger
    { RACE_DWARF,    1196 },    // Ice Claw Bear
    { RACE_NIGHTELF, 2042 },    // Nightsaber
    { RACE_UNDEAD_PLAYER, 1765 }, // Worg
    { RACE_TAUREN,   2958 },    // Prairie Wolf
    { RACE_GNOME,    1196 },    // Ice Claw Bear (the Dun Morogh beast)
    { RACE_TROLL,    3121 },    // Durotar Tiger
};
constexpr uint32 kAllianceFallback = 1128;   // Young Black Bear
constexpr uint32 kHordeFallback = 2958;      // Prairie Wolf

bool TameableBeast(uint32 entry)
{
    CreatureTemplate const* info = entry ? ObjectMgr::instance()->GetCreatureTemplate(entry) : nullptr;
    CreatureDifficulty const* difficulty = info ? info->GetDifficulty(DIFFICULTY_NONE) : nullptr;
    return info && difficulty && info->IsTameable(false, difficulty);
}
}

char const* GiveResultCode(GiveResult result)
{
    switch (result)
    {
        case GiveResult::Ok:           return "ok";
        case GiveResult::NotHunter:    return "wrong-class";
        case GiveResult::LevelTooLow:  return "level-too-low";
        case GiveResult::NotInWorld:   return "not-in-world";
        case GiveResult::HasPet:       return "has-pet";
        case GiveResult::HasStoredPet: return "stored-pet";
        case GiveResult::NoEntry:      return "no-starter";
        case GiveResult::CreateFailed: return "create-failed";
        case GiveResult::NoMap:        return "no-map";
        case GiveResult::NotSummoned:  return "not-summoned";
    }
    return "failed";
}

char const* GiveResultText(GiveResult result)
{
    switch (result)
    {
        case GiveResult::Ok:           return "the pet is out";
        case GiveResult::NotHunter:    return "only a hunter is given a pet (a warlock summons its demon)";
        case GiveResult::LevelTooLow:  return "a hunter has no pet before level 10";
        case GiveResult::NotInWorld:   return "the bot is not in the world";
        case GiveResult::HasPet:       return "the bot already has a pet out";
        case GiveResult::HasStoredPet: return "the bot has a pet in an active slot: Call Pet brings it back";
        case GiveResult::NoEntry:      return "no starter beast of the table is a tameable beast in this data";
        case GiveResult::CreateFailed: return "the core refused to make the pet (no free active slot, or no stats for the level)";
        case GiveResult::NoMap:        return "the pet has no map";
        case GiveResult::NotSummoned:  return "the pet was made but is not out";
    }
    return "failed";
}

uint32 StarterPetEntry(Player const* bot)
{
    for (Starter const& starter : kStarters)
        if (starter.race == bot->GetRace() && TameableBeast(starter.entry))
            return starter.entry;
    uint32 const fallback = bot->GetTeam() == ALLIANCE ? kAllianceFallback : kHordeFallback;
    return TameableBeast(fallback) ? fallback : 0;
}

GiveResult GiveHunterPet(Player* bot, bool saveToDb)
{
    if (!bot || bot->GetClass() != CLASS_HUNTER)
        return GiveResult::NotHunter;
    if (bot->GetLevel() < TortoiseBots::HUNTER_PET_MIN_LEVEL)
        return GiveResult::LevelTooLow;
    if (!bot->IsInWorld() || !bot->GetMap())
        return GiveResult::NotInWorld;
    if (bot->GetPet() || !bot->GetPetGUID().IsEmpty())
        return GiveResult::HasPet;
    if (HasStoredPetCompat(bot))
        return GiveResult::HasStoredPet;
    uint32 const entry = StarterPetEntry(bot);
    if (!entry)
        return GiveResult::NoEntry;

    // The core's tame path: a Pet of the creature entry (a tamed beast, not a spawned creature), stats for the owner's level, a free active slot of the stable
    // (InitTamedPet fills it and makes it the current pet), the beast's own abilities (Pet::LearnTameSpells).
    Pet* pet = bot->CreateTamedPetFrom(entry, 0);
    if (!pet)
        return GiveResult::CreateFailed;
    Map* map = pet->GetMap();
    if (!map)
    {
        delete pet;
        // Put the stable back as it was: InitTamedPet had taken a slot for it.
        PetStable& stable = bot->GetOrInitPetStable();
        if (Optional<uint32> index = stable.GetCurrentActivePetIndex())
            stable.ActivePets[*index].reset();
        stable.CurrentPetIndex.reset();
        return GiveResult::NoMap;
    }

    // Classic rules the bot's pet follows (the legacy InitPet did the same with the 1.12 setters): Best Friend (the training-point budget of a hired
    // hunter's pet), defensive. Set before the save, so the stored row carries them.
    pet->InitLoyalty(Pet::BEST_FRIEND, Pet::GetLoyaltyStartPoints(Pet::BEST_FRIEND));
    pet->SetReactState(REACT_DEFENSIVE);

    // The steps of Spell::EffectTameCreature after the pet exists.
    map->AddToMap(pet->ToCreature());
    bot->SetMinion(pet, true);
    pet->UpdateTrainingPoints();

    // The default autocast of every active spell the beast came with (Growl and the like follow the taunt rule of the engine's sweep afterwards).
    for (uint32 const spellId : PetSpellIdsCompat(pet))
        if (!IsPassiveSpell(spellId))
            PetToggleAutocastCompat(pet, spellId, TortoiseBots::ShouldPetSpellAutocastDefault(spellId));

    if (saveToDb)
    {
        pet->SavePetToDB(PET_SAVE_AS_CURRENT);
        // The next map change puts the pet away and a later save would write 0 here: the row names the pet already, so a re-login calls it back.
        if (CharmInfo const* charm = pet->GetCharmInfo())
            CharacterDatabase.DirectPExecute("UPDATE characters SET summonedPetNumber = {} WHERE guid = {}", charm->GetPetNumber(), bot->GetGUID().GetCounter());
    }
    bot->PetSpellInitialize();

    // Read back.
    Pet* const now = bot->GetPet();
    return now && now->IsInWorld() ? GiveResult::Ok : GiveResult::NotSummoned;
}
}
}

// =====================================================================================================================
// The five PlayerbotFactory members the slice used to stand in for (SliceLinkStubs.cpp, slice 1). The declarations are the legacy ones
// (ai/playerbot/PlayerbotFactory.h:155,178-187); the Turtle bodies are in ai/playerbot/PlayerbotFactory.cpp.
// =====================================================================================================================

// No ammo is handed out (lead decision D3: the kit carries the stock, the shots take it). What this does is let the core pick the ammo the bot carries: a bot has
// no client to choose it, so it prefers a stack drawn with a model (Player.h:1527) and takes one (Player::AutoSelectAmmo, Player.cpp:10586).
void PlayerbotFactory::InitAmmo()
{
    if (bot->GetClass() != CLASS_HUNTER)
        return;
    bot->SetPreferDrawableAmmo(true);
    bot->AutoSelectAmmo();
}

void PlayerbotFactory::InitPet(bool saveToDb)
{
    using TortoiseBots::p11::GiveResult;
    GiveResult const result = TortoiseBots::p11::GiveHunterPet(bot, saveToDb);
    // Not a hunter, too low, a pet already out or stored are ordinary answers of the periodic "initialize pet" check; anything else is worth a line, once per bot
    // and answer (the check asks again every few seconds while there is no pet).
    if (result != GiveResult::Ok && result != GiveResult::NotHunter && result != GiveResult::LevelTooLow && result != GiveResult::HasPet &&
        result != GiveResult::HasStoredPet)
    {
        static std::map<ObjectGuid, GiveResult> logged;
        auto const itr = logged.find(bot->GetGUID());
        if (itr == logged.end() || itr->second != result)
        {
            logged[bot->GetGUID()] = result;
            TC_LOG_WARN("tortoisebots", "pet: no pet for {}: {}", bot->GetName(), TortoiseBots::p11::GiveResultText(result));
        }
    }
}

// "Is this bot's pet trained by the module" (AiPlayerbot.PetAutoTrain, the key the legacy code names "pool bots and hires"; PetAutoTrainAlts is the player's
// own alts). A ForeverCore host bot is a hired bot: it is no pool bot (RandomBotFacade::IsRandomBot is false for it) and no real player's alt, so the hire key decides.
bool PlayerbotFactory::PetAutoTrainAllowed(Player* bot)
{
    return bot && sPlayerbotAIConfig.petAutoTrain;
}

namespace
{
// The training-point cost of one pet spell rank. An id the data does not hold costs more than any budget, so the planner never picks it; a free spell costs 0.
// T reads SkillLineAbility::reqtrainpoints, F keeps the table in classic_pet_spell_training_points (Pet::GetSpellTrainingPoints, Pet.h:122).
int32 HunterPetSpellCost(uint32 spellId)
{
    if (!GetSpellEntryCompat(spellId))
        return 1 << 20;
    return int32(Pet::GetSpellTrainingPoints(spellId));
}
}

bool PlayerbotFactory::TrainHunterPet(Player* bot, Pet* pet, bool apply, std::function<bool(uint32)> const& autocastDefault)
{
    if (!bot || !pet || pet->getPetType() != HUNTER_PET)
        return false;

    // Pool pets (hires included on Turtle) are Best Friends and follow the plan exactly; a pet the facade does not own spends its real loyalty's points and keeps
    // what was taught. A ForeverCore host bot is not in the facade, so its pet takes the second way: the planner works inside the points the core counts.
    bool const rebuild = sRandomBotFacade.IsRandomBot(bot);
    uint32 const level = pet->GetLevel();
    int32 const budget = TortoiseBots::HunterPetTrainingBudget(level,
        rebuild ? uint32(Pet::BEST_FRIEND) : uint32(pet->GetLoyaltyLevel()));
    CreatureTemplate const* info = ObjectMgr::instance()->GetCreatureTemplate(pet->GetEntry());
    uint32 const family = info ? uint32(info->family) : 0;

    std::map<TortoiseBots::PetRankLadder const*, int> knownRank;
    std::vector<uint32> knownManaged;
    for (uint32 const spellId : PetSpellIdsCompat(pet))
    {
        int rank = -1;
        TortoiseBots::HunterPetBuildEntry const* entry = TortoiseBots::FindHunterPetLadder(spellId, rank);
        if (!entry)
            continue;
        knownManaged.push_back(spellId);
        auto known = knownRank.find(entry->ladder);
        if (known == knownRank.end() || known->second < rank)
            knownRank[entry->ladder] = rank;
    }

    // An alt keeps the actives its player taught outside the family build.
    std::vector<TortoiseBots::PetRankLadder const*> kept;
    if (!rebuild)
    {
        std::vector<TortoiseBots::HunterPetBuildEntry> const build = TortoiseBots::HunterPetBuild(family);
        for (TortoiseBots::HunterPetBuildEntry const& entry : TortoiseBots::HunterPetManagedLadders())
        {
            if (entry.passive || entry.ladder == &TortoiseBots::GrowlLadder() || !knownRank.count(entry.ladder))
                continue;
            bool inBuild = false;
            for (TortoiseBots::HunterPetBuildEntry const& b : build)
                inBuild = inBuild || b.ladder == entry.ladder;
            if (!inBuild)
                kept.push_back(entry.ladder);
        }
    }

    std::vector<TortoiseBots::PetWantedSpell> plan;
    TortoiseBots::PlanHunterPetSpells(family, level, budget, kept,
        [](uint32_t spellId) { return HunterPetSpellCost(spellId); },
        [&](TortoiseBots::PetRankLadder const& ladder)
        {
            if (rebuild)
                return -1;
            auto known = knownRank.find(&ladder);
            return known == knownRank.end() ? -1 : known->second;
        },
        [&plan](TortoiseBots::PetWantedSpell wanted) { plan.push_back(wanted); });

    auto planned = [&plan](uint32 spellId)
    {
        for (TortoiseBots::PetWantedSpell const& wanted : plan)
            if (wanted.spellId == spellId)
                return true;
        return false;
    };
    std::vector<uint32> toUnlearn;
    if (rebuild)
        for (uint32 spellId : knownManaged)
            if (!planned(spellId))
                toUnlearn.push_back(spellId);
    std::vector<TortoiseBots::PetWantedSpell> toLearn;
    for (TortoiseBots::PetWantedSpell const& wanted : plan)
        if (!pet->HasSpell(wanted.spellId))
            toLearn.push_back(wanted);

    if (!apply || (toUnlearn.empty() && toLearn.empty()))
        return !toUnlearn.empty() || !toLearn.empty();

    for (uint32 spellId : toUnlearn)
        pet->unlearnSpell(spellId, false);
    for (TortoiseBots::PetWantedSpell const& wanted : toLearn)
    {
        int rank = -1;
        TortoiseBots::HunterPetBuildEntry const* entry = TortoiseBots::FindHunterPetLadder(wanted.spellId, rank);
        bool const newAbility = !entry || !knownRank.count(entry->ladder) || rebuild;
        PetLearnSpellCompat(pet, wanted.spellId);
        // A new rank keeps the autocast state of the one it replaces (an alt's player may have set it); only a new ability gets the default.
        if (newAbility && !wanted.passive && pet->HasSpell(wanted.spellId) && !IsPassiveSpell(wanted.spellId))
            PetToggleAutocastCompat(pet, wanted.spellId, autocastDefault(wanted.spellId));
    }

    // The core counts the points itself (Pet::UpdateTrainingPoints, Pet.cpp:1116: one cost per spell chain): refresh the pet's fields once after the unlearns.
    pet->UpdateTrainingPoints();
    return true;
}

void PlayerbotFactory::InitPetSpells()
{
    if (!bot->GetMap())
        return;
    Pet* pet = bot->GetPet();
    if (!pet)
        return;
    if (!PetAutoTrainAllowed(bot))
        return;

    if (bot->GetClass() == CLASS_HUNTER)
        TrainHunterPet(bot, pet, true, [](uint32 spellId) { return TortoiseBots::ShouldPetSpellAutocastDefault(spellId); });

    if (bot->GetClass() == CLASS_WARLOCK)
    {
        // Same shared table as the check (ForEachWarlockWantedSpell): top rank per demon line + single-rank rows; ids without a spell entry are skipped so an
        // unteachable row cannot pin the "initialize pet" check true.
        TortoiseBots::ForEachWarlockWantedSpell(pet->GetEntry(), pet->GetLevel(),
            [pet](TortoiseBots::PetWantedSpell wanted)
        {
            if (!GetSpellEntryCompat(wanted.spellId))
                return;
            if (!pet->HasSpell(wanted.spellId))
                PetLearnSpellCompat(pet, wanted.spellId);
            if (pet->HasSpell(wanted.spellId) && !IsPassiveSpell(wanted.spellId))
                PetToggleAutocastCompat(pet, wanted.spellId, TortoiseBots::ShouldPetSpellAutocastDefault(wanted.spellId));
        });
    }
}
