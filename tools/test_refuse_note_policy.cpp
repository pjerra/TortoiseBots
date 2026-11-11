// Standalone check for ai/playerbot/RefuseNotePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_refuse_note_policy.cpp -o /tmp/test_refuse_note_policy
//   /tmp/test_refuse_note_policy
#include "../ai/playerbot/RefuseNotePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

static const char* g_case = "(unnamed)";
#define CASE(name) (g_case = (name))

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << " in " << g_case << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots refuse note policy tests...\n";

    CASE("OutcomeFailCode_VetoBeatsCastCheck");
    CHECK(OutcomeFailCode(5, RefuseNote::Threat) == kRefuseNoteBase + 1);
    CHECK(OutcomeFailCode(5, RefuseNote::None) == 5);
    CHECK(OutcomeFailCode(0, RefuseNote::Attacked) == kRefuseNoteBase + 3);

    CASE("VetoNoteFor");
    CHECK(VetoNoteFor("threat") == RefuseNote::Threat);
    CHECK(VetoNoteFor("onyxia whelp hold") == RefuseNote::Veto);

    CASE("RefuseNoteWord");
    CHECK(std::string(RefuseNoteWord(kRefuseNoteBase + 1)) == "threat");
    CHECK(std::string(RefuseNoteWord(kRefuseNoteBase + 2)) == "veto");
    CHECK(std::string(RefuseNoteWord(kRefuseNoteBase + 3)) == "attacked");
    CHECK(RefuseNoteWord(0) == nullptr);
    CHECK(RefuseNoteWord(5) == nullptr);
    CHECK(RefuseNoteWord(kRefuseNoteBase) == nullptr);

    std::cout << "refuse note policy: all tests passed\n";
    return 0;
}
