#pragma once

// Pure policy for the resto shaman Spirit Link row: the 10-min link is a
// save for a low tank, not a pull buff, so it fires only at/below lowHealth.

namespace ai
{
    inline bool ShouldSpiritLink(bool linkUpInGroup, bool tankBuffMissing, bool inCombat,
                                 unsigned tankHealthPct, unsigned lowHealthPct)
    {
        return !linkUpInGroup && tankBuffMissing && inCombat && tankHealthPct <= lowHealthPct;
    }
}
