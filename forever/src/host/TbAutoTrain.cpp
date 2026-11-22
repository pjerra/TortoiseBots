// Automatic class-spell training: caches per-class trainer ids and class skill-line spells once,
// then teaches a bot its class spells free on login, level-up, or `.tb train <bot>`.
#include "host/TbAutoTrain.h"

#include "host/TbAutoTrainPure.h"
#include "TbBotMgr.h"
#include "compat/TbSpellEntry.h"

#include "Config.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

namespace tbhost
{
namespace autotrain
{
namespace
{

struct Index
{
    std::array<std::vector<uint32_t>, MAX_CLASSES> trainersByClass;
    std::array<std::vector<uint32_t>, MAX_CLASSES> lineSpellsByClass;
};

std::mutex g_indexMutex;
std::shared_ptr<Index const> g_index;
std::atomic<bool> g_enabled{ true };

std::shared_ptr<Index const> CurrentIndex()
{
    std::lock_guard<std::mutex> lock(g_indexMutex);
    return g_index;
}

std::shared_ptr<Index const> EnsureIndex()
{
    std::shared_ptr<Index const> index = CurrentIndex();
    if (!index)
    {
        BuildIndex();
        index = CurrentIndex();
    }
    return index;
}

} // namespace

void LoadConfig()
{
    g_enabled.store(sConfigMgr->GetBoolDefault("TortoiseBots.AutoLearnOnLevel", true), std::memory_order_release);
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_acquire);
}

void BuildIndex()
{
    auto index = std::make_shared<Index>();
    for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
    {
        if (trainer.GetType() != Trainer::Type::None)
            continue;
        uint32_t const mask = trainer.GetClassMask();
        // Only trainers dedicated to a single class (multi-class masks hold riding/recipe trainers).
        if (mask == 0 || (mask & (mask - 1)) != 0)
            continue;
        for (uint8_t cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
        {
            if (mask == (1u << (cls - 1)))
            {
                index->trainersByClass[cls].push_back(id);
                break;
            }
        }
    }
    for (uint8_t cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        std::set<uint32> const lineSpells = tbcompat::ClassLineSpells(cls);
        index->lineSpellsByClass[cls].assign(lineSpells.begin(), lineSpells.end());
    }
    std::lock_guard<std::mutex> lock(g_indexMutex);
    g_index = std::move(index);
}

uint32_t TeachClassSpells(Player* bot, Tally& out)
{
    out = Tally{};
    if (!bot)
        return 0;
    uint8_t const classId = bot->GetClass();
    if (classId == 0 || classId >= MAX_CLASSES)
        return 0;

    std::shared_ptr<Index const> const index = EnsureIndex();
    std::vector<uint32_t> const& classTrainers = index->trainersByClass[classId];
    std::vector<uint32_t> const& lineSpells = index->lineSpellsByClass[classId];
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(classId);

    std::unordered_set<uint32_t> knownBefore;
    knownBefore.reserve(bot->GetSpellMap().size());
    for (auto const& [spellId, spell] : bot->GetSpellMap())
        knownBefore.insert(spellId);

    uint32_t pass = 0;
    for (; pass < 8; ++pass)
    {
        uint32_t passTaught = 0;
        out.trainers = 0;
        for (uint32_t trainerId : classTrainers)
        {
            Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(trainerId);
            if (!trainer)
                continue;
            ++out.trainers;
            passTaught += trainer->TeachAvailableSpellsFree(bot, &out.trainerTally);
        }
        out.trainerTaught += passTaught;

        uint32_t lineThisPass = 0;
        if (classEntry)
        {
            for (uint32_t spellId : lineSpells)
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
                if (!info)
                    continue;
                bool const alreadyKnown = bot->HasSpell(spellId);
                uint32_t const rank = tbcompat::SpellRankOf(spellId);
                if (!alreadyKnown && info->SpellLevel != 0 && info->SpellLevel <= bot->GetLevel()
                    && info->SpellFamilyName == classEntry->SpellClassSet
                    && ShouldSkipRetailChainless(spellId, rank))
                {
                    ++out.retailSkipped;
                    continue;
                }
                if (!ShouldTeachClassLineSpell(uint8_t(info->SpellLevel), bot->GetLevel(),
                        info->SpellFamilyName, classEntry->SpellClassSet, alreadyKnown, spellId, rank))
                    continue;
                if (!bot->IsSpellFitByClassAndRace(info->Id) || !SpellMgr::IsSpellValid(info, bot, false))
                    continue;
                bot->LearnSpell(info->Id, false);
                ++lineThisPass;
            }
        }
        out.lineTaught += lineThisPass;
        if (!passTaught && !lineThisPass)
        {
            ++pass;
            break;
        }
    }
    out.passes = pass;

    // A retail-era spell of a trainer row that is in no verified chain is not taught: take newly added ones back.
    std::vector<uint32_t> taken;
    for (auto const& [spellId, spell] : bot->GetSpellMap())
    {
        if (ShouldRemovePostTrain(spellId, tbcompat::SpellRankOf(spellId), knownBefore.count(spellId) != 0))
            taken.push_back(spellId);
    }
    for (uint32_t spellId : taken)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        out.removed += std::to_string(spellId) + ":" + (info && info->SpellName ? info->SpellName->Str[LOCALE_enUS] : "?") + " ";
        bot->RemoveSpell(spellId, false, false);
        ++out.removedCount;
    }
    out.trainerTaught = NetTrainerTaught(out.trainerTaught, out.removedCount);
    return out.TotalTaught();
}

uint32_t Sync(Player* bot, char const* why)
{
    bool const isBot = bot && TortoiseBots::BotMgr::Instance().IsBot(bot->GetGUID());
    if (!ShouldAutoTrainOnEvent(Enabled(), isBot))
        return 0;

    Tally tally;
    uint32_t const taught = TeachClassSpells(bot, tally);
    if (taught > 0)
    {
        TC_LOG_INFO("tortoisebots", "autotrain: {} learned {} spell(s) ({} trainer, {} line) at level {} on {}",
            bot->GetName(), taught, tally.trainerTaught, tally.lineTaught, uint32_t(bot->GetLevel()), why ? why : "sync");
    }
    return taught;
}

} // namespace autotrain
} // namespace tbhost
