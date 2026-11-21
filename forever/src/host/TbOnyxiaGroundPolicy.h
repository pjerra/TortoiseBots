#pragma once
#include "TbOnyxiaPolicy.h"
#include <cmath>
#include <vector>

namespace TortoiseBots::onyxia
{
struct Point { float x, y, z; };
inline float Distance(Point a, Point b) { return std::hypot(a.x-b.x, a.y-b.y); }
inline bool Floor(Point p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        p.x >= -110 && p.x <= 38 && p.y >= -255 && p.y <= -170 && p.z >= -94 && p.z <= -80;
}
inline bool TankFloor(Point p) { return Floor(p) && p.y >= -232 && p.y <= -198; }
inline bool Flank(Point point, Point boss, float facing)
{
    float const angle = std::remainder(std::atan2(point.y-boss.y, point.x-boss.x)-facing, 6.2831853f);
    return std::abs(angle) >= 1.15f && std::abs(angle) <= 2.2f;
}
inline bool OutsideCones(Point p, Point boss, float facing)
{
    float const angle = std::abs(std::remainder(std::atan2(p.y-boss.y, p.x-boss.x)-facing, 6.2831853f));
    float const distance = Distance(p,boss);
    return (distance > 45 || angle > 1.0472f) && (distance > 30 || angle < 2.4871f);
}
inline bool RouteSafe(std::vector<Point> const& path, std::vector<Point> const& eggs,
    Point boss, float facing, bool tank)
{
    if (path.size() < 2) return false;
    float total = 0;
    for (std::size_t i=1; i<path.size(); ++i)
    {
        Point const a=path[i-1], b=path[i];
        float const length=std::hypot(Distance(a,b),b.z-a.z);
        if (!std::isfinite(length) || (total+=length)>160) return false;
        unsigned const steps=unsigned(std::ceil(length/0.5f))+1;
        for (unsigned j=0;j<=steps;++j)
        {
            float const t=float(j)/steps;
            Point const p{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            if (tank ? !TankFloor(p) : (!Floor(p) || !OutsideCones(p,boss,facing))) return false;
            for (Point egg:eggs) if (Distance(p,egg)<12) return false;
        }
    }
    return true;
}
struct DamageGate
{
    bool held = true;
    bool Update(bool ownerAlive, bool ownerVictim, float ownerHp, float lead)
    {
        if (!ownerAlive || !ownerVictim || !std::isfinite(ownerHp) || !std::isfinite(lead) || ownerHp<=40 || lead<0) held=true;
        else if (ownerHp>=60 && lead>=3000) held=false;
        return held;
    }
};
// One replacement is published to every participant before another bot ticks.
// Copy the old identity because context may itself be an element of states.
template<class States, class State, class Guid>
void ReassignOwner(States& states, State const& context, Guid next)
{
    auto const boss=context.boss, group=context.group, previous=context.owner;
    auto const instance=context.instance;
    for (auto& member:states)
        if (member.second.boss==boss && member.second.group==group &&
            member.second.instance==instance && member.second.owner==previous)
        { member.second.owner=next;member.second.gate.held=true; }
}
template<class States>
std::vector<typename States::key_type> Followers(States const& states, typename States::key_type owner)
{
    std::vector<typename States::key_type> followers;
    for (auto const& member:states)
        if (member.first!=owner && member.second.owner==owner) followers.push_back(member.first);
    return followers;
}
inline bool Ground(Phase phase) { return phase==Phase::GroundOne || phase==Phase::GroundThree; }
}
