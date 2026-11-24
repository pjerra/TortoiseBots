// Standalone test of the core-free half of the tactical orders (forever/src/host/TbTacticsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_tactics_pure.cpp -o /tmp/test_tactics_pure && /tmp/test_tactics_pure
// M1: the engage seconds and the stance word. A mutation that accepts 61 s, refuses `turn`, or maps `turn` to `near` fails here.
#include "forever/src/host/TbTacticsPure.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace t = TortoiseBots::tactics;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestEngageSeconds()
{
    unsigned seconds = 0;
    CHECK(t::kEngageDefaultSeconds == 4);
    CHECK(t::ParseEngageSeconds("", seconds) && seconds == t::kEngageDefaultSeconds);
    CHECK(t::ParseEngageSeconds("0", seconds) && seconds == 0);
    CHECK(t::ParseEngageSeconds("5", seconds) && seconds == 5);
    CHECK(t::ParseEngageSeconds("60", seconds) && seconds == 60);
    CHECK(!t::ParseEngageSeconds("61", seconds));
    CHECK(!t::ParseEngageSeconds("-1", seconds));
    CHECK(!t::ParseEngageSeconds("5s", seconds));
    CHECK(!t::ParseEngageSeconds("+5", seconds));
    CHECK(!t::ParseEngageSeconds(" ", seconds));
    CHECK(!t::ParseEngageSeconds("999999999999999999999", seconds));
}

static void TestStance()
{
    std::string out;
    CHECK(t::NormaliseStance("turn", out) && out == "turnback");
    CHECK(t::NormaliseStance("turnback", out) && out == "turnback");
    CHECK(t::NormaliseStance("Behind", out) && out == "behind");
    CHECK(t::NormaliseStance("near", out) && out == "near");
    CHECK(t::NormaliseStance("tank", out) && out == "tank");
    CHECK(!t::NormaliseStance("left", out));
    CHECK(!t::NormaliseStance("", out));
    CHECK(!t::NormaliseStance("turn back", out));
}

static void TestVerbs()
{
    for (char const* verb : { "stop", "flee", "guard", "stance", "engage", "rebuff", "interrupt" })
        CHECK(t::IsTacticalVerb(verb));
    for (char const* verb : { "", "follow", "attack", "status", "pull" })
        CHECK(!t::IsTacticalVerb(verb));
}

static void TestInterrupt()
{
    using t::InterruptProbe;

    // Within one bot the probe list's own order does not matter: TB's action order decides (counterspell before kick).
    std::vector<InterruptProbe> probes = { { "Mmage", false, { "kick", "counterspell" } } };
    t::InterruptPick pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.action == "counterspell");

    // No bot named: the first able bot in party order wins, even when a later one has a "better" action.
    probes = { { "Mtank", false, {} }, { "Mmage", false, { "counterspell" } }, { "Mpriest", false, { "silence" } } };
    pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.index == 1 && pick.action == "counterspell");

    // A named bot is the only one probed: it cannot interrupt, so the answer is none even though another bot can.
    probes = { { "Mtank", true, {} }, { "Mmage", false, { "counterspell" } } };
    CHECK(!t::PickInterrupt(probes).found);

    // A named bot that can interrupt wins over an earlier able bot.
    probes = { { "Mmage", false, { "counterspell" } }, { "Mpriest", true, { "silence" } } };
    pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.index == 1 && pick.action == "silence");

    CHECK(!t::PickInterrupt(std::vector<InterruptProbe>()).found);

    // Review fix: a bot that can land the interrupt from where it stands wins over an earlier bot that would have to walk.
    probes = { { "Mrogue", false, { "kick" }, {} }, { "Mmage", false, { "counterspell" }, { "counterspell" } } };
    pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.index == 1 && pick.action == "counterspell" && pick.inRange);
    // ... and with nobody in range the first able bot still gets the walk-and-queue order.
    probes = { { "Mrogue", false, { "kick" }, {} }, { "Mmage", false, { "counterspell" }, {} } };
    pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.index == 0 && pick.action == "kick" && !pick.inRange);
    // A named bot out of range is still the only one considered.
    probes = { { "Mrogue", true, { "kick" }, {} }, { "Mmage", false, { "counterspell" }, { "counterspell" } } };
    pick = t::PickInterrupt(probes);
    CHECK(pick.found && pick.index == 0 && !pick.inRange);

    // TB's list without the pet-only `spell lock` and the unported `repentance`.
    std::string order;
    for (char const* action : t::kInterruptActions)
        order += std::string(action) + "|";
    CHECK(order == "counterspell|silence|kick|pummel|shield bash|bash|hammer of justice|earth shock|death coil|");
}

static void TestInterruptTick()
{
    using Tick = t::InterruptTick;
    unsigned const window = t::kInterruptWindowMs;
    // The order is ok only when the spell cast was requested, whatever the target does after.
    CHECK(t::NextInterruptTick(true, true, true, true, 0) == Tick::Done);
    CHECK(t::NextInterruptTick(true, true, false, true, window + 1) == Tick::Done);
    CHECK(std::string(t::InterruptTickResult(Tick::Done)) == "ok");
    // The live failure: an accepted order that never cast reported ok. Not cast yet, target still casting: keep trying, not ok.
    CHECK(t::NextInterruptTick(true, true, true, false, 0) == Tick::Wait);
    CHECK(t::NextInterruptTick(true, true, true, false, window - 1) == Tick::Wait);
    CHECK(std::string(t::InterruptTickResult(Tick::Wait)) == "pending:interrupt");
    // Inside the window only; past it the order fails instead of staying pending or ok.
    CHECK(t::NextInterruptTick(true, true, true, false, window) == Tick::TimedOut);
    CHECK(std::string(t::InterruptTickResult(Tick::TimedOut)) == "fail:interrupt");
    // The target's own state ends the order first.
    CHECK(t::NextInterruptTick(false, false, false, false, 0) == Tick::NoTarget);
    CHECK(t::NextInterruptTick(true, false, false, false, 0) == Tick::NoTarget);
    CHECK(std::string(t::InterruptTickResult(Tick::NoTarget)) == "fail:no-target");
    CHECK(t::NextInterruptTick(true, true, false, false, 0) == Tick::NotCasting);
    CHECK(std::string(t::InterruptTickResult(Tick::NotCasting)) == "fail:not-casting");
    // The window is shorter than a Frostbolt's 3 s cast plus the GCD slack the live caster showed (2.9 s), and long enough for two engine ticks.
    CHECK(window >= 1000 && window <= 4000);
}

static void TestToggles()
{
    // ParseToggle: empty = flip; on/off/status case-insensitive; anything else refused.
    bool ok = false;
    CHECK(t::ParseToggle("", ok) == t::ToggleOp::Flip && ok);
    CHECK(t::ParseToggle("on", ok) == t::ToggleOp::On && ok);
    CHECK(t::ParseToggle("OFF", ok) == t::ToggleOp::Off && ok);
    CHECK(t::ParseToggle("status", ok) == t::ToggleOp::Status && ok);
    CHECK(t::ParseToggle("maybe", ok) == t::ToggleOp::Flip && !ok);
    CHECK(t::ParseToggle("flip", ok) == t::ToggleOp::Flip && !ok);

    // ToggleWanted: the wanted state after the op against the current state.
    CHECK(!t::ToggleWanted(t::ToggleOp::Flip, true));
    CHECK(t::ToggleWanted(t::ToggleOp::Flip, false));
    CHECK(t::ToggleWanted(t::ToggleOp::On, true));
    CHECK(t::ToggleWanted(t::ToggleOp::On, false));
    CHECK(!t::ToggleWanted(t::ToggleOp::Off, true));
    CHECK(!t::ToggleWanted(t::ToggleOp::Off, false));

    // AggregateState: one state when all agree, "mixed" when they do not.
    CHECK(t::AggregateState({ true, true }) == "on");
    CHECK(t::AggregateState({ false, false }) == "off");
    CHECK(t::AggregateState({ true, false }) == "mixed");
    CHECK(t::AggregateState({ false, true }) == "mixed");
    CHECK(t::AggregateState({ true, true, false }) == "mixed");
    CHECK(t::AggregateState({}) == "off");
}

// ---- M4: the whole-party cc pick (D12) ----
namespace DR = TortoiseBots::DungeonRun;
namespace perm = ai::CcPermissions;

static t::CcCandidate Cand(char const* name, bool kit, std::uint32_t known, bool inCombat = false, std::uint32_t denied = 0, bool alive = true)
{
    t::CcCandidate c;
    c.name = name;
    c.hasCcKit = kit;
    c.known = known;
    c.inCombat = inCombat;
    c.denied = denied;
    c.alive = alive;
    return c;
}

static void TestCcPick()
{
    using F = DR::CcFamily;
    std::uint32_t const poly = DR::CcBit(F::Polymorph), shackle = DR::CcBit(F::Shackle), sap = DR::CcBit(F::Sap);
    std::uint32_t const fear = DR::CcBit(F::Fear), trap = DR::CcBit(F::FreezingTrap), banish = DR::CcBit(F::Banish);

    // An undead target with a mage and a priest (shackle known): the priest, because shackle fits it and polymorph does not.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mmage", true, poly), Cand("Mpriest", true, shackle) };
        t::CcPick const pick = t::PickCcBot(cands, DR::kCreatureUndead, false);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Shackle && pick.familyToken == "shackleundead");
    }
    // A humanoid out of combat with a rogue (sap) and a mage: tier A, then the table order: the polymorph row comes before sap, so the mage,
    // whichever stands first in the party.
    {
        std::vector<t::CcCandidate> const rogueFirst = { Cand("Mrogue", true, sap), Cand("Mmage", true, poly) };
        t::CcPick pick = t::PickCcBot(rogueFirst, DR::kCreatureHumanoid, false);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Polymorph && pick.familyToken == "polymorph");
        std::vector<t::CcCandidate> const mageFirst = { Cand("Mmage", true, poly), Cand("Mrogue", true, sap) };
        pick = t::PickCcBot(mageFirst, DR::kCreatureHumanoid, false);
        CHECK(pick.found && pick.index == 0 && pick.family == F::Polymorph);
    }
    // The same family twice: the first bot in party order.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Ma", true, poly), Cand("Mb", true, poly) };
        CHECK(t::PickCcBot(cands, DR::kCreatureHumanoid, false).index == 0);
    }
    // An engaged target never gets Sap (prePull), and a bot in combat never saps either.
    {
        std::vector<t::CcCandidate> const rogueOnly = { Cand("Mrogue", true, sap) };
        t::CcPick pick = t::PickCcBot(rogueOnly, DR::kCreatureHumanoid, true);
        CHECK(!pick.found && pick.reasons == "Mrogue:in-combat");
        std::vector<t::CcCandidate> const busyRogue = { Cand("Mrogue", true, sap, true) };
        pick = t::PickCcBot(busyRogue, DR::kCreatureHumanoid, false);
        CHECK(!pick.found && pick.reasons == "Mrogue:in-combat");
        // ... but a rogue out of combat on a target out of combat is a candidate
        pick = t::PickCcBot(rogueOnly, DR::kCreatureHumanoid, false);
        CHECK(pick.found && pick.family == F::Sap);
        // with a mage along, the engaged target goes to the mage
        std::vector<t::CcCandidate> const both = { Cand("Mrogue", true, sap), Cand("Mmage", true, poly) };
        pick = t::PickCcBot(both, DR::kCreatureHumanoid, true);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Polymorph);
    }
    // A denied family is skipped (the owner's permissions: sheep for polymorph); the next bot or family wins, or the refusal names `denied`.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mmage", true, poly, false, perm::kSheep), Cand("Mrogue", true, sap) };
        t::CcPick pick = t::PickCcBot(cands, DR::kCreatureHumanoid, false);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Sap);
        std::vector<t::CcCandidate> const onlyMage = { Cand("Mmage", true, poly, false, perm::kSheep) };
        pick = t::PickCcBot(onlyMage, DR::kCreatureHumanoid, false);
        CHECK(!pick.found && pick.reasons == "Mmage:denied");
    }
    // Fear stays tier R: any tier A family beats it, and it needs the permission (denied by default for fear).
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mlock", true, fear | banish), Cand("Mmage", true, poly) };
        t::CcPick pick = t::PickCcBot(cands, DR::kCreatureDemon, false);
        CHECK(pick.found && pick.index == 0 && pick.family == F::Banish);       // banish is tier A and fits a demon; fear is R
        pick = t::PickCcBot(cands, DR::kCreatureHumanoid, false);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Polymorph);
        std::vector<t::CcCandidate> const fearOnly = { Cand("Mlock", true, fear, false, perm::kFear) };
        CHECK(!t::PickCcBot(fearOnly, DR::kCreatureHumanoid, false).found);
    }
    // Freezing Trap is never an automatic pick (autoPick false), whatever the target type.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mhunter", true, trap) };
        t::CcPick const pick = t::PickCcBot(cands, DR::kCreatureBeast, false);
        CHECK(!pick.found && pick.reasons == "Mhunter:no-fitting-spell");
    }
    // A named family restricts the pick to it.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mmage", true, poly), Cand("Mrogue", true, sap) };
        t::CcPick pick = t::PickCcBot(cands, DR::kCreatureHumanoid, false, F::Sap);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Sap);
        pick = t::PickCcBot(cands, DR::kCreatureUndead, false, F::Polymorph);
        CHECK(!pick.found);
    }
    // The refusal names every bot's reason: dead, no kit (#123), no fitting spell.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mtank", false, 0), Cand("Mmage", true, poly, false, 0, false), Cand("Mpriest", true, shackle), Cand("Mwar", false, 0) };
        t::CcPick const pick = t::PickCcBot(cands, DR::kCreatureHumanoid, false);
        CHECK(!pick.found);
        CHECK(pick.reasons == "Mtank:no-cc-kit;Mmage:dead;Mpriest:no-fitting-spell;Mwar:no-cc-kit");
    }
    // A kit with the strategy but no known family at all: no-fitting-spell.
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mmage", true, 0) };
        CHECK(t::PickCcBot(cands, DR::kCreatureHumanoid, false).reasons == "Mmage:no-fitting-spell");
    }
    // An unread creature type fits polymorph only (the run's own CcFits).
    {
        std::vector<t::CcCandidate> const cands = { Cand("Mpriest", true, shackle), Cand("Mmage", true, poly) };
        t::CcPick const pick = t::PickCcBot(cands, 0, false);
        CHECK(pick.found && pick.index == 1 && pick.family == F::Polymorph);
    }
    // The family token text: known tokens map to the family, `none` and unknown words to none.
    CHECK(t::CcFamilyFromToken("polymorph") == F::Polymorph);
    CHECK(t::CcFamilyFromToken("shackleundead") == F::Shackle);
    CHECK(t::CcFamilyFromToken("none") == F::None);
    CHECK(t::CcFamilyFromToken("dance") == F::None);
    CHECK(t::CcFamilyFromToken("") == F::None);
}

int main()
{
    TestEngageSeconds();
    TestStance();
    TestVerbs();
    TestInterrupt();
    TestInterruptTick();
    TestToggles();
    TestCcPick();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("%d checks passed\n", g_checks);
    return 0;
}
