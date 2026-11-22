// Standalone regression test for the solo warlock default-pet rule
// (runtime/WarlockPetPolicy.h): once a pool bot knows Summon Voidwalker
// (697) AND holds a Soul Shard (6265, the summon reagent) the upkeep summons
// the Voidwalker; below that it keeps the Imp, and a shardless bot stays
// quiet instead of queueing a summon that fails isPossible. Owned/hired
// bots (live real-player master) are never touched.
//
// Also covers the opt-in spec-fitting pet (AiPlayerbot.WarlockPetBySpec):
// Affliction/Demonology -> Felhunter -> Succubus -> Voidwalker -> Imp by what
// is learned, Destruction -> Imp, a tankless group keeps the Voidwalker, live
// pets only swap for pool bots, a missing shard keeps the existing fallback.
// WarlockSpecPetWanted (the caller's early return, before the bag read) is None
// exactly when the decision is LeaveAlone whatever the shard holds.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_warlock_pet_policy.cpp -o /tmp/test_warlock_pet
//   /tmp/test_warlock_pet

#include "../runtime/policy/pet/WarlockPetPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static WarlockSoloPetInputs Solo(bool knowsVW, bool hasShard, bool hasPet, uint32_t entry)
{
    WarlockSoloPetInputs inputs;
    inputs.knowsVoidwalker = knowsVW;
    inputs.hasSoulShard = hasShard;
    inputs.hasPet = hasPet;
    inputs.currentPetEntry = entry;
    return inputs;
}

// Petless lvl10+ pool bot that knows the summon and holds a shard: summon.
static void TestPetlessSummonsVoidwalker()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, false, 0)) == WarlockSoloPetDecision::SummonVoidwalker);
}

// Imp running past the tier with a shard: upgrade to the Voidwalker.
static void TestImpUpgrades()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::SummonVoidwalker);
}

// Voidwalker already out: leave it alone (no resummon loop).
static void TestVoidwalkerLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, WARLOCK_VOIDWALKER_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
}

// Below the tier (no 697): the Imp stays, petless stays petless.
static void TestBelowTierKeepsImp()
{
    CHECK(DecideWarlockSoloPet(Solo(false, true, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(false, true, false, 0)) == WarlockSoloPetDecision::LeaveAlone);
}

// Shardless at/above the tier: stay quiet so no failed summon is queued
// (petless falls back to the "no pet" summon-imp node instead).
static void TestShardlessStaysQuiet()
{
    CHECK(DecideWarlockSoloPet(Solo(true, false, true, WARLOCK_IMP_PET_ENTRY)) ==
        WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(true, false, false, 0)) == WarlockSoloPetDecision::LeaveAlone);
}

// Exotic current pet (Succubus/Felhunter from a manual order): never override.
static void TestExoticPetLeftAlone()
{
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, 1863)) == WarlockSoloPetDecision::LeaveAlone);
    CHECK(DecideWarlockSoloPet(Solo(true, true, true, 417)) == WarlockSoloPetDecision::LeaveAlone);
}

// Owned/hired bot with a live master: the player decides, always.
static void TestOwnedBotLeftAlone()
{
    WarlockSoloPetInputs inputs = Solo(true, true, false, 0);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
    inputs = Solo(true, true, true, WARLOCK_IMP_PET_ENTRY);
    inputs.hasRealPlayerMaster = true;
    CHECK(DecideWarlockSoloPet(inputs) == WarlockSoloPetDecision::LeaveAlone);
}

// Pin the server-data constants so a stray edit faces this test.
static void TestConstants()
{
    CHECK(WARLOCK_VOIDWALKER_SUMMON_SPELL == 697);
    CHECK(WARLOCK_VOIDWALKER_PET_ENTRY == 1860);
    CHECK(WARLOCK_IMP_PET_ENTRY == 416);
    CHECK(WARLOCK_SOUL_SHARD_ITEM == 6265);
}

// ---- spec-fitting pet (AiPlayerbot.WarlockPetBySpec) -----------------------

static WarlockSpecPetInputs Spec(WarlockSpec spec, int level, bool shard = true)
{
    WarlockSpecPetInputs in;
    in.spec = spec;
    in.knowsVoidwalker = level >= 10;
    in.knowsSuccubus = level >= 20;
    in.knowsFelhunter = level >= 30;
    in.hasSoulShard = shard;
    return in;
}

static void TestSpecPreferenceMapping()
{
    // Affliction and Demonology share the donor chain (no Felguard in 1.12).
    for (WarlockSpec spec : { WarlockSpec::Affliction, WarlockSpec::Demonology })
    {
        CHECK(WarlockSpecPetPreference(spec, false, false, false) == WarlockPetKind::Imp);
        CHECK(WarlockSpecPetPreference(spec, true, false, false) == WarlockPetKind::Voidwalker);
        CHECK(WarlockSpecPetPreference(spec, true, true, false) == WarlockPetKind::Succubus);
        CHECK(WarlockSpecPetPreference(spec, true, true, true) == WarlockPetKind::Felhunter);
    }
    // Destruction is the Imp at every level.
    CHECK(WarlockSpecPetPreference(WarlockSpec::Destruction, false, false, false) == WarlockPetKind::Imp);
    CHECK(WarlockSpecPetPreference(WarlockSpec::Destruction, true, true, true) == WarlockPetKind::Imp);
    CHECK(WarlockSpecPetPreference(WarlockSpec::Unknown, true, true, true) == WarlockPetKind::None);
}

static void TestSpecPetlessSolo()
{
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Affliction, 60)) == WarlockSpecPetDecision::SummonFelhunter);
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Demonology, 25)) == WarlockSpecPetDecision::SummonSuccubus);
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Affliction, 15)) == WarlockSpecPetDecision::SummonVoidwalker);
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Demonology, 5)) == WarlockSpecPetDecision::SummonImp);
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Destruction, 60)) == WarlockSpecPetDecision::SummonImp);
    // Spec unknown (no class strategy yet): existing rows decide.
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Unknown, 60)) == WarlockSpecPetDecision::LeaveAlone);
}

static void TestSpecTankRuleKeepsVoidwalker()
{
    WarlockSpecPetInputs in = Spec(WarlockSpec::Affliction, 60);
    in.petIsGroupTank = true;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::SummonVoidwalker);
    in = Spec(WarlockSpec::Destruction, 60);
    in.petIsGroupTank = true;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::SummonVoidwalker);
    // Tank rule needs the Voidwalker learned; below 10 the chain is unchanged.
    in = Spec(WarlockSpec::Affliction, 5);
    in.petIsGroupTank = true;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::SummonImp);
}

static void TestSpecShardGate()
{
    // Shardless: every demon but the Imp stays quiet (the "no pet" summon-imp
    // fallback covers the petless case); the Imp needs none.
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Affliction, 60, false)) == WarlockSpecPetDecision::LeaveAlone);
    CHECK(DecideWarlockSpecPet(Spec(WarlockSpec::Destruction, 60, false)) == WarlockSpecPetDecision::SummonImp);
}

static void TestSpecLivePetSwap()
{
    // Pool bot: a default demon that does not fit is swapped; a fitting one stays.
    WarlockSpecPetInputs in = Spec(WarlockSpec::Affliction, 60);
    in.hasPet = true;
    in.canSwapLivePet = true;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::SummonFelhunter);
    in.currentPetEntry = WARLOCK_FELHUNTER_PET_ENTRY;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::LeaveAlone);
    // No ping-pong with the tank rule: Voidwalker out under a tankless group stays.
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    in.petIsGroupTank = true;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::LeaveAlone);
    // Shardless pool bot keeps its live demon (no swap to the Imp).
    in = Spec(WarlockSpec::Affliction, 60, false);
    in.hasPet = true;
    in.canSwapLivePet = true;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::LeaveAlone);
    // Destruction pool bot with a Voidwalker swaps to the Imp (no shard needed).
    in = Spec(WarlockSpec::Destruction, 60, false);
    in.hasPet = true;
    in.canSwapLivePet = true;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::SummonImp);
    // A player-owned bot never swaps a live pet.
    in = Spec(WarlockSpec::Affliction, 60);
    in.hasPet = true;
    in.canSwapLivePet = false;
    in.currentPetEntry = WARLOCK_IMP_PET_ENTRY;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::LeaveAlone);
    // An unknown creature (not one of the four demons) is left alone.
    in.canSwapLivePet = true;
    in.currentPetEntry = 12345;
    CHECK(DecideWarlockSpecPet(in) == WarlockSpecPetDecision::LeaveAlone);
}

static void TestSpecConstants()
{
    CHECK(WARLOCK_SUCCUBUS_SUMMON_SPELL == 712);
    CHECK(WARLOCK_FELHUNTER_SUMMON_SPELL == 691);
    CHECK(WARLOCK_SUCCUBUS_PET_ENTRY == 1863);
    CHECK(WARLOCK_FELHUNTER_PET_ENTRY == 417);
    CHECK(WarlockPetEntry(WarlockPetKind::Imp) == 416);
    CHECK(WarlockPetKindFromEntry(417) == WarlockPetKind::Felhunter);
    CHECK(WarlockPetKindFromEntry(0) == WarlockPetKind::None);
}

static WarlockSpecPetDecision DecisionFor(WarlockPetKind kind)
{
    switch (kind)
    {
        case WarlockPetKind::Imp: return WarlockSpecPetDecision::SummonImp;
        case WarlockPetKind::Voidwalker: return WarlockSpecPetDecision::SummonVoidwalker;
        case WarlockPetKind::Succubus: return WarlockSpecPetDecision::SummonSuccubus;
        case WarlockPetKind::Felhunter: return WarlockSpecPetDecision::SummonFelhunter;
        default: return WarlockSpecPetDecision::LeaveAlone;
    }
}

// The caller's early return: WarlockSpecPetWanted is None when the live pet
// stands (or the spec is unknown), and the caller then never reads the bag.
static void TestSpecWantedStands()
{
    // Affliction 60 with a Felhunter out in a pool bot: the pet stands.
    WarlockSpecPetInputs in = Spec(WarlockSpec::Affliction, 60);
    in.hasPet = true;
    in.canSwapLivePet = true;
    in.currentPetEntry = WARLOCK_FELHUNTER_PET_ENTRY;
    CHECK(WarlockSpecPetWanted(in) == WarlockPetKind::None);
    // Destruction with an Imp out stands too, until a tankless group wants the Voidwalker.
    in = Spec(WarlockSpec::Destruction, 60);
    in.hasPet = true;
    in.canSwapLivePet = true;
    in.currentPetEntry = WARLOCK_IMP_PET_ENTRY;
    CHECK(WarlockSpecPetWanted(in) == WarlockPetKind::None);
    in.petIsGroupTank = true;
    CHECK(WarlockSpecPetWanted(in) == WarlockPetKind::Voidwalker);
    // A player-owned bot, a non-default demon and an unknown spec always stand.
    in = Spec(WarlockSpec::Affliction, 60);
    in.hasPet = true;
    in.canSwapLivePet = false;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(WarlockSpecPetWanted(in) == WarlockPetKind::None);
    in.canSwapLivePet = true;
    in.currentPetEntry = 12345;
    CHECK(WarlockSpecPetWanted(in) == WarlockPetKind::None);
    CHECK(WarlockSpecPetWanted(Spec(WarlockSpec::Unknown, 60)) == WarlockPetKind::None);
    // Petless: the spec's demon is wanted, so the shard read follows.
    CHECK(WarlockSpecPetWanted(Spec(WarlockSpec::Affliction, 60)) == WarlockPetKind::Felhunter);
}

// Over the whole input grid: a None from WarlockSpecPetWanted decides LeaveAlone
// with or without a shard (so skipping the bag read is exact), and any other
// wanted demon decides its summon once a shard is held; without one only the
// Imp, which needs no shard, still summons.
static void TestSpecWantedIsShardFree()
{
    WarlockSpec const specs[] = { WarlockSpec::Unknown, WarlockSpec::Affliction, WarlockSpec::Demonology, WarlockSpec::Destruction };
    uint32_t const pets[] = { 416, 1860, 1863, 417, 12345 };
    for (WarlockSpec spec : specs)
    for (int knows = 0; knows < 8; ++knows)
    for (int tank = 0; tank < 2; ++tank)
    for (int petState = 0; petState < 11; ++petState) // 0: petless; else canSwap x pets
    {
        WarlockSpecPetInputs in;
        in.spec = spec;
        in.knowsVoidwalker = (knows & 1) != 0;
        in.knowsSuccubus = (knows & 2) != 0;
        in.knowsFelhunter = (knows & 4) != 0;
        in.petIsGroupTank = tank != 0;
        in.hasPet = petState > 0;
        if (in.hasPet)
        {
            in.canSwapLivePet = (petState - 1) / 5 != 0;
            in.currentPetEntry = pets[(petState - 1) % 5];
        }
        WarlockPetKind const want = WarlockSpecPetWanted(in);
        in.hasSoulShard = false;
        WarlockSpecPetDecision const noShard = DecideWarlockSpecPet(in);
        in.hasSoulShard = true;
        WarlockSpecPetDecision const withShard = DecideWarlockSpecPet(in);
        if (want == WarlockPetKind::None)
        {
            CHECK(noShard == WarlockSpecPetDecision::LeaveAlone);
            CHECK(withShard == WarlockSpecPetDecision::LeaveAlone);
        }
        else
        {
            CHECK(withShard == DecisionFor(want));
            CHECK(noShard == (want == WarlockPetKind::Imp ? WarlockSpecPetDecision::SummonImp : WarlockSpecPetDecision::LeaveAlone));
        }
    }
}

// ---- default pet: the best demon the warlock knows -------------------------

static WarlockAutoPetInputs Auto(bool imp, bool vw, bool suc, bool fh, bool shard)
{
    WarlockAutoPetInputs in;
    in.knowsImp = imp;
    in.knowsVoidwalker = vw;
    in.knowsSuccubus = suc;
    in.knowsFelhunter = fh;
    in.hasSoulShard = shard;
    return in;
}

// The donor's summon chain, highest learned demon first: Felhunter, Succubus,
// Voidwalker, Imp. The ticket's case is the last line's sibling: a level-1
// warlock holds only 688 and must summon the Imp, not wait for 697.
static void TestAutoLadderByWhatIsKnown()
{
    CHECK(WarlockBestKnownPet(true, false, false, false) == WarlockPetKind::Imp);
    CHECK(WarlockBestKnownPet(true, true, false, false) == WarlockPetKind::Voidwalker);
    CHECK(WarlockBestKnownPet(true, true, true, false) == WarlockPetKind::Succubus);
    CHECK(WarlockBestKnownPet(true, true, true, true) == WarlockPetKind::Felhunter);
    CHECK(WarlockBestKnownPet(false, false, false, false) == WarlockPetKind::None);
    CHECK(DecideWarlockAutoPet(Auto(true, false, false, false, false)) == WarlockAutoPetDecision::SummonImp);
    CHECK(DecideWarlockAutoPet(Auto(true, true, false, false, true)) == WarlockAutoPetDecision::SummonVoidwalker);
    CHECK(DecideWarlockAutoPet(Auto(true, true, true, false, true)) == WarlockAutoPetDecision::SummonSuccubus);
    CHECK(DecideWarlockAutoPet(Auto(true, true, true, true, true)) == WarlockAutoPetDecision::SummonFelhunter);
}

// A petless warlock without a shard still summons: the chain drops to the
// Imp, which needs none.
static void TestAutoShardlessPetlessFallsToImp()
{
    CHECK(DecideWarlockAutoPet(Auto(true, true, false, false, false)) == WarlockAutoPetDecision::SummonImp);
    CHECK(DecideWarlockAutoPet(Auto(true, true, true, true, false)) == WarlockAutoPetDecision::SummonImp);
    // No Imp learned either: nothing to summon.
    CHECK(DecideWarlockAutoPet(Auto(false, true, true, true, false)) == WarlockAutoPetDecision::LeaveAlone);
}

static void TestAutoLivePetStaysOrUpgrades()
{
    // The best demon is already out: no resummon loop.
    WarlockAutoPetInputs in = Auto(true, true, true, false, true);
    in.hasPet = true;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::SummonSuccubus);
    in.currentPetEntry = WARLOCK_SUCCUBUS_PET_ENTRY;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);

    // A swap costs a shard; without one the live demon stays (never a downgrade).
    in = Auto(true, true, true, false, false);
    in.hasPet = true;
    in.currentPetEntry = WARLOCK_VOIDWALKER_PET_ENTRY;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);
    in.currentPetEntry = WARLOCK_IMP_PET_ENTRY;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);

    // A live Imp below the Voidwalker tier is the choice already.
    in = Auto(true, false, false, false, false);
    in.hasPet = true;
    in.currentPetEntry = WARLOCK_IMP_PET_ENTRY;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);
}

// A player's pick wins: their manual strategy or `.bot choice pet` order, and
// a pet a real player's bot already has is never swapped.
static void TestAutoPlayerChoiceWins()
{
    WarlockAutoPetInputs in = Auto(true, true, true, true, true);
    in.choiceOwned = true;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);
    CHECK(WarlockAutoPetWanted(in) == WarlockPetKind::None);

    in = Auto(true, true, false, false, true);
    in.hasPet = true;
    in.currentPetEntry = WARLOCK_IMP_PET_ENTRY;
    in.hasRealPlayerMaster = true;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);
    // A petless owned bot still gets a demon.
    in.hasPet = false;
    in.currentPetEntry = 0;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::SummonVoidwalker);

    // A pet that is not one of the four demons is not swapped.
    in = Auto(true, true, true, true, true);
    in.hasRealPlayerMaster = false;
    in.hasPet = true;
    in.currentPetEntry = 12345;
    CHECK(DecideWarlockAutoPet(in) == WarlockAutoPetDecision::LeaveAlone);
}

static WarlockAutoPetDecision DecisionForAuto(WarlockPetKind kind)
{
    switch (kind)
    {
        case WarlockPetKind::Imp: return WarlockAutoPetDecision::SummonImp;
        case WarlockPetKind::Voidwalker: return WarlockAutoPetDecision::SummonVoidwalker;
        case WarlockPetKind::Succubus: return WarlockAutoPetDecision::SummonSuccubus;
        case WarlockPetKind::Felhunter: return WarlockAutoPetDecision::SummonFelhunter;
        default: return WarlockAutoPetDecision::LeaveAlone;
    }
}

// Without a shard a petless warlock drops down the chain to the Imp (which
// needs none); a live demon is never swapped for want of a shard.
static WarlockAutoPetDecision AutoWithoutShard(WarlockPetKind want, bool knowsImp, bool hasPet)
{
    if (want == WarlockPetKind::Imp)
        return WarlockAutoPetDecision::SummonImp;
    if (hasPet || !knowsImp)
        return WarlockAutoPetDecision::LeaveAlone;
    return WarlockAutoPetDecision::SummonImp;
}

// The caller's early return (before the bag read): Wanted is None exactly when
// the decision is LeaveAlone whatever the shard count, and a wanted demon only
// needs the shard when it is not the Imp.
static void TestAutoWantedIsShardFree()
{
    bool const known[] = { false, true };
    uint32_t const pets[] = { 0, 416, 1860, 1863, 417, 12345 };
    for (bool imp : known)
    for (bool vw : known)
    for (bool suc : known)
    for (bool fh : known)
    for (bool owned : known)
    for (int ownedChoice = 0; ownedChoice < 2; ++ownedChoice)
    for (int petState = 0; petState < 11; ++petState)
    {
        WarlockAutoPetInputs in;
        in.knowsImp = imp;
        in.knowsVoidwalker = vw;
        in.knowsSuccubus = suc;
        in.knowsFelhunter = fh;
        in.hasRealPlayerMaster = owned;
        in.choiceOwned = ownedChoice != 0;
        in.hasPet = petState > 0;
        if (in.hasPet)
            in.currentPetEntry = pets[(petState - 1) % 6];
        WarlockPetKind const want = WarlockAutoPetWanted(in);
        in.hasSoulShard = false;
        WarlockAutoPetDecision const noShard = DecideWarlockAutoPet(in);
        in.hasSoulShard = true;
        WarlockAutoPetDecision const withShard = DecideWarlockAutoPet(in);
        if (want == WarlockPetKind::None)
        {
            CHECK(noShard == WarlockAutoPetDecision::LeaveAlone);
            CHECK(withShard == WarlockAutoPetDecision::LeaveAlone);
        }
        else
        {
            CHECK(withShard == DecisionForAuto(want));
            CHECK(noShard == AutoWithoutShard(want, imp, in.hasPet));
        }
    }
}

static void TestAutoConstants()
{
    CHECK(WARLOCK_IMP_SUMMON_SPELL == 688);
}

int main()
{
    std::printf("Starting warlock pet policy tests...\n");
    TestPetlessSummonsVoidwalker();
    std::printf("  [PASS] petless summons voidwalker\n");
    TestImpUpgrades();
    std::printf("  [PASS] imp upgrades\n");
    TestVoidwalkerLeftAlone();
    std::printf("  [PASS] voidwalker left alone\n");
    TestBelowTierKeepsImp();
    std::printf("  [PASS] below tier keeps imp\n");
    TestShardlessStaysQuiet();
    std::printf("  [PASS] shardless stays quiet\n");
    TestExoticPetLeftAlone();
    std::printf("  [PASS] exotic pet left alone\n");
    TestOwnedBotLeftAlone();
    std::printf("  [PASS] owned bot left alone\n");
    TestConstants();
    std::printf("  [PASS] constants\n");
    TestSpecPreferenceMapping();
    std::printf("  [PASS] spec preference mapping\n");
    TestSpecPetlessSolo();
    std::printf("  [PASS] spec petless solo\n");
    TestSpecTankRuleKeepsVoidwalker();
    std::printf("  [PASS] spec tank rule keeps voidwalker\n");
    TestSpecShardGate();
    std::printf("  [PASS] spec shard gate\n");
    TestSpecLivePetSwap();
    std::printf("  [PASS] spec live pet swap\n");
    TestSpecConstants();
    std::printf("  [PASS] spec constants\n");
    TestSpecWantedStands();
    std::printf("  [PASS] spec wanted stands when the pet matches\n");
    TestSpecWantedIsShardFree();
    std::printf("  [PASS] spec wanted is shard free\n");
    TestAutoLadderByWhatIsKnown();
    std::printf("  [PASS] auto ladder follows the donor order\n");
    TestAutoShardlessPetlessFallsToImp();
    std::printf("  [PASS] auto shardless petless falls to the imp\n");
    TestAutoLivePetStaysOrUpgrades();
    std::printf("  [PASS] auto live pet stays or upgrades\n");
    TestAutoPlayerChoiceWins();
    std::printf("  [PASS] auto player choice wins\n");
    TestAutoWantedIsShardFree();
    std::printf("  [PASS] auto wanted is shard free\n");
    TestAutoConstants();
    std::printf("  [PASS] auto constants\n");
    std::printf("All warlock pet policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
