// Standalone test of the crowd-control permission policy (ai/playerbot/policy/combat/CcPermissionPolicy.h, issue #12):
//   g++ -std=c++17 -O1 -Wall -Wextra forever/tools/test_cc_permissions.cpp -o /tmp/test_cc_permissions && /tmp/test_cc_permissions
// No core, no database: the header is pure.
#include "../../ai/playerbot/policy/combat/CcPermissionPolicy.h"

#include <cstdio>
#include <string>

namespace cc = ai::CcPermissions;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void KindWords()
{
    CHECK(cc::KindFromWord("fear") == cc::kFear);
    CHECK(cc::KindFromWord("sheep") == cc::kSheep);
    CHECK(cc::KindFromWord("banish") == cc::kBanish);
    CHECK(cc::KindFromWord("shackle") == cc::kShackle);
    CHECK(cc::KindFromWord("sap") == cc::kSap);
    CHECK(cc::KindFromWord("sleep") == cc::kSleep);
    CHECK(cc::KindFromWord("trap") == cc::kTrap);
    CHECK(cc::KindFromWord("charm") == cc::kCharm);
    CHECK(cc::KindFromWord("all") == cc::kAll);
    CHECK(cc::KindFromWord("cc") == cc::kAll);
    CHECK(cc::KindFromWord("") == 0);
    CHECK(cc::KindFromWord("frostbolt") == 0);
    CHECK(cc::KindFromWord("stun") == 0);
    CHECK(cc::kDefaultDenied == cc::kFear);
}

static void SpellKinds()
{
    CHECK(cc::KindFromSpellName("fear") == cc::kFear);
    CHECK(cc::KindFromSpellName("howl of terror") == cc::kFear);
    CHECK(cc::KindFromSpellName("death coil") == cc::kFear);
    CHECK(cc::KindFromSpellName("psychic scream") == cc::kFear);
    CHECK(cc::KindFromSpellName("intimidating shout") == cc::kFear);
    CHECK(cc::KindFromSpellName("scare beast") == cc::kFear);
    CHECK(cc::KindFromSpellName("turn undead") == cc::kFear);
    CHECK(cc::KindFromSpellName("polymorph") == cc::kSheep);
    CHECK(cc::KindFromSpellName("polymorph: pig") == cc::kSheep);
    CHECK(cc::KindFromSpellName("Polymorph: Turtle") == cc::kSheep);
    CHECK(cc::KindFromSpellName("banish") == cc::kBanish);
    CHECK(cc::KindFromSpellName("shackle undead") == cc::kShackle);
    CHECK(cc::KindFromSpellName("sap") == cc::kSap);
    CHECK(cc::KindFromSpellName("hibernate") == cc::kSleep);
    CHECK(cc::KindFromSpellName("wyvern sting") == cc::kSleep);
    CHECK(cc::KindFromSpellName("freezing trap") == cc::kTrap);
    CHECK(cc::KindFromSpellName("seduction") == cc::kCharm);
    CHECK(cc::KindFromSpellName("enslave demon") == cc::kCharm);
    CHECK(cc::KindFromSpellName("mind control") == cc::kCharm);
    // The DBC writes proper case; the lookup is case-insensitive without allocating.
    CHECK(cc::KindFromSpellName("Fear") == cc::kFear);
    CHECK(cc::KindFromSpellName("POLYMORPH") == cc::kSheep);
    CHECK(cc::KindFromSpellName("Shackle Undead") == cc::kShackle);
    CHECK(cc::KindFromSpellName("WYVERN STING") == cc::kSleep);
    // Stuns, roots, snares, interrupts and everything else: no kind.
    CHECK(cc::KindFromSpellName("") == 0);
    CHECK(cc::KindFromSpellName(nullptr) == 0);
    CHECK(cc::KindFromSpellName("frostbolt") == 0);
    CHECK(cc::KindFromSpellName("kick") == 0);
    CHECK(cc::KindFromSpellName("hamstring") == 0);
    CHECK(cc::KindFromSpellName("entangling roots") == 0);
    CHECK(cc::KindFromSpellName("frost nova") == 0);
    CHECK(cc::KindFromSpellName("intercept") == 0);
    CHECK(cc::KindFromSpellName("polymorphology") == 0);
    // The Razuvious tactic's action is not the charm spell.
    CHECK(cc::KindFromSpellName("razuvious mind control") == 0);
}

static void Classes()
{
    CHECK(cc::ClassFromWord("warrior") == 1);
    CHECK(cc::ClassFromWord("paladin") == 2);
    CHECK(cc::ClassFromWord("hunter") == 3);
    CHECK(cc::ClassFromWord("rogue") == 4);
    CHECK(cc::ClassFromWord("priest") == 5);
    CHECK(cc::ClassFromWord("deathknight") == 6);
    CHECK(cc::ClassFromWord("shaman") == 7);
    CHECK(cc::ClassFromWord("mage") == 8);
    CHECK(cc::ClassFromWord("warlock") == 9);
    CHECK(cc::ClassFromWord("druid") == 11);
    CHECK(cc::ClassFromWord("mages") == 8);
    CHECK(cc::ClassFromWord("warlocks") == 9);
    CHECK(cc::ClassFromWord("priests") == 5);
    CHECK(cc::ClassFromWord("allow") == 0);
    CHECK(cc::ClassFromWord("deny") == 0);
    CHECK(cc::ClassFromWord("") == 0);
    CHECK(std::string(cc::ClassName(9)) == "warlock");
    CHECK(std::string(cc::ClassName(8)) == "mage");
    CHECK(std::string(cc::ClassName(0)) == "");
    CHECK(std::string(cc::ClassName(42)) == "");
}

static void DeniedWordList()
{
    CHECK(cc::DeniedWords(0).empty());
    auto one = cc::DeniedWords(cc::kFear);
    CHECK(one.size() == 1 && one[0] == "fear");
    auto two = cc::DeniedWords(cc::kFear | cc::kTrap);
    CHECK(two.size() == 2 && two[0] == "fear" && two[1] == "trap");
    CHECK(cc::DeniedWords(cc::kAll).size() == 8);
}

static void StoreRules()
{
    cc::Store store;
    CHECK(store.Size() == 0 && store.Version() == 0);
    // No entry: the caller's default (the host passes fear).
    CHECK(store.DeniedFor(7, 9, cc::kDefaultDenied) == cc::kFear);
    std::uint64_t const v0 = store.Version();
    store.Set(7, 9, cc::kFear | cc::kSheep);
    CHECK(store.Version() != v0);
    CHECK(store.DeniedFor(7, 9, cc::kDefaultDenied) == (cc::kFear | cc::kSheep));
    // Another owner and another class are untouched.
    CHECK(store.DeniedFor(8, 9, cc::kDefaultDenied) == cc::kFear);
    CHECK(store.DeniedFor(7, 8, cc::kDefaultDenied) == cc::kFear);
    // Same value twice: no new version for the tick guard.
    std::uint64_t const v1 = store.Version();
    store.Set(7, 9, cc::kFear | cc::kSheep);
    CHECK(store.Version() == v1);
    // An explicit allow (mask 0) wins over the default and the all-classes entry.
    store.Set(7, cc::kAnyClass, cc::kFear);
    CHECK(store.DeniedFor(7, 8, cc::kDefaultDenied) == cc::kFear);
    store.Set(7, 8, 0);
    CHECK(store.DeniedFor(7, 8, cc::kDefaultDenied) == 0);
    CHECK(store.DeniedFor(7, 4, cc::kDefaultDenied) == cc::kFear);
    // allow then deny again.
    store.Set(7, 8, cc::kSap);
    CHECK(store.DeniedFor(7, 8, cc::kDefaultDenied) == cc::kSap);
    CHECK(store.Has(7, 8));
    CHECK(!store.Has(7, 3));
}

int main()
{
    KindWords();
    SpellKinds();
    Classes();
    DeniedWordList();
    StoreRules();
    if (g_fail)
        std::printf("%d/%d checks FAILED\n", g_fail, g_checks);
    else
        std::printf("PASS %d checks\n", g_checks);
    return g_fail != 0;
}
