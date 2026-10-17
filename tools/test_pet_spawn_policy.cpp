// Standalone regression test for the new-pet spawn check
// (runtime/PetSpawnPolicy.h, issue #1043): a bot's freshly summoned pet is
// pulled onto its owner when it appeared out of sight, on another floor, or
// far away; a pet next to its owner stays put, and each pet is checked once.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_spawn_policy.cpp -o /tmp/test_pet_spawn_policy
//   /tmp/test_pet_spawn_policy

#include "../runtime/PetSpawnPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static PetSpawnCheckInputs Inputs(float dx, float dy, float dz, bool sees)
{
    PetSpawnCheckInputs inputs;
    inputs.dx = dx;
    inputs.dy = dy;
    inputs.dz = dz;
    inputs.ownerSeesPet = sees;
    return inputs;
}

// The normal summon: a couple of yards beside the owner, same floor.
static void TestPetBesideOwnerStays()
{
    CHECK(!ShouldPullNewPetToOwner(Inputs(2.0f, 0.5f, 0.3f, true)));
    CHECK(!ShouldPullNewPetToOwner(Inputs(0.0f, 0.0f, 0.0f, true)));
}

// The live #1043 case at the Uldaman portal: 3 yards across, 205 yards up.
static void TestPetOnTheRoofIsPulled()
{
    CHECK(ShouldPullNewPetToOwner(Inputs(-0.5f, 3.2f, 205.5f, true)));
    CHECK(ShouldPullNewPetToOwner(Inputs(-0.5f, 3.2f, 205.5f, false)));
}

// A different floor either way, even within a few yards.
static void TestHeightDifference()
{
    CHECK(!ShouldPullNewPetToOwner(Inputs(1.0f, 1.0f, 5.0f, true)));
    CHECK(ShouldPullNewPetToOwner(Inputs(1.0f, 1.0f, 5.5f, true)));
    CHECK(ShouldPullNewPetToOwner(Inputs(1.0f, 1.0f, -6.0f, true)));
}

static void TestDistance()
{
    CHECK(!ShouldPullNewPetToOwner(Inputs(29.0f, 0.0f, 0.0f, true)));
    CHECK(ShouldPullNewPetToOwner(Inputs(31.0f, 0.0f, 0.0f, true)));
    CHECK(ShouldPullNewPetToOwner(Inputs(22.0f, 22.0f, 0.0f, true)));
}

// Behind a wall on the same floor and close by: still pulled.
static void TestNoLineOfSight()
{
    CHECK(ShouldPullNewPetToOwner(Inputs(3.0f, 0.0f, 0.0f, false)));
}

// Each pet object is checked once; no pet means nothing to check.
static void TestCheckedOncePerPet()
{
    CHECK(!IsUncheckedPet(0, 0));
    CHECK(!IsUncheckedPet(0, 0xF1400000D3000071ull));
    CHECK(IsUncheckedPet(0xF1400000D3000071ull, 0));
    CHECK(!IsUncheckedPet(0xF1400000D3000071ull, 0xF1400000D3000071ull));
    // Call Pet brings the same pet number back as a new object (new guid).
    CHECK(IsUncheckedPet(0xF1400000D3000077ull, 0xF1400000D3000071ull));
}

// The map-thread hook acts only where the core is about to unsummon the pet
// (120 yd): a pet at range, chasing, or a floor away stays lock-free.
static void TestUnsummonGuard()
{
    CHECK(!IsPetAboutToBeUnsummoned(Inputs(35.0f, 0.0f, 0.0f, true)));
    CHECK(!IsPetAboutToBeUnsummoned(Inputs(2.0f, 1.0f, 8.0f, true)));
    CHECK(!IsPetAboutToBeUnsummoned(Inputs(70.0f, 70.0f, 0.0f, true)));
    CHECK(IsPetAboutToBeUnsummoned(Inputs(-0.5f, 3.2f, 205.5f, true)));
    CHECK(IsPetAboutToBeUnsummoned(Inputs(101.0f, 0.0f, 0.0f, true)));
}

int main()
{
    TestPetBesideOwnerStays();
    TestPetOnTheRoofIsPulled();
    TestHeightDifference();
    TestDistance();
    TestNoLineOfSight();
    TestCheckedOncePerPet();
    TestUnsummonGuard();
    std::printf("test_pet_spawn_policy: %d checks passed\n", checks);
    return 0;
}
