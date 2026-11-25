// The host half of the class-quest spells: one index over the quest store, checked against the reviewed allow-list, and the per-bot
// sync that teaches what the bot has earned. World thread builds the index; map threads only copy the pointer and read.
#include "host/TbClassQuestSpells.h"

#include "host/TbClassQuestSpellsPure.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "TbBotMgr.h"

#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace TortoiseBots
{
namespace questspells
{
namespace
{

using RowKey = std::pair<uint8_t, uint32_t>;

// One quest the index found teaching one row, and the gate that quest resolves to (its classic min level, else the row's).
struct Entry
{
    Quest const* quest = nullptr;
    uint8_t minLevel = 0;
};

struct Index
{
    std::map<RowKey, std::vector<Entry>> rows;
    std::set<RowKey> disabled;   // rows the running data does not support
};

std::mutex g_indexMutex;
std::shared_ptr<Index const> g_index;
std::atomic<bool> g_enabled{ true };

// The classes the allow-list has rows for; the mage has none (D11).
constexpr uint8_t kClasses[] = { 1, 2, 3, 4, 5, 7, 9, 11 };

std::vector<uint32_t> LearnTriggers(uint32_t spellId)
{
    std::vector<uint32_t> triggers;
    if (!spellId)
        return triggers;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return triggers;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
            triggers.push_back(effect.TriggerSpell);
    return triggers;
}

// The hint is validation only. Any counted quest carrying it as its reward or source spell passes; otherwise the hint spell
// itself must teach the row (or be the row).
bool HintMatches(AllowedSpell const& row, std::vector<Entry> const& entries)
{
    for (Entry const& entry : entries)
        if (entry.quest->GetRewSpell() == row.hint || entry.quest->GetSrcSpell() == row.hint)
            return true;
    if (row.hint == row.spellId)
        return true;
    SpellInfo const* hint = sSpellMgr->GetSpellInfo(row.hint, DIFFICULTY_NONE);
    if (!hint)
        return true;   // nothing to check the hint against; the quest that teaches the spell is the authority
    for (SpellEffectInfo const& effect : hint->GetEffects())
        if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell == row.spellId)
            return true;
    return false;
}

QuestRaw RawOf(uint32_t questId, Quest const* quest)
{
    QuestRaw raw;
    raw.questId = questId;
    raw.classMask = quest->GetAllowableClasses();
    raw.repeatable = quest->IsRepeatable();
    raw.classicMinLevel = quest->GetClassicMinLevel();
    raw.rewSpell = int32_t(quest->GetRewSpell());
    raw.srcSpell = quest->GetSrcSpell();
    raw.rewLearns = LearnTriggers(quest->GetRewSpell());
    raw.srcLearns = LearnTriggers(quest->GetSrcSpell());
    for (QuestRewardDisplaySpell const& display : quest->RewardDisplaySpell)
        if (display.SpellId)
            raw.displays.push_back(display.SpellId);
    return raw;
}

uint8_t ResolveMinLevel(AllowedSpell const& row, int32_t classicMinLevel)
{
    if (classicMinLevel <= 0)
        return row.level;
    return uint8_t(std::min<int32_t>(classicMinLevel, 60));
}

std::shared_ptr<Index const> CurrentIndex()
{
    std::lock_guard<std::mutex> lock(g_indexMutex);
    return g_index;
}

} // namespace

void LoadConfig()
{
    g_enabled = sConfigMgr->GetBoolDefault("TortoiseBots.ClassQuestSpells", false);
}

bool Enabled()
{
    return g_enabled;
}

void BuildIndex()
{
    if (!Enabled())
    {
        std::lock_guard<std::mutex> lock(g_indexMutex);
        g_index.reset();
        return;
    }

    auto index = std::make_shared<Index>();
    size_t impliedEntries = 0;
    for (auto const& [questId, questPtr] : sObjectMgr->GetQuestTemplates())
    {
        Quest const* quest = questPtr.get();
        if (!quest || quest->IsRepeatable())
            continue;
        // A quest with no class mask still counts when the spell it teaches is on the class's list: the spell implies the class
        // (a world's class-spell quests may carry no AllowableClasses).
        QuestRaw const raw = RawOf(questId, quest);
        for (uint8_t classId : kClasses)
        {
            for (uint32_t spellId : CollectTaught(raw, classId))
            {
                AllowedSpell const* row = FindRow(classId, spellId);
                if (!row)
                    continue;
                index->rows[RowKey(classId, spellId)].push_back(Entry{ quest, ResolveMinLevel(*row, raw.classicMinLevel) });
                if (!raw.classMask)
                    ++impliedEntries;
            }
        }
    }

    size_t active = 0;
    std::set<uint32_t> quests;
    for (AllowedSpell const& row : kAllowed)
    {
        RowKey const key(row.classId, row.spellId);
        auto const found = index->rows.find(key);
        char const* reason = nullptr;
        if (!sSpellMgr->GetSpellInfo(row.spellId, DIFFICULTY_NONE))
            reason = "unknown spell";
        else if (found == index->rows.end() || found->second.empty())
            reason = "no quest";
        else if (!HintMatches(row, found->second))
            reason = "hint mismatch";

        if (reason)
        {
            index->disabled.insert(key);
            TC_LOG_WARN("tortoisebots", "class quest spells: {} {} {}: {}", uint32_t(row.classId), row.spellId, row.name, reason);
            continue;
        }
        ++active;
        for (Entry const& entry : found->second)
            quests.insert(entry.quest->GetQuestId());
    }
    TC_LOG_INFO("tortoisebots", "class quest spells: {} of {} rows active, {} quests ({} entries from quests with no class mask)",
        active, kAllowedCount, quests.size(), impliedEntries);

    std::lock_guard<std::mutex> lock(g_indexMutex);
    g_index = index;
}

uint32_t Sync(Player* bot, char const* why)
{
    if (!Enabled() || !bot || !BotMgr::Instance().IsBot(bot->GetGUID()))
        return 0;
    std::shared_ptr<Index const> const index = CurrentIndex();
    if (!index)
        return 0;

    uint8_t const classId = bot->GetClass();
    uint32_t taught = 0;
    for (AllowedSpell const* row : RowsForClass(classId))
    {
        RowKey const key(classId, row->spellId);
        if (index->disabled.count(key))
            continue;
        auto const found = index->rows.find(key);
        if (found == index->rows.end())
            continue;

        std::vector<Candidate> cands;
        cands.reserve(found->second.size());
        for (Entry const& entry : found->second)
            cands.push_back(Candidate{ entry.quest->GetQuestId(), bot->SatisfyQuestRace(entry.quest, false), entry.minLevel });

        // The known test is inside Decide: a second login or level event teaches and logs nothing.
        Decision const decision = Decide(*row, bot->GetLevel(), cands, bot->HasSpell(row->spellId), bot->IsSpellFitByClassAndRace(row->spellId));
        if (decision.verdict != Verdict::Teach)
            continue;
        // Exactly what Spell::EffectLearnSpell ends in; no cast, so combat, death or a mount changes nothing.
        SpellInfo const* info = sSpellMgr->GetSpellInfo(row->spellId, DIFFICULTY_NONE);
        if (!info || !SpellMgr::IsSpellValid(info, bot, false))
            continue;
        if (bot->HasSpell(row->spellId))
            continue;
        bot->LearnSpell(row->spellId, false);
        TC_LOG_INFO("tortoisebots", "class quest spells: {} learned {} {} (quest {}, level {}) on {}",
            bot->GetName(), row->spellId, row->name, decision.questId, uint32_t(decision.gate), why ? why : "sync");
        ++taught;
    }
    return taught;
}

} // namespace questspells
} // namespace TortoiseBots
