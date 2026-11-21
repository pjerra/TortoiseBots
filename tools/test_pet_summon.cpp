// Standalone test of the tame path's order (forever/src/host/TbPetSummonPure.h; slice 11 review fix: a map insert that does not take must not leave a pet):
//   g++ -std=c++17 -Wall -Wextra tools/test_pet_summon.cpp -o /tmp/test_pet_summon && /tmp/test_pet_summon
//
// GiveHunterPet runs these steps in this order. The fakes below are the bot's pet state and the five calls the host makes, each of which can refuse: the map
// can decline the pet and SetMinion can leave a pet the bot does not read back. What the flow must do then is undo the active slot InitTamedPet took and the
// object itself, attach nothing to the bot, and write neither the pet row nor characters.summonedPetNumber, so the next check can make a pet again. Drop the
// insert answer again (the order the core's own tame effect uses) and the pet is attached, saved and numbered even though the map refused it:
// TestInsertFailureLeavesNothing fails on the named pet, the taken slot and the save.
#include "../forever/src/host/TbPetSummonPure.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace p11 = TortoiseBots::p11;

static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } \
} while (0)

// The bot and its pet as CreateTamedPetFrom leaves them: the pet object exists and an active slot is taken. The flow may change the rest only by keeping it.
struct World
{
    bool petObject = true;     // the pet exists (the host deletes or removes it)
    int stableSlot = 0;        // the active slot InitTamedPet took (-1: none)
    bool petGuid = false;      // the bot names a pet
    int saves = 0;             // pet rows written (and summons numbered)
    unsigned summonedNumber = 0;
    std::vector<std::string> calls;
};

struct Steps
{
    World& w;
    int insertFails;           // the map refuses this many inserts
    bool attachDrops;          // SetMinion takes the pet but the read-back does not show it

    bool Insert()
    {
        w.calls.push_back("insert");
        if (insertFails > 0)
        {
            --insertFails;
            return false;
        }
        return true;
    }
    void Attach()
    {
        w.calls.push_back("attach");
        if (!attachDrops)
            w.petGuid = true;
    }
    bool Out()
    {
        w.calls.push_back("out");
        return w.petGuid;
    }
    void Keep()
    {
        w.calls.push_back("keep");
        ++w.saves;
        w.summonedNumber = 4242;
    }
    void Discard(bool attached)
    {
        w.calls.push_back(attached ? "discard-attached" : "discard-insert");
        w.petGuid = false;
        w.stableSlot = -1;
        w.petObject = false;
    }
};

// The insert the map refuses: the pet is never attachable, so nothing is kept and the next attempt starts where the first did.
static void TestInsertFailureLeavesNothing()
{
    World w;
    Steps steps{ w, 1, false };
    CHECK(p11::SummonTamedPet(steps) == p11::PetSummonOutcome::NotInserted);
    CHECK((w.calls == std::vector<std::string>{ "insert", "discard-insert" }));
    CHECK(!w.petGuid);
    CHECK(w.stableSlot == -1);
    CHECK(w.saves == 0);
    CHECK(w.summonedNumber == 0);
    CHECK(!w.petObject);

    // The retry: CreateTamedPetFrom takes the now-free slot again, and the same steps keep the pet.
    w.petObject = true;
    w.stableSlot = 0;
    Steps retry{ w, 0, false };
    CHECK(p11::SummonTamedPet(retry) == p11::PetSummonOutcome::Out);
    CHECK((w.calls == std::vector<std::string>{ "insert", "discard-insert", "insert", "attach", "out", "keep" }));
    CHECK(w.petGuid);
    CHECK(w.stableSlot == 0);
    CHECK(w.saves == 1);
    CHECK(w.summonedNumber == 4242);
}

// The attach happens but the bot does not read the pet back: the attachment and the slot are undone before any write.
static void TestReadBackFailureLeavesNothing()
{
    World w;
    Steps steps{ w, 0, true };
    CHECK(p11::SummonTamedPet(steps) == p11::PetSummonOutcome::NotAttached);
    CHECK((w.calls == std::vector<std::string>{ "insert", "attach", "out", "discard-attached" }));
    CHECK(!w.petGuid);
    CHECK(w.stableSlot == -1);
    CHECK(w.saves == 0);
    CHECK(w.summonedNumber == 0);
    CHECK(!w.petObject);

    w.petObject = true;
    w.stableSlot = 0;
    Steps retry{ w, 0, false };
    CHECK(p11::SummonTamedPet(retry) == p11::PetSummonOutcome::Out);
    CHECK(w.saves == 1);
    CHECK(w.summonedNumber == 4242);
}

// The pet is out: every step runs once, in the core's order, and one row is written.
static void TestOutKeepsOnce()
{
    World w;
    Steps steps{ w, 0, false };
    CHECK(p11::SummonTamedPet(steps) == p11::PetSummonOutcome::Out);
    CHECK((w.calls == std::vector<std::string>{ "insert", "attach", "out", "keep" }));
    CHECK(w.petGuid);
    CHECK(w.stableSlot == 0);
    CHECK(w.saves == 1);
    CHECK(w.summonedNumber == 4242);
}

int main()
{
    TestInsertFailureLeavesNothing();
    TestReadBackFailureLeavesNothing();
    TestOutKeepsOnce();
    std::printf("OK: pet summon (%d checks)\n", g_checks);
    return 0;
}
