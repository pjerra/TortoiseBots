#pragma once

// Pure policy for the Fire Nova range gates. On the 1.12 data Fire Nova is
// a totem DROP that detonates after 4s; on the Forever data it is a burst
// around the active fire totem. Both cover 10 yd from the totem, which the
// bot drops at its feet, so the bot-to-target distance at cast time bounds
// the damage. Chain heal needs no change: the live
// `medium aoe heal -> chain heal` row already matches priest/druid shape.

namespace ai
{
    // Burst range: the totem lands at our feet, so the bot-to-target
    // distance at cast time bounds the pulse.
    inline float FireNovaDropRange() { return 10.0f; }

    inline bool FireNovaDropShouldFire(float botDistanceToTarget)
    {
        return botDistanceToTarget <= FireNovaDropRange();
    }

    // Forever's Fire Nova needs a live fire totem (the core refuses the
    // cast otherwise) and fires around it, so both conditions hold.
    inline bool FireNovaPulseShouldFire(bool fireTotemStanding, float botDistanceToTarget)
    {
        return fireTotemStanding && FireNovaDropShouldFire(botDistanceToTarget);
    }
}
