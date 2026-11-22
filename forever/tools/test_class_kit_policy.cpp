#include "../../ai/playerbot/InterruptCastingPolicy.h"
#include <cstdio>

namespace
{
int g_failures = 0;

void Check(bool cond, char const* name)
{
    if (!cond)
    {
        std::printf("FAIL: %s\n", name);
        ++g_failures;
    }
}
}

int main()
{
    ai::InterruptTargetSpell bolt{};
    bolt.spell = reinterpret_cast<Spell const*>(1);
    bolt.state = ai::INTERRUPT_SPELL_PREPARING;
    bolt.castTime = 2000;
    bolt.preventionType = 1; // SILENCE
    bolt.interruptFlags = 0x02; // INTERRUPT_FLAG_DAMAGE

    ai::InterruptSpellInfo scatter{};
    scatter.effect[0] = ai::INTERRUPT_EFFECT_APPLY_AURA;
    scatter.effectAura[0] = 5; // SPELL_AURA_MOD_CONFUSE

    Check(ai::ShouldInterruptTargetCasting(&bolt, nullptr, scatter, false),
          "confuse disorient (Scatter Shot) interrupts preparing cast even with allowStun=false");

    ai::InterruptSpellInfo immuneScatter = scatter;
    immuneScatter.effectImmune[0] = true;
    Check(!ai::ShouldInterruptTargetCasting(&bolt, nullptr, immuneScatter, false),
          "immune confuse disorient does not interrupt");

    if (g_failures > 0)
    {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    std::printf("PASS: test_class_kit_policy\n");
    return 0;
}
