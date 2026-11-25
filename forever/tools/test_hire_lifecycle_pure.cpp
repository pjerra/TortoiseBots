// Standalone test of the hired-companion decisions the Forever lifecycle relies on: the departure rules
// (runtime/policy/hire/HireDeparturePolicy.h), the deletion guard (HireDeletionPolicy.h), the battleground rules
// (HirePvpPolicy.h) and the hire-account naming/room rules (host/hire/TbHireAccountsPure.h). No core type is used.
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iforever/src forever/tools/test_hire_lifecycle_pure.cpp -o /tmp/test_hire_lifecycle_pure
//   /tmp/test_hire_lifecycle_pure
#include "runtime/policy/hire/HireDeletionPolicy.h"
#include "runtime/policy/hire/HireDeparturePolicy.h"
#include "runtime/policy/hire/HirePvpPolicy.h"
#include "host/hire/TbHireAccountsPure.h"

#include <cstdio>
#include <string>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    using namespace TortoiseBots;
    using namespace TortoiseBots::hire::pure;

    // ---- the deletion guard: all three durable facts, in order ----
    CHECK(DecideHireDeletion(false, false, false) == HireDeletionDecision::RefusedNoHireLedger);
    CHECK(DecideHireDeletion(false, true, true) == HireDeletionDecision::RefusedNoHireLedger);
    CHECK(DecideHireDeletion(true, false, true) == HireDeletionDecision::RefusedAccountNotManaged);
    CHECK(DecideHireDeletion(true, true, false) == HireDeletionDecision::RefusedAccountMismatch);
    CHECK(DecideHireDeletion(true, true, true) == HireDeletionDecision::Allowed);
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::Allowed)) == "allowed");
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedNoHireLedger)) == "no hire ledger row");
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedAccountNotManaged)) == "account is not a managed pool account");
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedAccountMismatch)) == "account differs from the ledger row");

    // ---- leaving the hirer's party ----
    CHECK(!ShouldDismissHireOnGroupDeparture(false, true, true, false));        // not a hire
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, true, true));          // still together
    CHECK(!ShouldDismissHireOnGroupDeparture(true, false, true, false));       // master offline: the grace owns it
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, false, false));        // not grouped yet: provisioning
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Bound));
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Transition));
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false));
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::None));

    // The hire itself was removed: a bind/unbind step is the one exception; a bound hire someone else removed still ends.
    CHECK(!ShouldDismissHireOnOwnRemoval(HireCommandState::Transition));
    CHECK(ShouldDismissHireOnOwnRemoval(HireCommandState::None));
    CHECK(ShouldDismissHireOnOwnRemoval(HireCommandState::Bound));

    // A disband ends an ordinary hire, never one held by the hirer's binding.
    CHECK(ShouldDismissHireOnDisband(HireCommandState::None));
    CHECK(!ShouldDismissHireOnDisband(HireCommandState::Bound));
    CHECK(!ShouldDismissHireOnDisband(HireCommandState::Transition));

    // The sweep's verdict on a bound hire.
    CHECK(DecideBoundHireSweep(false, true, true) == BoundHireVerdict::ReturnToHirer);
    CHECK(DecideBoundHireSweep(true, false, false) == BoundHireVerdict::Keep);
    CHECK(DecideBoundHireSweep(true, true, true) == BoundHireVerdict::Keep);
    CHECK(DecideBoundHireSweep(true, true, false) == BoundHireVerdict::DropState);

    // The binding's end returns the hire to the hirer, or parks it while the hirer is offline.
    CHECK(DecideHireBindingEnd(true, true, true, false) == HireBindingEnd::Leave);              // the character is gone
    CHECK(DecideHireBindingEnd(true, false, false, true) == HireBindingEnd::ParkForLogin);       // hirer offline
    CHECK(DecideHireBindingEnd(false, true, false, true) == HireBindingEnd::AwaitWorld);         // out of world
    CHECK(DecideHireBindingEnd(true, true, true, true) == HireBindingEnd::Keep);                 // already together
    CHECK(DecideHireBindingEnd(true, true, false, true) == HireBindingEnd::RejoinHirer);

    // The re-join of a hire held out of world.
    CHECK(DecideAwaitedRegroup(true, false, true) == AwaitedRegroup::GiveUp);
    CHECK(DecideAwaitedRegroup(false, false, false) == AwaitedRegroup::Park);
    CHECK(DecideAwaitedRegroup(true, true, false) == AwaitedRegroup::Retry);
    CHECK(DecideAwaitedRegroup(true, false, false) == AwaitedRegroup::Wait);
    CHECK(KeepAwaitingRegroup(false, false));
    CHECK(!KeepAwaitingRegroup(true, false));
    CHECK(!KeepAwaitingRegroup(false, true));

    // A party move inside a dungeon keeps the bot's instance; nothing else is touched.
    CHECK(DecidePartyMoveInstance(false, false, false, false) == PartyMoveInstance::Nothing);
    CHECK(DecidePartyMoveInstance(true, true, false, false) == PartyMoveInstance::Nothing);
    CHECK(DecidePartyMoveInstance(true, false, false, false) == PartyMoveInstance::BindParty);
    CHECK(DecidePartyMoveInstance(true, false, true, true) == PartyMoveInstance::KeepValid);
    CHECK(DecidePartyMoveInstance(true, false, true, false) == PartyMoveInstance::Nothing);
    CHECK(DropSoloBindAfterPartyMove(PartyMoveInstance::BindParty, true, false, true));
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::BindParty, true, true, true));       // a permanent bind stays
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::BindParty, true, false, false));     // another instance
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::Nothing, true, false, true));

    // ---- battlegrounds ----
    CHECK(!ShouldDismissHireOnBattleground(true, true, true));            // HireAllowPvP
    CHECK(ShouldDismissHireOnBattleground(false, true, false));           // master inside
    CHECK(ShouldDismissHireOnBattleground(false, false, true));           // hire inside
    CHECK(!ShouldDismissHireOnBattleground(false, false, false));
    CHECK(ShouldWarnMasterOnBgQueue(false, true, false, false));
    CHECK(!ShouldWarnMasterOnBgQueue(false, true, false, true));          // once per stay
    CHECK(!ShouldWarnMasterOnBgQueue(false, true, true, false));          // inside: the entry notice owns it
    CHECK(!ShouldWarnMasterOnBgQueue(true, true, false, false));
    CHECK(!ShouldWarnMasterOnBgQueue(false, false, false, false));
    CHECK(ShouldDequeueHireFromBgQueue(false, true, false, false));
    CHECK(!ShouldDequeueHireFromBgQueue(false, true, true, false));
    CHECK(!ShouldDequeueHireFromBgQueue(false, true, false, true));
    CHECK(!ShouldDequeueHireFromBgQueue(true, true, false, false));
    CHECK(MayHireForPvpState(true, true, true));
    CHECK(!MayHireForPvpState(false, true, false));
    CHECK(!MayHireForPvpState(false, false, true));
    CHECK(MayHireForPvpState(false, false, false));
    CHECK(ShouldRefuseBattlegroundInvite(false, true));
    CHECK(!ShouldRefuseBattlegroundInvite(false, false));
    CHECK(!ShouldRefuseBattlegroundInvite(true, true));

    // ---- hire accounts ----
    CHECK(std::string(AccountName(1)) == "TBHIRE0001");
    CHECK(std::string(AccountName(42)) == "TBHIRE0042");
    CHECK(std::string(AccountName(9999)) == "TBHIRE9999");
    CHECK(AccountNumber("TBHIRE0001") == 1);
    CHECK(AccountNumber("tbhire0007") == 7);
    CHECK(AccountNumber("TBHIRE9999") == 9999);
    CHECK(AccountNumber("TBHIRE1") == 0);
    CHECK(AccountNumber("TBHIRE00000") == 0);
    CHECK(AccountNumber("TBHIRE0000") == 0);
    CHECK(AccountNumber("TBPOOL0001") == 0);
    CHECK(AccountNumber("TBHIRE00X1") == 0);
    CHECK(AccountNumber("") == 0);
    CHECK(IsAccountName("TBHIRE0042"));
    CHECK(!IsAccountName("TBPOOL0042"));
    CHECK(!IsAccountName("PLAYER"));
    CHECK(IsAccountStamp("TBHIRESTAMP"));
    CHECK(IsAccountStamp("tbhirestamp"));
    CHECK(!IsAccountStamp(""));
    CHECK(!IsAccountStamp("TBHIRE0001"));
    CHECK(kCharactersPerAccount == 1);
    CHECK(HasRoom(0));
    CHECK(!HasRoom(1));
    CHECK(!HasRoom(2));

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed)
        return 1;
    std::printf("PASS: hire lifecycle decisions\n");
    return 0;
}
