#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/policy/loot/LootViewRulePolicy.h"

namespace ai
{
    // One rule for "this corpse still has body loot for this bot": the raw lootable flag
    // or the viewer's own loot right. The core masks the flag per viewer, so a corpse
    // holding only this bot's quest item can lack it while IsAllowedToLoot still says yes.
    // (Both cores: CorpseHasBodyLootForCompat. T: the expression below, moved word for word
    // into cmangos-compat-s2i.h; F: Player::isAllowedToLoot, forever/src/compat/TbCompat_I2.h.)
    inline bool CorpseHasBodyLootFor(Player* bot, Creature* creature)
    {
        return CorpseHasBodyLootForCompat(bot, creature);
    }

    // Loot::hasItemFor, restricted to the copies the core will actually show this viewer.
    // The algorithm below reads the 1.12 personal item maps (T); `auto` makes it a generic
    // lambda, so it is only looked at where it runs (T). F answers with Loot::hasItemFor.
    inline bool HasVisiblePersonalItemFor(Loot const& loot, Player* viewer)
    {
        return PersonalItemVisibleCompat(loot, viewer, [](auto const& loot, auto* viewer) -> bool {
        uint32 const me = viewer->GetGUIDLow();
        bool const turn = loot.roundRobinPlayer != 0;
        bool const mine = loot.roundRobinPlayer == viewer->GetGUID();
        uint32 const holder = ObjectGuid(loot.roundRobinPlayer).GetCounter();
        auto const& q = loot.GetPlayerQuestItems();
        auto const& f = loot.GetPlayerFFAItems();
        auto const& c = loot.GetPlayerNonQuestNonFFAConditionalItems();
        auto has = [](auto const& m, uint32 k) { auto i = m.find(k); return i != m.end() && i->second; };
        if (has(q, me))
            for (auto const& qi : *q.find(me)->second)
            {
                LootItem const& it = loot.m_questItems[qi.index];
                if (!qi.is_looted && !it.is_looted && PersonalCopyVisible(it.freeforall, turn, mine, has(q, holder)))
                    return true;
            }
        if (has(f, me))
            for (auto const& fi : *f.find(me)->second)
                if (!fi.is_looted && !loot.items[fi.index].is_looted)
                    return true;                               // FFA: always shown (LootMgr.cpp:1029-1043)
        if (has(c, me))
            for (auto const& ci : *c.find(me)->second)
            {
                LootItem const& it = loot.items[ci.index];
                if (!ci.is_looted && !it.is_looted && PersonalCopyVisible(it.freeforall, turn, mine, has(c, holder)))
                    return true;
            }
        return false;
        });
    }
}
