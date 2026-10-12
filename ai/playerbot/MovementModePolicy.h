#pragma once

#include <cstring>
#include <string>

namespace ai
{
    // Companion movement-mode rules. Pure decisions, no core includes, so
    // tools/test_movement_mode_policy.cpp can test them standalone.

    // The movement modes the owner chooses between. A bot that has none of
    // them in its non-combat and reaction engines does not move out of
    // combat at all: it stands where it is until someone whispers "follow".
    constexpr char const* kMovementModeStrategies[] = {
        "follow", "stay", "wander", "guard", "free", "passive"
    };

    inline bool IsMovementModeStrategy(std::string const& name)
    {
        for (char const* mode : kMovementModeStrategies)
        {
            if (name == mode)
                return true;
        }
        return false;
    }

    // Which engines a stored strategy snapshot may clear and replay. Before
    // Save wrote its rows in one transaction, they went out as loose async
    // statements over several database workers, so a stored snapshot can
    // lack a row (the delete ran after that insert), and a rebuild right
    // after a Save (a tank hire saves and then joins the party at once) could
    // read the combat row before the non-combat row existed. Only an engine
    // whose own row was read is replaced; any other engine keeps the
    // defaults it was just rebuilt with (mod-playerbots does the same).
    struct StoredStrategyRows
    {
        bool combat = false;
        bool nonCombat = false;
        bool dead = false;
        bool reaction = false;

        void Note(char const* key)
        {
            if (!key)
                return;
            if (std::strcmp(key, "co") == 0)
                combat = true;
            else if (std::strcmp(key, "nc") == 0)
                nonCombat = true;
            else if (std::strcmp(key, "dead") == 0)
                dead = true;
            else if (std::strcmp(key, "react") == 0)
                reaction = true;
        }

        bool Any() const { return combat || nonCombat || dead || reaction; }
    };

    // A rebuild must leave a real player's bot with a movement mode. A stored
    // snapshot that was saved while the bot had none (a stay that a tactical
    // order broke) would otherwise reload that state after every rebuild,
    // and a master's far teleport (.tele, zoning into or out of an instance)
    // can trigger one. Battlegrounds run their own movement kit.
    inline bool NeedsDefaultMovementMode(bool inBattleGround, bool hasActivePlayerMaster, bool hasMovementMode)
    {
        return !inBattleGround && hasActivePlayerMaster && !hasMovementMode;
    }
}
