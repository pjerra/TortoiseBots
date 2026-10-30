#include "../runtime/policy/hire/LftPartyAcceptPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::LftPartyBotMayAccept;
using TortoiseBots::LftPartyOfferMayAccept;
using TortoiseBots::LftQueuedTogether;

int main()
{
    std::cout << "Starting TortoiseBots LFT party accept tests...\n";

    CHECK(LftPartyBotMayAccept(false, false));   // alt on the owner's account
    std::cout << "  [PASS] alt on the owner's account may accept\n";
    CHECK(!LftPartyBotMayAccept(true, false));   // pool bot that is not the human's: never
    std::cout << "  [PASS] unrelated pool bot never accepts\n";
    CHECK(LftPartyBotMayAccept(true, true));     // hire (#1284): random record, hired
    std::cout << "  [PASS] hired mercenary may accept\n";
    CHECK(LftPartyBotMayAccept(false, true));
    std::cout << "  [PASS] non-pool hired record may accept\n";

    // Final decision per anchor: (randomPool, hired, anchorIsHireMaster, sameOffer).
    CHECK(!LftPartyOfferMayAccept(true, true, false, true));   // hire in a stranger's offer
    std::cout << "  [PASS] hire refuses a stranger's offer\n";
    CHECK(!LftPartyOfferMayAccept(true, true, true, false));   // hirer in a different offer
    std::cout << "  [PASS] hire refuses when its hirer is in another offer\n";
    CHECK(LftPartyOfferMayAccept(true, true, true, true));     // the hirer's own offer
    std::cout << "  [PASS] hire accepts its hirer's own offer\n";
    CHECK(LftPartyOfferMayAccept(false, false, false, false)); // alt: any anchor, as before
    std::cout << "  [PASS] alt keeps the old anchor rule\n";
    CHECK(!LftPartyOfferMayAccept(true, false, true, true));   // unhired pool bot
    std::cout << "  [PASS] unhired pool bot never accepts\n";

    // Same queue block (=> same offer): same group, both listed, same joinTime and instances.
    std::vector<std::string> const rfc = {"rfc"};
    std::vector<std::string> const dm = {"dm"};
    CHECK(LftQueuedTogether(true, true, 100, 100, rfc, rfc));
    std::cout << "  [PASS] one queue block counts as the same offer\n";
    CHECK(!LftQueuedTogether(false, true, 100, 100, rfc, rfc));
    std::cout << "  [PASS] different groups are not the same offer\n";
    CHECK(!LftQueuedTogether(true, false, 100, 100, rfc, rfc));
    std::cout << "  [PASS] a player missing from the queue is not the same offer\n";
    // One rolecheck can straddle a second boundary (EnqueuePlayer reads the game time per member): 1 s apart is one block.
    CHECK(LftQueuedTogether(true, true, 100, 101, rfc, rfc));
    CHECK(LftQueuedTogether(true, true, 101, 100, rfc, rfc));
    // More than the slack apart is two queue blocks.
    CHECK(!LftQueuedTogether(true, true, 100, 102, rfc, rfc));
    std::cout << "  [PASS] different join times are not the same offer\n";
    CHECK(!LftQueuedTogether(true, true, 100, 100, rfc, dm));
    std::cout << "  [PASS] different instance lists are not the same offer\n";

    std::cout << "TortoiseBots LFT party accept tests passed.\n";
    return 0;
}
