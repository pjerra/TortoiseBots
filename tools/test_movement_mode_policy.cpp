#include "../ai/playerbot/MovementModePolicy.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsMovementModeStrategy;
using ai::NeedsDefaultMovementMode;
using ai::StoredStrategyRows;

// True when a stored "+a,+b,..." strategy row holds a movement mode.
static bool RowHasMovementMode(std::string const& row)
{
    std::stringstream in(row);
    std::string entry;
    while (std::getline(in, entry, ','))
    {
        if (!entry.empty() && entry[0] == '+' && IsMovementModeStrategy(entry.substr(1)))
            return true;
    }
    return false;
}

int main()
{
    std::cout << "Starting TortoiseBots movement-mode policy tests...\n";

    // The six owner-controlled modes, and nothing else.
    for (char const* mode : { "follow", "stay", "wander", "guard", "free", "passive" })
        CHECK(IsMovementModeStrategy(mode));
    for (char const* other : { "follow jump", "grind", "rpg", "tank assist", "", "Follow" })
        CHECK(!IsMovementModeStrategy(other));
    std::cout << "  [PASS] movement modes recognised\n";

    // Stored non-combat rows read back from a live server (build 11): the
    // rogue that was never told to stay kept follow; the tank lost every
    // default but the snapshot baseline, the mage kept its kit minus any mode.
    CHECK(RowHasMovementMode("+boost,+buff,+collision,+default,+dps assist,+follow,+follow jump,+food,+group,+loot"));
    CHECK(!RowHasMovementMode("+chat,+delayed roll,+loot,+protection,+protection pve,+quest"));
    CHECK(!RowHasMovementMode("+boost,+buff,+collision,+default,+dps assist,+follow jump,+food,+frost,+group,+loot"));
    CHECK(RowHasMovementMode("+chat,+loot,+stay"));
    std::cout << "  [PASS] stored rows with and without a movement mode\n";

    // A half-written snapshot (Save queues its rows asynchronously; a rebuild
    // right after it reads them synchronously): the combat row is there, the
    // non-combat row not yet. Only the combat engine may be replaced.
    {
        StoredStrategyRows rows;
        for (char const* key : { "value", "value", "co" })
            rows.Note(key);
        CHECK(rows.Any());
        CHECK(rows.combat);
        CHECK(!rows.nonCombat);
        CHECK(!rows.dead);
        CHECK(!rows.reaction);
    }
    // A complete snapshot replaces all four engines.
    {
        StoredStrategyRows rows;
        for (char const* key : { "value", "co", "nc", "dead", "react" })
            rows.Note(key);
        CHECK(rows.combat && rows.nonCombat && rows.dead && rows.reaction);
    }
    // A value-only preset replaces none.
    {
        StoredStrategyRows rows;
        rows.Note("value");
        rows.Note(nullptr);
        CHECK(!rows.Any());
    }
    std::cout << "  [PASS] snapshot rows decide engine by engine\n";

    // A rebuild gives a player's bot its default mode only when it has none.
    CHECK(NeedsDefaultMovementMode(false, true, false));
    CHECK(!NeedsDefaultMovementMode(false, true, true));   // a stay the player placed survives
    CHECK(!NeedsDefaultMovementMode(false, false, false)); // pool bots keep their own rules
    CHECK(!NeedsDefaultMovementMode(true, true, false));   // battlegrounds run their own kit
    std::cout << "  [PASS] rebuild restores a missing movement mode\n";

    std::cout << "All TortoiseBots movement-mode policy tests passed.\n";
    return 0;
}
