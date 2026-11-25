// Standalone test of the world-buff pure half (forever/src/host/hire/TbHireWorldBuffsPure.h): the Forever ids, the
// faction catalogue (Might of Stormwind for Alliance, Warchief's Blessing for Horde), the Dire Maul pack without the
// dummy 22817, the aura/kill credit map, the recruiter-caster filter and the raid rule. Nothing else compiles this file:
// forever/tools/test_hire_worldbuff_wiring.py builds and runs it.
//
// Build and run (from the repository root):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_hire_worldbuff_policy.cpp -o /tmp/test_hire_worldbuff_policy
//   /tmp/test_hire_worldbuff_policy

#include "forever/src/host/hire/TbHireWorldBuffsPure.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using namespace TortoiseBots;
using namespace TortoiseBots::hire::worldbuffs;

static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSenders()
{
    CHECK(kSenderRoot == 505);
    CHECK(kSenderBuy == 506);
    CHECK(kSenderSayge == 507);
    CHECK(kSenderBack == 508);
    CHECK(kSenderConfirm == 509);
    CHECK(kSenderRoot < kSenderBuy && kSenderBuy < kSenderSayge && kSenderSayge < kSenderBack &&
        kSenderBack < kSenderConfirm);
}

static void TestQuestMap()
{
    std::set<uint32_t> ids;
    for (uint8_t purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        uint32_t a = UnlockQuestFor(purchase, false);
        uint32_t h = UnlockQuestFor(purchase, true);
        CHECK(a >= kQuestFirst && a <= kQuestLast);
        CHECK(h >= kQuestFirst && h <= kQuestLast);
        CHECK(h == a + 1);
        CHECK(IsWorldBuffQuest(a) && IsWorldBuffQuest(h));
        CHECK(IsFactionUnlockQuest(a, false));
        CHECK(!IsFactionUnlockQuest(a, true));
        CHECK(IsFactionUnlockQuest(h, true));
        CHECK(!IsFactionUnlockQuest(h, false));
        ids.insert(a);
        ids.insert(h);
    }
    CHECK(ids.size() == 14);
    CHECK(*ids.begin() == kQuestFirst);
    CHECK(*ids.rbegin() == kQuestLast);
    CHECK(UnlockQuestFor(0, false) == 0);
    CHECK(UnlockQuestFor(kWorldBuffBuyCount + 1, false) == 0);
    CHECK(!IsWorldBuffQuest(kQuestFirst - 1));
    CHECK(!IsWorldBuffQuest(kQuestLast + 1));
    // The contracts package owns 100050+.
    CHECK(!IsWorldBuffQuest(100050));
}

static void TestRecruiters()
{
    CHECK(CapitalRecruiter(1000017)); // Stormwind
    CHECK(CapitalRecruiter(1000006)); // Ironforge
    CHECK(CapitalRecruiter(1000012)); // Darnassus
    CHECK(CapitalRecruiter(1000025)); // Orgrimmar
    CHECK(CapitalRecruiter(1000018)); // Undercity
    CHECK(CapitalRecruiter(1000019)); // Thunder Bluff
    CHECK(!CapitalRecruiter(1000000));
    CHECK(!CapitalRecruiter(1000067));
    CHECK(!CapitalRecruiter(1000016));
    CHECK(HireRecruiterEntry(kRecruiterFirst));
    CHECK(HireRecruiterEntry(kRecruiterLast));
    CHECK(!HireRecruiterEntry(kRecruiterFirst - 1));
    CHECK(!HireRecruiterEntry(kRecruiterLast + 1));
    CHECK(CasterIsRecruiter(true, 1000017));
    CHECK(!CasterIsRecruiter(true, 1000068));
    CHECK(!CasterIsRecruiter(false, 1000017));
    // Credit-only creatures are not recruiters: their auras never exist, but the filter must not treat them as casts.
    CHECK(!HireRecruiterEntry(kCreditRally));
}

static void TestFactionAndCatalogue()
{
    CHECK(WarchiefSpell(false) == 460939);
    CHECK(WarchiefSpell(true) == 16609);
    {
        std::vector<uint32_t> apply, strip;
        CHECK(!PurchaseSpells(0, 0, false, apply, strip));
        CHECK(!PurchaseSpells(kWorldBuffBuyCount + 1, 0, false, apply, strip));
    }

    // Rally
    {
        std::vector<uint32_t> apply, strip;
        CHECK(PurchaseSpells(kWorldBuffBuyRally, 0, false, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 22888);
        CHECK(strip.empty());
    }
    // Entry 2 is the faction split.
    {
        std::vector<uint32_t> apply, strip;
        CHECK(PurchaseSpells(kWorldBuffBuyWarchief, 0, false, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 460939);
        CHECK(PurchaseSpells(kWorldBuffBuyWarchief, 0, true, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 16609);
    }
    // Zandalar
    {
        std::vector<uint32_t> apply, strip;
        CHECK(PurchaseSpells(kWorldBuffBuyZandalar, 0, false, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 24425);
    }
    // Dire Maul: 22817 is a dummy aura on this core, the pack sells the two that work.
    {
        std::vector<uint32_t> apply, strip;
        CHECK(PurchaseSpells(kWorldBuffBuyDmPack, 0, false, apply, strip));
        CHECK(apply.size() == 2);
        CHECK(apply[0] == 22818);
        CHECK(apply[1] == 22820);
        for (uint32_t spell : apply)
            CHECK(spell != 22817);
    }
    // Sayge: chosen fortune applied, the other seven stripped.
    {
        uint32_t count = 0;
        WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
        CHECK(count == 8);
        for (uint32_t i = 0; i < count; ++i)
        {
            std::vector<uint32_t> apply, strip;
            CHECK(PurchaseSpells(kWorldBuffBuySayge, picks[i].spellId, false, apply, strip));
            CHECK(apply.size() == 1 && apply[0] == picks[i].spellId);
            CHECK(strip.size() == 7);
            for (uint32_t sibling : strip)
                CHECK(sibling != picks[i].spellId);
        }
        std::vector<uint32_t> apply, strip;
        CHECK(!PurchaseSpells(kWorldBuffBuySayge, 12345, false, apply, strip));
    }
    // Songflower and Silithyst.
    {
        std::vector<uint32_t> apply, strip;
        CHECK(PurchaseSpells(kWorldBuffBuySongflower, 0, false, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 15366);
        CHECK(PurchaseSpells(kWorldBuffBuySilithyst, 0, false, apply, strip));
        CHECK(apply.size() == 1 && apply[0] == 29534);
    }
    CHECK(PurchaseName(kWorldBuffBuyWarchief, false) != nullptr);
    CHECK(std::string(PurchaseName(kWorldBuffBuyWarchief, false)) == "Might of Stormwind");
    CHECK(std::string(PurchaseName(kWorldBuffBuyWarchief, true)) == "Warchief's Blessing");
    CHECK(PurchaseName(0, false) == nullptr);
}

static void TestAuraCredit()
{
    for (uint32_t spell : { 22817u, 22818u, 22820u })
    {
        CHECK(AuraQuestFor(spell, false) == UnlockQuestFor(kWorldBuffBuyDmPack, false));
        CHECK(AuraQuestFor(spell, true) == UnlockQuestFor(kWorldBuffBuyDmPack, true));
        CHECK(IsAuraQuestSpell(spell));
    }
    uint32_t count = 0;
    WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        CHECK(AuraQuestFor(picks[i].spellId, false) == UnlockQuestFor(kWorldBuffBuySayge, false));
        CHECK(AuraQuestFor(picks[i].spellId, true) == UnlockQuestFor(kWorldBuffBuySayge, true));
        CHECK(IsAuraQuestSpell(picks[i].spellId));
    }
    CHECK(AuraQuestFor(15366, false) == UnlockQuestFor(kWorldBuffBuySongflower, false));
    CHECK(AuraQuestFor(15366, true) == UnlockQuestFor(kWorldBuffBuySongflower, true));
    CHECK(IsAuraQuestSpell(15366));
    // Kill-credited buffs are not aura quests.
    CHECK(AuraQuestFor(22888, false) == 0);
    CHECK(AuraQuestFor(16609, false) == 0);
    CHECK(!IsAuraQuestSpell(460939));

    CHECK(ShouldCreditAura(true, false, false, true, false));
    CHECK(!ShouldCreditAura(false, false, false, true, false));
    CHECK(!ShouldCreditAura(true, true, false, true, false));    // purchased aura
    CHECK(!ShouldCreditAura(true, false, true, true, false));    // raid instance
    CHECK(!ShouldCreditAura(true, false, false, false, false));  // not in the log
    CHECK(!ShouldCreditAura(true, false, false, true, true));    // already rewarded
}

static void TestKillCredit()
{
    CHECK(BossKillCredit(kBossOnyxia) == kCreditRally);
    CHECK(BossKillCredit(kBossNefarian) == kCreditRally);
    CHECK(BossKillCredit(kBossRend) == 0);   // direct quest objective, the core shares the kill
    CHECK(BossKillCredit(kBossHakkar) == 0);
    CHECK(BossKillCredit(10339) == 0);       // Gyth dies with Rend and must not credit
    CHECK(BossKillCredit(0) == 0);
    CHECK(kCreditRally != kCreditSilithyst);
    CHECK(kSilithystKills == 5);
    CHECK(kZoneSilithus == 1377);
}

static void TestRaidRule()
{
    CHECK(ShouldStripOnRaidEntry(true, false, true));
    CHECK(!ShouldStripOnRaidEntry(true, true, true));    // keep-in-raids
    CHECK(!ShouldStripOnRaidEntry(true, false, false));  // a dungeon or the world never strips
    CHECK(!ShouldStripOnRaidEntry(false, false, true));  // feature off
    uint32_t count = 0;
    uint32_t const* spells = RaidStripSpells(count);
    CHECK(count >= 19);
    CHECK(IsRaidStripSpell(22888));
    CHECK(IsRaidStripSpell(16609));
    CHECK(IsRaidStripSpell(460939));
    CHECK(IsRaidStripSpell(24425));
    CHECK(IsRaidStripSpell(22818));
    CHECK(IsRaidStripSpell(22820));
    CHECK(IsRaidStripSpell(15366));
    CHECK(IsRaidStripSpell(23768));
    CHECK(!IsRaidStripSpell(29534));  // Silithyst is a PvP-zone buff, TB never stripped it
    CHECK(!IsRaidStripSpell(0));
    (void)spells;
}

static void TestPrices()
{
    // The shop reuses TB's price pairs; the tier table must still resolve for every purchase.
    for (uint8_t purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        WorldBuffPricePair pair;
        CHECK(WorldBuffPrices(purchase, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000));
        CHECK(pair.baseCopper > 0);
    }
    WorldBuffPricePair pair;
    CHECK(!WorldBuffPrices(0, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000));
    // Standard tier: 10g + 2g per head, head count includes the buyer.
    CHECK(WorldBuffTotalPrice(100000, 20000, 1) == 120000);
    CHECK(WorldBuffTotalPrice(100000, 20000, 5) == 200000);
    CHECK(WorldBuffTotalPrice(100000, 20000, 0) == 120000);
}

int main()
{
    TestSenders();
    TestQuestMap();
    TestRecruiters();
    TestFactionAndCatalogue();
    TestAuraCredit();
    TestKillCredit();
    TestRaidRule();
    TestPrices();
    std::printf("%d checks, 0 failed\n", checks);
    return 0;
}
