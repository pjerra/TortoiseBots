#include "../ai/playerbot/HealReachPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::HealReachWanted;
using ai::HealerCoversAlly;
using ai::HealerFitToCover;
using ai::HealReachLimitFor;
using ai::DropCoveredCandidate;
using ai::CoverApplies;

int main()
{
    std::cout << "Starting TortoiseBots heal reach policy tests...\n";

    // Out of combat: the full-health tank the value always names does not pull
    // the healer away from its leader (Uldaman, full-health party).
    {
        CHECK(!HealReachWanted(false, 100.0f, 90.0f));
        CHECK(!HealReachWanted(false, 90.0f, 90.0f));
        std::cout << "  [PASS] out of combat: no reach to a member at or above almost-full health\n";
    }

    // Out of combat, a member who really is hurt after a fight still gets the walk.
    {
        CHECK(HealReachWanted(false, 89.0f, 90.0f));
        CHECK(HealReachWanted(false, 20.0f, 90.0f));
        std::cout << "  [PASS] out of combat: reach to a hurt member\n";
    }

    // In combat nothing changes: the healer keeps within reach of the tank
    // even before the first hit.
    {
        CHECK(HealReachWanted(true, 100.0f, 90.0f));
        CHECK(HealReachWanted(true, 40.0f, 90.0f));
        std::cout << "  [PASS] in combat: unchanged\n";
    }

    // Issue #904: in combat, another healer that covers the ally means no walk.
    {
        CHECK(!HealReachWanted(true, 100.0f, 90.0f, true));
        CHECK(!HealReachWanted(true, 40.0f, 90.0f, true));
        CHECK(HealReachWanted(true, 40.0f, 90.0f, false));
        std::cout << "  [PASS] in combat: no reach when another healer covers the ally\n";
    }

    // Out of combat the cover flag changes nothing.
    {
        CHECK(HealReachWanted(false, 40.0f, 90.0f, true));
        CHECK(!HealReachWanted(false, 100.0f, 90.0f, true));
        std::cout << "  [PASS] out of combat: cover flag ignored\n";
    }

    // What counts as cover: a living healer on the map, in range and in sight.
    {
        auto yes = [] { return true; };
        auto no = [] { return false; };
        CHECK(HealerCoversAlly(true, true, 30.0f, 38.0f, true, yes, yes, yes));
        CHECK(HealerCoversAlly(true, true, 38.0f, 38.0f, true, yes, yes, yes));
        CHECK(!HealerCoversAlly(true, true, 38.5f, 38.0f, true, yes, yes, yes));  // too far
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, true, yes, yes, no));   // no line of sight
        CHECK(!HealerCoversAlly(false, true, 30.0f, 38.0f, true, yes, yes, yes)); // dead
        CHECK(!HealerCoversAlly(true, false, 30.0f, 38.0f, true, yes, yes, yes)); // other map
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, true, yes, no, yes));   // not a healer
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, false, yes, yes, yes)); // class cannot heal
        std::cout << "  [PASS] cover: living healer on the map, in range and sight\n";
    }

    // Raid cost: the talent walk and the line-of-sight ray run last, and only
    // when everything cheaper passed.
    {
        int fitCalls = 0, roleCalls = 0, sightCalls = 0;
        auto fit = [&] { ++fitCalls; return true; };
        auto role = [&] { ++roleCalls; return true; };
        auto sight = [&] { ++sightCalls; return true; };
        CHECK(!HealerCoversAlly(true, true, 60.0f, 38.0f, true, fit, role, sight));  // out of range
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, false, fit, role, sight)); // wrong class
        CHECK(!HealerCoversAlly(false, true, 30.0f, 38.0f, true, fit, role, sight)); // dead
        CHECK(fitCalls == 0 && roleCalls == 0 && sightCalls == 0);
        auto notHealer = [&] { ++roleCalls; return false; };
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, true, fit, notHealer, sight));
        CHECK(fitCalls == 1 && roleCalls == 1 && sightCalls == 0);
        CHECK(HealerCoversAlly(true, true, 30.0f, 38.0f, true, fit, role, sight));
        CHECK(fitCalls == 2 && roleCalls == 2 && sightCalls == 1);
        std::cout << "  [PASS] cover: cheap tests first, role and sight lazy\n";
    }

    // A healer that cannot act does not cover: crowd-controlled, silenced or
    // pacified, or too low on mana.
    {
        auto yes = [] { return true; };
        CHECK(HealerFitToCover(false, false, 100.0f));
        CHECK(HealerFitToCover(false, false, 20.0f));
        CHECK(!HealerFitToCover(false, false, 19.9f));  // out of mana
        CHECK(!HealerFitToCover(false, false, 0.0f));
        CHECK(!HealerFitToCover(true, false, 100.0f));  // stunned, feared, confused, charmed
        CHECK(!HealerFitToCover(false, true, 100.0f));  // silenced or pacified
        auto unfit = [] { return false; };
        CHECK(!HealerCoversAlly(true, true, 30.0f, 38.0f, true, unfit, yes, yes));
        std::cout << "  [PASS] cover: control, silence and mana\n";
    }

    // Battleground heal range differs from the normal one.
    {
        CHECK(HealReachLimitFor(false, 38.0f, 25.0f, 1.0f) == 37.0f);
        CHECK(HealReachLimitFor(true, 38.0f, 25.0f, 1.0f) == 24.0f);
        std::cout << "  [PASS] reach limit: battleground range\n";
    }

    // Target pick: a covered, out-of-reach ally is dropped, except at critical health.
    {
        int reachCalls = 0, coverCalls = 0;
        auto out = [&] { ++reachCalls; return true; };
        auto in = [&] { ++reachCalls; return false; };
        auto cov = [&] { ++coverCalls; return true; };
        auto unc = [&] { ++coverCalls; return false; };
        CHECK(DropCoveredCandidate(60.0f, 20.0f, out, cov));
        CHECK(!DropCoveredCandidate(60.0f, 20.0f, in, cov));   // reachable: keep
        CHECK(!DropCoveredCandidate(60.0f, 20.0f, out, unc));  // nobody covers: keep
        CHECK(reachCalls == 3 && coverCalls == 2);
        reachCalls = coverCalls = 0;
        CHECK(!DropCoveredCandidate(20.0f, 20.0f, out, cov));  // critical: always kept
        CHECK(!DropCoveredCandidate(5.0f, 20.0f, out, cov));
        CHECK(reachCalls == 0 && coverCalls == 0);
        CHECK(DropCoveredCandidate(21.0f, 20.0f, out, cov));
        CHECK(CoverApplies(21.0f, 20.0f));
        CHECK(!CoverApplies(20.0f, 20.0f)); // critical: cover ignored, walk and pick allowed
        CHECK(!CoverApplies(1.0f, 20.0f));
        std::cout << "  [PASS] pick: covered ally dropped, never at critical health\n";
    }

    std::cout << "All heal reach policy tests passed.\n";
    return 0;
}
