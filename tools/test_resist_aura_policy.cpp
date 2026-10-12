#include "../ai/playerbot/ResistAuraPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsElectedResistAuraPaladin;
using ai::IsFireAuraBoss;
using ai::IsShadowAuraBoss;
using ai::ResistAuraCandidate;
using ai::ShouldRestoreSpecAura;
using ai::ShouldSwapResistAura;
using ai::WantedResistAuraAction;

int main()
{
    std::cout << "Starting TortoiseBots resist-aura policy tests...\n";

    // Fire bosses: MC fire pack + BWL drakes/Vael/Broodlord/Razorgore.
    CHECK(IsFireAuraBoss(11982));
    CHECK(IsFireAuraBoss(12057));
    CHECK(IsFireAuraBoss(12056));
    CHECK(IsFireAuraBoss(12098));
    CHECK(IsFireAuraBoss(11988));
    CHECK(IsFireAuraBoss(11502));
    CHECK(IsFireAuraBoss(12435));
    CHECK(IsFireAuraBoss(13020));
    CHECK(IsFireAuraBoss(12017));
    CHECK(IsFireAuraBoss(11983));
    CHECK(IsFireAuraBoss(11981));
    CHECK(IsFireAuraBoss(14020)); // Chromaggus
    CHECK(IsFireAuraBoss(10184)); // Onyxia
    CHECK(!IsFireAuraBoss(12118));
    CHECK(!IsFireAuraBoss(14601));
    CHECK(!IsFireAuraBoss(11583));
    std::cout << "  [PASS] fire boss list\n";

    // Shadow bosses: Lucifron, Gehennas, Majordomo.
    CHECK(IsShadowAuraBoss(12118));
    CHECK(IsShadowAuraBoss(12259));
    CHECK(IsShadowAuraBoss(12018));
    CHECK(IsShadowAuraBoss(14601)); // Ebonroc
    CHECK(IsShadowAuraBoss(11583)); // Nefarian
    CHECK(!IsShadowAuraBoss(11982));
    CHECK(!IsShadowAuraBoss(10184));
    CHECK(!IsShadowAuraBoss(14020));
    std::cout << "  [PASS] shadow boss list\n";

    // Swap only when the wanted aura is missing.
    CHECK(ShouldSwapResistAura(true, false, false, false));
    CHECK(!ShouldSwapResistAura(true, false, true, false));
    CHECK(ShouldSwapResistAura(false, true, false, false));
    CHECK(!ShouldSwapResistAura(false, true, false, true));
    CHECK(!ShouldSwapResistAura(false, false, false, false));
    std::cout << "  [PASS] swap-when-missing rule\n";

    // Action routing: fire wins on conflict (a boss is never both).
    CHECK(WantedResistAuraAction(true, false) == "fire resistance aura");
    CHECK(WantedResistAuraAction(false, true) == "shadow resistance aura");
    CHECK(WantedResistAuraAction(false, false).empty());
    std::cout << "  [PASS] action routing\n";

    // Election: the first eligible paladin bot swaps.
    {
        ResistAuraCandidate ready;
        ready.isBot = ready.alive = ready.sameMap = ready.inCombat = ready.knowsAura = true;
        ResistAuraCandidate self = ready;
        self.self = true;

        CHECK(IsElectedResistAuraPaladin({ self }));
        CHECK(!IsElectedResistAuraPaladin({ ready, self }));
        CHECK(IsElectedResistAuraPaladin({ self, ready }));
        CHECK(!IsElectedResistAuraPaladin({}));

        ResistAuraCandidate human = ready;
        human.isBot = false;
        CHECK(IsElectedResistAuraPaladin({ human, self }));

        ResistAuraCandidate elsewhere = ready;
        elsewhere.sameMap = false;
        CHECK(IsElectedResistAuraPaladin({ elsewhere, self }));

        ResistAuraCandidate dead = ready;
        dead.alive = false;
        CHECK(IsElectedResistAuraPaladin({ dead, self }));

        ResistAuraCandidate idle = ready;
        idle.inCombat = false;
        CHECK(IsElectedResistAuraPaladin({ idle, self }));

        ResistAuraCandidate untrained = ready;
        untrained.knowsAura = false;
        CHECK(IsElectedResistAuraPaladin({ untrained, self }));

        ResistAuraCandidate ordered = ready;
        ordered.manualAura = true;
        CHECK(IsElectedResistAuraPaladin({ ordered, self }));
    }
    std::cout << "  [PASS] election skips humans, other maps, dead, idle, untrained, ordered\n";

    // Restore after the fight: own swap, alive, out of combat.
    CHECK(ShouldRestoreSpecAura(true, true, false));
    CHECK(!ShouldRestoreSpecAura(true, true, true));
    CHECK(!ShouldRestoreSpecAura(true, false, false));
    CHECK(!ShouldRestoreSpecAura(false, true, false));
    std::cout << "  [PASS] fight-end restore\n";

    std::cout << "All resist-aura policy tests passed.\n";
    return 0;
}
