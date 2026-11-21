// Standalone proof of the dispel-mana-worth rules (ai/playerbot/policy/heal/DispelTargetPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_dispel_mana_policy.cpp -o /tmp/test_dispel_mana_policy && /tmp/test_dispel_mana_policy
#include "ai/playerbot/policy/heal/DispelTargetPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    int passed = 0;

    // 1. Harmless debuff: dispelled at healthy mana, skipped when mana is low or about to expire.
    {
        ai::DispelWorthInput in;
        in.isFriendlyTarget = true;
        in.isDangerous = false;
        in.isSpreading = false;
        in.remainingDurationMs = 8000;
        in.minDurationMs = 2000;
        in.casterHasMana = true;
        in.casterManaPct = 60;
        in.minManaPct = 25;
        assert(ai::IsDispelWorthMana(in));

        in.casterManaPct = 20;
        assert(!ai::IsDispelWorthMana(in));

        in.casterManaPct = 60;
        in.remainingDurationMs = 1500;
        assert(!ai::IsDispelWorthMana(in));
        ++passed;
    }

    // 2. Dangerous CC (Polymorph/Fear/Stun/Silence/Root) and spreading auras still get dispelled at low mana.
    {
        ai::DispelDebuffInput poly;
        poly.mechanic = ai::kDispelMechanicPolymorph;
        assert(ai::IsDangerousDispelDebuff(poly));

        ai::DispelDebuffInput fear;
        fear.effects.push_back({ ai::kDispelAuraModFear, 0 });
        assert(ai::IsDangerousDispelDebuff(fear));

        ai::DispelDebuffInput stun;
        stun.allMechanicMask = (1u << (ai::kDispelMechanicStun - 1));
        assert(ai::IsDangerousDispelDebuff(stun));

        ai::DispelWorthInput in;
        in.isFriendlyTarget = true;
        in.isDangerous = true;
        in.remainingDurationMs = 1200;
        in.minDurationMs = 2000;
        in.casterHasMana = true;
        in.casterManaPct = 5;
        in.minManaPct = 25;
        assert(ai::IsDispelWorthMana(in));

        in.isDangerous = false;
        in.isSpreading = true;
        assert(ai::IsDispelWorthMana(in));
        ++passed;
    }

    // 3. Big DoT vs small DoT classification.
    {
        ai::DispelDebuffInput smallDot;
        smallDot.effects.push_back({ ai::kDispelAuraPeriodicDamage, 25 });
        assert(!ai::IsDangerousDispelDebuff(smallDot));

        ai::DispelDebuffInput bigDot;
        bigDot.effects.push_back({ ai::kDispelAuraPeriodicDamage, 200 });
        assert(ai::IsDangerousDispelDebuff(bigDot));

        ai::DispelDebuffInput pctDot;
        pctDot.effects.push_back({ ai::kDispelAuraPeriodicDamagePercent, 5 });
        assert(ai::IsDangerousDispelDebuff(pctDot));
        ++passed;
    }

    // 4. Threshold disabled (0) or non-mana caster (e.g. Stoneform) never blocks on mana; enemy offensive dispel does.
    {
        ai::DispelWorthInput in;
        in.isFriendlyTarget = true;
        in.isDangerous = false;
        in.remainingDurationMs = 6000;
        in.minDurationMs = 2000;
        in.casterHasMana = true;
        in.casterManaPct = 5;
        in.minManaPct = 0;
        assert(ai::IsDispelWorthMana(in));

        in.minManaPct = 25;
        in.casterHasMana = false;
        assert(ai::IsDispelWorthMana(in));

        in.casterHasMana = true;
        in.isFriendlyTarget = false;
        in.isDangerous = true; // Enemy buff is not a friendly debuff
        assert(!ai::IsDispelWorthMana(in));
        ++passed;
    }

    std::cout << "PASS: test_dispel_mana_policy (" << passed << " checks, 0 failed)\n";
    return 0;
}
