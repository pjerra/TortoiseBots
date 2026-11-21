// Core-free spec-choice policy for ForeverCore traits and `.tb spec` overrides.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace slice
{
namespace spec
{
enum ClassId : unsigned
{
    ClassWarrior = 1,
    ClassPaladin = 2,
    ClassHunter = 3,
    ClassRogue = 4,
    ClassPriest = 5,
    ClassShaman = 7,
    ClassMage = 8,
    ClassWarlock = 9,
    ClassDruid = 11,
};

struct TraitNodeLayout
{
    std::int32_t nodeId = 0;
    std::int32_t posX = 0;
    std::int32_t posY = 0;
};

// Classic talent tabs sit side by side in one trait tree, separated by >= 1500 in X;
// hidden nodes sit outside rows 0..6 or in small clusters (< 8 nodes).
inline std::unordered_map<std::int32_t, int> BuildNodeTabMap(std::vector<TraitNodeLayout> const& nodes)
{
    std::unordered_map<std::int32_t, int> result;
    if (nodes.empty())
        return result;

    std::vector<std::int32_t> ys;
    ys.reserve(nodes.size());
    for (TraitNodeLayout const& n : nodes)
        ys.push_back(n.posY);
    std::sort(ys.begin(), ys.end());

    std::int32_t topY = ys.front();
    for (std::int32_t y : ys)
    {
        if (std::count_if(ys.begin(), ys.end(), [y](std::int32_t o) { return std::abs(o - y) <= 100; }) >= 3)
        {
            topY = y;
            break;
        }
    }

    struct Placed { std::int32_t nodeId; std::int32_t x; };
    std::vector<Placed> placed;
    placed.reserve(nodes.size());
    for (TraitNodeLayout const& n : nodes)
    {
        std::int32_t const row = static_cast<std::int32_t>(std::lround(double(n.posY - topY) / 600.0));
        if (row < 0 || row > 6)
            continue;
        placed.push_back({ n.nodeId, n.posX });
    }
    if (placed.empty())
        return result;

    std::sort(placed.begin(), placed.end(), [](Placed const& a, Placed const& b) { return a.x < b.x; });

    std::vector<std::vector<Placed>> tabs(1);
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (i > 0 && placed[i].x - placed[i - 1].x >= 1500)
            tabs.emplace_back();
        tabs.back().push_back(placed[i]);
    }
    tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [](std::vector<Placed> const& t) { return t.size() < 8; }), tabs.end());

    for (size_t t = 0; t < tabs.size() && t < 3; ++t)
        for (Placed const& p : tabs[t])
            result[p.nodeId] = static_cast<int>(t);

    return result;
}

inline int DefaultClassSpecTab(unsigned classId)
{
    switch (classId)
    {
        case ClassMage:
        case ClassRogue:
        case ClassHunter:
            return 1;
        case ClassDruid:
        case ClassShaman:
            return 2;
        default:
            return 0;
    }
}

inline int PickSpecTab(unsigned classId, std::uint32_t level, int forcedSpecTab, int tab0, int tab1, int tab2)
{
    if (forcedSpecTab >= 0 && forcedSpecTab <= 2)
        return forcedSpecTab;

    int const total = tab0 + tab1 + tab2;
    if (level >= 10 && total > 0)
    {
        int const counts[3] = { tab0, tab1, tab2 };
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (counts[i] > counts[best])
                best = i;
        return best;
    }

    return DefaultClassSpecTab(classId);
}

inline int ResolveKitSpecTab(unsigned classId, std::uint32_t level, int forcedSpecTab, std::uint8_t effectiveRole, int tab0, int tab1, int tab2)
{
    int const tab = PickSpecTab(classId, level, forcedSpecTab, tab0, tab1, tab2);
    if (forcedSpecTab >= 0 && forcedSpecTab <= 2)
        return forcedSpecTab;

    if (effectiveRole == 1 /*RoleTank*/)
    {
        if (classId == ClassWarrior) return 2;
        if (classId == ClassPaladin) return 1;
        if (classId == ClassDruid) return 1;
    }
    else if (effectiveRole != 0 /*RoleNone*/)
    {
        if (classId == ClassWarrior && tab == 2)
            return 0;
        if (classId == ClassPaladin && tab == 1)
            return effectiveRole == 2 /*RoleHealer*/ ? 0 : 2;
    }
    return tab;
}

inline std::uint8_t DefaultSpecRole(unsigned classId, std::uint32_t level, int specTab)
{
    switch (classId)
    {
        case ClassWarrior:
            return specTab == 2 ? 1 /*RoleTank*/ : 4 /*RoleDps*/;
        case ClassPaladin:
            return specTab == 0 ? 2 /*RoleHealer*/ : (specTab == 1 ? 1 /*RoleTank*/ : 4 /*RoleDps*/);
        case ClassPriest:
            return specTab == 2 ? 4 /*RoleDps*/ : 2 /*RoleHealer*/;
        case ClassShaman:
            return specTab == 2 ? 2 /*RoleHealer*/ : 4 /*RoleDps*/;
        case ClassDruid:
            if (level < 10)
                return 4 /*RoleDps*/;
            return specTab == 2 ? 2 /*RoleHealer*/ : 4 /*RoleDps*/;
        default:
            return 4 /*RoleDps*/;
    }
}

inline char const* SpecTabName(unsigned classId, int tab)
{
    if (tab < 0 || tab > 2)
        return "auto";
    switch (classId)
    {
        case ClassWarrior: return tab == 0 ? "arms" : (tab == 1 ? "fury" : "protection");
        case ClassPaladin: return tab == 0 ? "holy" : (tab == 1 ? "protection" : "retribution");
        case ClassHunter:  return tab == 0 ? "beast mastery" : (tab == 1 ? "marksmanship" : "survival");
        case ClassRogue:   return tab == 0 ? "assassination" : (tab == 1 ? "combat" : "subtlety");
        case ClassPriest:  return tab == 0 ? "discipline" : (tab == 1 ? "holy" : "shadow");
        case ClassShaman:  return tab == 0 ? "elemental" : (tab == 1 ? "enhancement" : "restoration");
        case ClassMage:    return tab == 0 ? "arcane" : (tab == 1 ? "fire" : "frost");
        case ClassWarlock: return tab == 0 ? "affliction" : (tab == 1 ? "demonology" : "destruction");
        case ClassDruid:   return tab == 0 ? "balance" : (tab == 1 ? "feral" : "restoration");
        default:           return "unknown";
    }
}

inline bool ParseSpecArg(unsigned classId, std::string_view lowerWord, int& outTab)
{
    if (lowerWord == "none" || lowerWord == "auto" || lowerWord == "clear")
    {
        outTab = -1;
        return true;
    }
    if (lowerWord == "0") { outTab = 0; return true; }
    if (lowerWord == "1") { outTab = 1; return true; }
    if (lowerWord == "2") { outTab = 2; return true; }

    switch (classId)
    {
        case ClassWarrior:
            if (lowerWord == "arms") { outTab = 0; return true; }
            if (lowerWord == "fury") { outTab = 1; return true; }
            if (lowerWord == "protection" || lowerWord == "prot") { outTab = 2; return true; }
            return false;
        case ClassPaladin:
            if (lowerWord == "holy") { outTab = 0; return true; }
            if (lowerWord == "protection" || lowerWord == "prot") { outTab = 1; return true; }
            if (lowerWord == "retribution" || lowerWord == "ret") { outTab = 2; return true; }
            return false;
        case ClassHunter:
            if (lowerWord == "beast_mastery" || lowerWord == "beastmastery" || lowerWord == "bm") { outTab = 0; return true; }
            if (lowerWord == "marksmanship" || lowerWord == "mm") { outTab = 1; return true; }
            if (lowerWord == "survival" || lowerWord == "surv") { outTab = 2; return true; }
            return false;
        case ClassRogue:
            if (lowerWord == "assassination" || lowerWord == "mut") { outTab = 0; return true; }
            if (lowerWord == "combat") { outTab = 1; return true; }
            if (lowerWord == "subtlety" || lowerWord == "sub") { outTab = 2; return true; }
            return false;
        case ClassPriest:
            if (lowerWord == "discipline" || lowerWord == "disc") { outTab = 0; return true; }
            if (lowerWord == "holy") { outTab = 1; return true; }
            if (lowerWord == "shadow") { outTab = 2; return true; }
            return false;
        case ClassShaman:
            if (lowerWord == "elemental" || lowerWord == "ele") { outTab = 0; return true; }
            if (lowerWord == "enhancement" || lowerWord == "enh") { outTab = 1; return true; }
            if (lowerWord == "restoration" || lowerWord == "resto") { outTab = 2; return true; }
            return false;
        case ClassMage:
            if (lowerWord == "arcane") { outTab = 0; return true; }
            if (lowerWord == "fire") { outTab = 1; return true; }
            if (lowerWord == "frost") { outTab = 2; return true; }
            return false;
        case ClassWarlock:
            if (lowerWord == "affliction" || lowerWord == "aff") { outTab = 0; return true; }
            if (lowerWord == "demonology" || lowerWord == "demo") { outTab = 1; return true; }
            if (lowerWord == "destruction" || lowerWord == "destro") { outTab = 2; return true; }
            return false;
        case ClassDruid:
            if (lowerWord == "balance" || lowerWord == "boomkin") { outTab = 0; return true; }
            if (lowerWord == "feral" || lowerWord == "cat" || lowerWord == "bear") { outTab = 1; return true; }
            if (lowerWord == "restoration" || lowerWord == "resto") { outTab = 2; return true; }
            return false;
        default:
            return false;
    }
}
}
}
}
