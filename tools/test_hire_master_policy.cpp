#include "../runtime/HireMasterPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::HireAllowsMasterAdoption;
using TortoiseBots::HireRestoresHirer;

int main()
{
    std::cout << "Starting TortoiseBots hire-master tests (issue #1014)...\n";

    // A bot that is not a hire adopts as before, bot or human candidate.
    CHECK(HireAllowsMasterAdoption(false, false, false));
    CHECK(HireAllowsMasterAdoption(false, false, true));
    CHECK(HireAllowsMasterAdoption(false, true, false));
    CHECK(HireAllowsMasterAdoption(false, true, true));
    std::cout << "  [PASS] non-hire adoption unchanged\n";

    // A hire whose hirer is between maps keeps the binding: no adoption at all.
    CHECK(!HireAllowsMasterAdoption(true, true, false));
    CHECK(!HireAllowsMasterAdoption(true, true, true));
    std::cout << "  [PASS] hirer between maps keeps the binding\n";

    // A hire never takes a bot (the lead bot) as master; a human still may.
    CHECK(!HireAllowsMasterAdoption(true, false, false));
    CHECK(HireAllowsMasterAdoption(true, false, true));
    std::cout << "  [PASS] hire never adopts a bot\n";

    // The hirer back in the world is the hire's live master again, but only
    // while the binding still names the hirer and no pointer is held.
    CHECK(HireRestoresHirer(true, true, false, true));
    CHECK(!HireRestoresHirer(true, true, false, false));
    CHECK(!HireRestoresHirer(true, true, true, true));
    CHECK(!HireRestoresHirer(true, false, false, true));
    CHECK(!HireRestoresHirer(false, true, false, true));
    std::cout << "  [PASS] hirer back in world is restored\n";

    std::cout << "All TortoiseBots hire-master tests passed.\n";
    return 0;
}
