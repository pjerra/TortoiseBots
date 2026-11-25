// World buffs at the six capital recruiters, the core-free half: the Forever ids (recruiters, quests, credits, bosses),
// the faction catalogue (Alliance buys Might of Stormwind where Horde buys Warchief's Blessing), the aura/kill credit
// map and the raid rule. The gossip and purchase code in TbHireWorldBuffs.cpp reads this header so the menu, the hooks
// and the unit test cannot disagree about ids or spells.
//
// The price and spell catalogues themselves are TB's pure ones (runtime/WorldBuffService.h); this header only replaces
// what Forever changes: ids, the faction entry, the Dire Maul pack and the raid list.
#pragma once

#include "runtime/WorldBuffService.h"
#include "runtime/policy/pvp/WorldBuffPolicy.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots::hire::worldbuffs
{
// Gossip senders (TbHire.h kSenderWorldBuffFirst..Last). Mirrors TB: root 505, buy 506, Sayge 507, back 508,
// confirm 509.
constexpr uint32_t kSenderRoot = 505;
constexpr uint32_t kSenderBuy = 506;
constexpr uint32_t kSenderSayge = 507;
constexpr uint32_t kSenderBack = 508;
constexpr uint32_t kSenderConfirm = 509;

// Sender 505 with action 1 returns to the recruiter's main menu (TB's "Hire bots"); any other action opens the buff
// page. The main menu itself belongs to the recruiter package.
constexpr uint32_t kRootActionMainMenu = 1;

// Unlock quests 100000-100013 (one per purchase per faction); contracts own 100050+.
constexpr uint32_t kQuestFirst = 100000;
constexpr uint32_t kQuestLast = 100013;

// Credit-only creatures (never spawned), from the recruiter NPC rows.
constexpr uint32_t kCreditRally = 1000070;
constexpr uint32_t kCreditSilithyst = 1000071;
constexpr uint32_t kSilithystKills = 5;
constexpr uint32_t kZoneSilithus = 1377;

// Recruiter entries 1000000-1000067; the six capital NPCs are the only ones that sell world buffs.
constexpr uint32_t kRecruiterFirst = 1000000;
constexpr uint32_t kRecruiterLast = 1000067;
constexpr uint32_t kCapitalRecruiters[] = {
    1000017, // Stormwind (Walter)
    1000006, // Ironforge (Dagna Firebrew)
    1000012, // Darnassus (Taeloran)
    1000025, // Orgrimmar (Throk)
    1000018, // Undercity (Agatha)
    1000019, // Thunder Bluff (Tahkan)
};

// Entry 2 is faction-dependent: Horde keeps Warchief's Blessing, Alliance gets Might of Stormwind.
constexpr uint32_t kSpellWarchief = 16609;
constexpr uint32_t kSpellMightOfStormwind = 460939;

// Bosses that feed a kill quest. Rend and Hakkar are direct objectives (the core shares the kill with the group);
// Onyxia and Nefarian share one invisible credit because the quest accepts either.
constexpr uint32_t kBossOnyxia = 10184;
constexpr uint32_t kBossNefarian = 11583;
constexpr uint32_t kBossRend = 10429;
constexpr uint32_t kBossHakkar = 14834;

// Turn-in fees shipped in quest_classic_money (negative RewardMoney): the raid unlocks (Rally, Rend, Zandalar) cost
// more than the rest. The Forever build keeps them in SQL; AiPlayerbot.WorldBuffsUnlockFee*Copper is Turtle-only.
constexpr uint32_t kUnlockFeeRaidCopper = 2000000;
constexpr uint32_t kUnlockFeeCopper = 1000000;

inline uint32_t UnlockQuestFor(uint8_t purchase, bool horde)
{
    if (!IsValidWorldBuffPurchase(purchase))
        return 0;
    return kQuestFirst + (uint32_t(purchase) - 1) * 2 + (horde ? 1 : 0);
}

inline bool IsWorldBuffQuest(uint32_t questId)
{
    return questId >= kQuestFirst && questId <= kQuestLast;
}

inline bool CapitalRecruiter(uint32_t entry)
{
    for (uint32_t capital : kCapitalRecruiters)
        if (capital == entry)
            return true;
    return false;
}

inline bool HireRecruiterEntry(uint32_t entry)
{
    return entry >= kRecruiterFirst && entry <= kRecruiterLast;
}

inline bool IsFactionUnlockQuest(uint32_t questId, bool horde)
{
    return IsWorldBuffQuest(questId) && ((questId - kQuestFirst) % 2) == (horde ? 1u : 0u);
}

inline uint32_t WarchiefSpell(bool horde)
{
    return horde ? kSpellWarchief : kSpellMightOfStormwind;
}

inline char const* PurchaseName(uint8_t purchase, bool horde)
{
    switch (purchase)
    {
        case kWorldBuffBuyRally: return "Rallying Cry of the Dragonslayer";
        case kWorldBuffBuyWarchief: return horde ? "Warchief's Blessing" : "Might of Stormwind";
        case kWorldBuffBuyZandalar: return "Spirit of Zandalar";
        case kWorldBuffBuyDmPack: return "Dire Maul Tribute";
        case kWorldBuffBuySayge: return "Sayge's Dark Fortune";
        case kWorldBuffBuySongflower: return "Songflower Serenade";
        case kWorldBuffBuySilithyst: return "Traces of Silithyst";
        default: return nullptr;
    }
}

// Spells one purchase applies (apply) and removes first (strip). Reuses TB's catalogue for everything except the
// faction entry and the Dire Maul pack: 22817 (Fengus' Ferocity) is a dummy aura on this core's data and gives
// nothing, so it is not sold (it still counts as a real Dire Maul tribute for the unlock).
inline bool PurchaseSpells(uint8_t purchase, uint32_t saygeSpell, bool horde,
    std::vector<uint32_t>& apply, std::vector<uint32_t>& strip)
{
    if (!IsValidWorldBuffPurchase(purchase))
        return false;
    if (!WorldBuffSpells(purchase, saygeSpell, apply, strip))
        return false;
    if (purchase == kWorldBuffBuyWarchief)
    {
        apply.clear();
        apply.push_back(WarchiefSpell(horde));
    }
    if (purchase == kWorldBuffBuyDmPack)
        apply.erase(std::remove(apply.begin(), apply.end(), 22817u), apply.end());
    return !apply.empty();
}

// Aura-gain unlocks: Dire Maul (any one tribute blessing), any Sayge fortune, a Songflower. 0 = not tracked here
// (Rally and Rend/Zandalar are kill quests).
inline uint32_t AuraQuestFor(uint32_t spellId, bool horde)
{
    if (IsDireMaulTributeAura(spellId))
        return UnlockQuestFor(kWorldBuffBuyDmPack, horde);
    if (IsSaygeFortuneAura(spellId))
        return UnlockQuestFor(kWorldBuffBuySayge, horde);
    if (spellId == kWorldBuffSpellSongflower)
        return UnlockQuestFor(kWorldBuffBuySongflower, horde);
    return 0;
}

inline bool IsAuraQuestSpell(uint32_t spellId)
{
    return AuraQuestFor(spellId, false) != 0;
}

inline uint32_t BossKillCredit(uint32_t bossEntry)
{
    if (bossEntry == kBossOnyxia || bossEntry == kBossNefarian)
        return kCreditRally;
    return 0;
}

inline bool CasterIsRecruiter(bool casterIsCreature, uint32_t casterEntry)
{
    return casterIsCreature && HireRecruiterEntry(casterEntry);
}

// One place for the aura credit gate: a recruiter-cast (purchased) aura never unlocks, a raid instance never counts,
// and only the receiver still holding the quest in progress is credited.
inline bool ShouldCreditAura(bool enabled, bool casterIsRecruiter, bool inRaid,
    bool questIncomplete, bool alreadyRewarded)
{
    return enabled && !casterIsRecruiter && !inRaid && questIncomplete && !alreadyRewarded;
}

// The core strips nothing on raid entry on this realm (the old instance_buff_removal list did not come along), so the
// module owns the rule: keep-in-raids off means the world buffs go when the player lands in a raid instance.
inline bool ShouldStripOnRaidEntry(bool enabled, bool keepInRaids, bool mapIsRaid)
{
    return enabled && !keepInRaids && mapIsRaid;
}

// TB's instance_buff_removal spell set plus Might of Stormwind. Silithyst (29534) is a PvP-zone buff and stays.
inline uint32_t const* RaidStripSpells(uint32_t& count)
{
    static uint32_t const spells[] = {
        15366,
        16609,
        18968,
        22817, 22818, 22820,
        22888,
        23735, 23736, 23737, 23738,
        23766, 23767, 23768, 23769,
        24425,
        26393,
        28681,
        460939,
    };
    count = static_cast<uint32_t>(sizeof(spells) / sizeof(spells[0]));
    return spells;
}

inline bool IsRaidStripSpell(uint32_t spellId)
{
    uint32_t count = 0;
    for (uint32_t const* p = RaidStripSpells(count); count > 0; --count, ++p)
        if (*p == spellId)
            return true;
    return false;
}

} // namespace TortoiseBots::hire::worldbuffs
