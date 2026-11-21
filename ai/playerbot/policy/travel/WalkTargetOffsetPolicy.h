#pragma once

#include <cmath>
#include <cstdint>

namespace ai
{
    constexpr float kWalkOffsetMinDistanceYd = 15.0f;
    constexpr float kWalkOffsetDefaultMaxYd = 2.5f;

    inline bool ShouldOffsetWalkTarget(bool inInstance, bool hasUnitTarget, bool exactSite,
        float distanceYd, float maxOffsetYd)
    {
        if (maxOffsetYd <= 0.0f || inInstance || hasUnitTarget || exactSite)
            return false;
        return distanceYd >= kWalkOffsetMinDistanceYd;
    }

    // Deterministic per-(bot, quantized destination) lateral offset so bots
    // walking the same route spread out instead of marching single-file, while
    // keeping consecutive ticks for the same destination stable for the route cache.
    inline void ComputeWalkTargetOffset(uint32_t botGuidLow, float destX, float destY,
        float maxOffsetYd, float& outDx, float& outDy)
    {
        if (maxOffsetYd <= 0.0f)
        {
            outDx = 0.0f;
            outDy = 0.0f;
            return;
        }

        uint32_t const qx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(destX / 4.0f)));
        uint32_t const qy = static_cast<uint32_t>(static_cast<int32_t>(std::floor(destY / 4.0f)));
        uint32_t h = botGuidLow ^ (qx * 0x9E3779B1u) ^ (qy * 0x85EBCA77u);
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;

        constexpr float kTwoPi = 6.283185307179586f;
        float const angle = (static_cast<float>(h & 0xFFFFu) / 65536.0f) * kTwoPi;
        float const frac = static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f;
        float const radius = maxOffsetYd * (0.25f + 0.75f * frac);

        outDx = std::cos(angle) * radius;
        outDy = std::sin(angle) * radius;
    }
}
