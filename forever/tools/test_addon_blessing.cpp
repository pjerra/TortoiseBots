// Standalone test of the `blessing` addon verb's core-free half (forever/src/host/TbAddonBlessingPure.h):
// argument parsing, class/player/status validation, TBM:BLESSING formatting, and ACK detail.

#include "forever/src/host/TbAddonBlessingPure.h"

#include <array>
#include <cassert>
#include <iostream>
#include <string>

using namespace TortoiseBots::blessing;

int main()
{
    int checks = 0;
    auto ok = [&](bool cond)
    {
        assert(cond);
        ++checks;
    };

    // 1. Class order parsing
    {
        ParsedBlessingArgs const p = ParseBlessingArgs("  Aelindra  Warrior   MIGHT ");
        ok(p.valid);
        ok(!p.status);
        ok(!p.playerMode);
        ok(!p.scopeIsParty);
        ok(p.scope == "aelindra");
        ok(p.classIndex == 0);
        ok(p.blessingWord == "might");
        ok(FormatBlessingAckDetail(p, "") == "warrior=might");
    }

    // 2. Party/raid scope + auto
    {
        ParsedBlessingArgs const p = ParseBlessingArgs("PARTY Mage auto");
        ok(p.valid);
        ok(!p.status);
        ok(!p.playerMode);
        ok(p.scopeIsParty);
        ok(p.scope == "party");
        ok(p.classIndex == 6);
        ok(p.blessingWord == "auto");
        ok(FormatBlessingAckDetail(p, "") == "mage=auto");
    }

    // 3. Player mode preserves targetName case while lowercasing blessingWord
    {
        ParsedBlessingArgs const p = ParseBlessingArgs("raid player Legolas Wisdom");
        ok(p.valid);
        ok(!p.status);
        ok(p.playerMode);
        ok(p.scopeIsParty);
        ok(p.scope == "raid");
        ok(p.targetName == "Legolas");
        ok(p.blessingWord == "wisdom");
        ok(FormatBlessingAckDetail(p, "Legolas") == "player:Legolas=wisdom");
    }

    // 4. Status mode
    {
        ParsedBlessingArgs const p = ParseBlessingArgs("party status");
        ok(p.valid);
        ok(p.status);
        ok(p.scopeIsParty);
        ok(std::string(BlessingIntent(p)) == "blessing status");
    }

    // 5. Invalid inputs
    {
        ok(!ParseBlessingArgs("").valid);
        ok(!ParseBlessingArgs("party").valid);
        ok(!ParseBlessingArgs("party unknown might").valid);
        ok(!ParseBlessingArgs("party warrior invalid").valid);
        ok(!ParseBlessingArgs("party warrior might extra").valid);
        ok(!ParseBlessingArgs("party player").valid);
        ok(!ParseBlessingArgs("party player Bob invalid").valid);
    }

    // 6. TBM:BLESSING formatting
    {
        std::array<std::string, ai::kBlessingOrderClassCount> classes{};
        classes[0] = "might";
        classes[6] = "wisdom";
        classes[1] = "bogus"; // invalid/unset falls back to auto
        std::array<bool, ai::kBlessingOrderBlessingCount> known{};
        known[0] = true; // might
        known[1] = true; // wisdom
        known[2] = true; // kings
        std::string const line = FormatBlessingLine("Aelindra", classes, known, "Legolas:wisdom+Aragorn:kings");
        ok(line == "TBM:BLESSING|Aelindra|warrior=might,paladin=auto,hunter=auto,rogue=auto,priest=auto,shaman=auto,mage=wisdom,warlock=auto,druid=auto,known=might+wisdom+kings,players=Legolas:wisdom+Aragorn:kings");
    }

    // 7. TBM:BLESSING without players
    {
        std::array<std::string, ai::kBlessingOrderClassCount> classes{};
        std::array<bool, ai::kBlessingOrderBlessingCount> known{};
        std::string const line = FormatBlessingLine("Pal", classes, known, "");
        ok(line == "TBM:BLESSING|Pal|warrior=auto,paladin=auto,hunter=auto,rogue=auto,priest=auto,shaman=auto,mage=auto,warlock=auto,druid=auto,known=");
    }

    std::cout << "test_addon_blessing: " << checks << " checks passed\n";
    return 0;
}
