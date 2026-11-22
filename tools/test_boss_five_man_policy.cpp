#include "../ai/playerbot/BossTacticsFiveManPolicy.h"
#include <cassert>
#include <iostream>
using namespace ai;
int main()
{
    for (uint32_t map:{33u,34u,36u,43u,47u,48u,70u,90u,109u,129u,189u,209u,229u,230u,289u,329u,349u,389u,429u}) assert(FiveManMap(map));
    for (uint32_t map:{0u,1u,249u,309u,409u,469u,509u,531u,533u,532u,807u}) assert(!FiveManMap(map));
    for (auto kind:{BossTacticKind::Interrupt,BossTacticKind::KillFirst,BossTacticKind::Behind,BossTacticKind::AvoidCast}) assert(FiveManKind(kind));
    assert(!FiveManKind(BossTacticKind::SwapOnAura));
    assert(!FiveManKind(BossTacticKind::TankPost));
    assert(!FiveManKind(BossTacticKind::None));
    assert(FiveManSafetyAction("boss tactics stand"));
    assert(FiveManSafetyAction("boss tactics step behind"));
    assert(FiveManSafetyAction("boss tactics avoid cast"));
    assert(!FiveManSafetyAction("boss tactics kill first"));
    assert(!FiveManSafetyAction("melee"));
    // A pull wait lets the safety moves and the interrupt of a listed (already engaged) cast through, nothing that deals damage.
    assert(FiveManPullWaitAllowed("boss tactics interrupt"));
    assert(FiveManPullWaitAllowed("boss tactics stand"));
    assert(FiveManPullWaitAllowed("boss tactics avoid cast"));
    assert(!FiveManPullWaitAllowed("boss tactics kill first"));
    assert(!FiveManPullWaitAllowed("melee"));
    assert(!FiveManSafetyAction("boss tactics interrupt"));   // the kill-first multiplier still treats it as its own action
    assert(!BossTacticsWanted(false,false,true));
    assert(!BossTacticsWanted(true,true,false));
    assert(BossTacticsWanted(false,true,true));
    BossTacticRow interrupt;interrupt.map=389;interrupt.bossEntry=11518;interrupt.kind=BossTacticKind::Interrupt;interrupt.spellId=20800;
    ActiveBossRows rows{&interrupt};
    assert(IsInterruptListed(rows,11518,20800));
    assert(!IsInterruptListed(rows,11519,20800));
    assert(!IsInterruptListed(rows,11518,18267));
    assert(!IsInterruptListed(rows,11518,0));
    std::cout<<"PASS: five-man scope, supported mechanics, pull movement boundary and exact interrupt binding\n";
}
