#include "../forever/src/host/TbOnyxiaGroundPolicy.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
using namespace TortoiseBots::onyxia;
int main()
{
    struct Member { int boss,group,owner,instance; DamageGate gate; };
    std::map<int,Member> members{
        {1,{10184,10,1,40,{false}}}, {2,{10184,10,1,40,{false}}},
        {3,{10184,20,3,40,{false}}}, {4,{10184,10,4,41,{false}}},
        {5,{99999,10,5,40,{false}}}};
    assert(Followers(members,1)==std::vector<int>{2});
    ReassignOwner(members,members.at(1),2);
    assert(members.at(1).owner==2 && members.at(2).owner==2);
    assert(members.at(1).gate.held && members.at(2).gate.held);
    assert(members.at(3).owner==3 && !members.at(3).gate.held);
    assert(members.at(4).owner==4 && !members.at(4).gate.held);
    assert(members.at(5).owner==5 && !members.at(5).gate.held);
    assert(Followers(members,1).empty());
    assert(Followers(members,2)==std::vector<int>{1});
    DamageGate gate;
    assert(gate.Update(true,false,100,5000));
    assert(gate.Update(true,true,100,2999));
    assert(!gate.Update(true,true,100,3000));
    assert(!gate.Update(true,true,50,4000));
    assert(gate.Update(true,true,100,-1));
    assert(!gate.Update(true,true,100,3000));
    assert(gate.Update(true,true,40,4000));
    assert(gate.Update(true,true,59,4000));
    assert(!gate.Update(true,true,60,4000));
    assert(gate.Update(false,true,100,5000));
    assert(!gate.Update(true,true,100,5000));
    assert(gate.Update(true,true,std::numeric_limits<float>::quiet_NaN(),5000));
    Point boss{-30,-215,-86};
    assert(Flank({-30,-189,-86},boss,0));
    assert(!Flank({-4,-215,-86},boss,0));
    assert(!Flank({-56,-215,-86},boss,0));
    assert(RouteSafe({{-30,-215,-86},{22,-217,-85}},{},boss,0,true));
    assert(!RouteSafe({{-30,-215,-86},{22,-260,-85}},{},boss,0,true));
    assert(!RouteSafe({{-30,-215,-86},{22,-217,-85}},{{0,-215,-85}},boss,0,true));
    assert(!RouteSafe({{-30,-215,-86},{22,-217,-85}},{},boss,0,false));
    assert(RouteSafe({{-30,-190,-86},{-25,-190,-86}},{},boss,0,false));
    // Safe endpoints cannot hide a segment crossing the boss's front cone.
    assert(!RouteSafe({{-20,-190,-86},{-20,-240,-86}},{},boss,0,false));
    assert(!RouteSafe({{-30,-190,-86},{-25,-190,std::numeric_limits<float>::quiet_NaN()}},{},boss,0,false));
    assert(Ground(Phase::GroundOne) && Ground(Phase::GroundThree));
    assert(!Ground(Phase::Air) && !Ground(Phase::Idle));
    std::cout << "PASS: Onyxia ground owner isolation, threat hysteresis, flanks and complete route safety\n";
}
