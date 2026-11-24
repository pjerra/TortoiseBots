
#include "playerbot/playerbot.h"
#include "SpellIdValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/SpellPickPolicy.h"

using namespace ai;

SpellIdValue::SpellIdValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "spell id", 10), Qualified()
{
}

uint32 SpellIdValue::Calculate()
{
    std::string namepart = qualifier;
    ItemIds itemIds = ChatHelper::parseItems(namepart);

    PlayerbotChatHandler handler(bot);
    uint32 extractedSpellId = handler.extractSpellId(namepart);
    if (extractedSpellId)
    {
        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(extractedSpellId);
        if (pSpellInfo) namepart = pSpellInfo->SpellName[0];
    }

    std::vector<uint32> ids = chat->SpellIds(namepart);

    char firstSymbol = 'x';
    int spellLength = 0;
    std::wstring wnamepart;

    if (ids.empty())
    {
        //sLog.outError("Please add spell %s to spell list", namepart.c_str());
        if (!Utf8toWStr(namepart, wnamepart))
            return 0;

        wstrToLower(wnamepart);
        firstSymbol = tolower(namepart[0]);
        spellLength = wnamepart.length();
    }

    int loc = bot->GetSession()->GetSessionDbcLocale();

    std::set<uint32> spellIds;
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

        if (!ids.empty())
        {
            if (std::find(ids.begin(), ids.end(), spellId) == ids.end())
                continue;
        }

        if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        if (pSpellInfo->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
            continue;

        bool useByItem = false;
        for (int i = 0; i < 3; ++i)
        {
            if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && itemIds.find(pSpellInfo->EffectItemType[i]) != itemIds.end())
            {
                useByItem = true;
                break;
            }
        }

        if (ids.empty())
        {
            // Penqle's SpellName is std::string[8].
            std::string const& spellName = pSpellInfo->SpellName[loc];
            if (!useByItem && (tolower(spellName[0]) != firstSymbol || spellName.size() != spellLength || !Utf8FitTo(spellName.c_str(), wnamepart)))
                continue;
        }

        spellIds.insert(spellId);
    }

    Pet* pet = bot->GetPet();
    if (spellIds.empty() && pet)
    {
        for (uint32 const spellId : PetSpellIdsCompat(pet))
        {
            if (!ids.empty())
            {
                if (std::find(ids.begin(), ids.end(), spellId) == ids.end())
                    continue;
            }

            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                continue;

            if (pSpellInfo->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
                continue;

            if (ids.empty())
            {
                // Penqle's SpellName is std::string[8].
                std::string const& spellName = pSpellInfo->SpellName[loc];
                if (tolower(spellName[0]) != firstSymbol || spellName.size() != spellLength || !Utf8FitTo(spellName.c_str(), wnamepart))
                    continue;
            }

            spellIds.insert(spellId);
        }
    }

    if (spellIds.empty()) return 0;

    int saveMana = (int) round(AI_VALUE(double, "mana save level"));
    int rank = 1;
    int highestSpellId = 0;
    int lowestSpellId = 0;

    if (saveMana <= 1)
    {
        // One candidate per same-named spell the bot knows; PickCastSpellId
        // keeps a same-named rank-less proc from shadowing the ranked line.
        std::vector<KnownSpellRank> candidates;
        candidates.reserve(spellIds.size());
        for (std::set<uint32>::reverse_iterator itr = spellIds.rbegin(); itr != spellIds.rend(); ++itr)
        {
            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(*itr);
            if (!pSpellInfo)
                continue;

            candidates.push_back({ *itr, SpellRankNumber(pSpellInfo->Rank[0]) });
        }

        return PickCastSpellId(candidates);
    }

    for (std::set<uint32>::reverse_iterator i = spellIds.rbegin(); i != spellIds.rend(); ++i)
    {
        if (!highestSpellId) highestSpellId = *i;
        if (IsHighRankOfSpellCompat(*i, highestSpellId)) highestSpellId = *i;
        if (saveMana == rank) return *i;
        lowestSpellId = *i;
        rank++;
    }

    return lowestSpellId;
}
