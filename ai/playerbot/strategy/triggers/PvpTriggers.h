#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class EnemyPlayerNear : public Trigger
    {
    public:
        EnemyPlayerNear(PlayerbotAI* ai) : Trigger(ai, "enemy player near") {}

    public:
        virtual bool IsActive() override;
    };

    class PlayerHasNoFlag : public Trigger
    {
    public:
        PlayerHasNoFlag(PlayerbotAI* ai) : Trigger(ai, "player has no flag") {}

    public:
        virtual bool IsActive() override;
    };

    class PlayerHasFlag : public Trigger
    {
    public:
        PlayerHasFlag(PlayerbotAI* ai) : Trigger(ai, "player has flag") {}

    public:
        virtual bool IsActive() override;
    };

    class EnemyFlagCarrierNear : public Trigger
    {
    public:
        EnemyFlagCarrierNear(PlayerbotAI* ai) : Trigger(ai, "enemy flagcarrier near") {}

    public:
        virtual bool IsActive() override;
    };

    class TeamFlagCarrierNear : public Trigger
    {
    public:
        TeamFlagCarrierNear(PlayerbotAI* ai) : Trigger(ai, "team flagcarrier near") {}

    public:
        virtual bool IsActive() override;
    };

    class TeamHasFlag : public Trigger
    {
    public:
        TeamHasFlag(PlayerbotAI* ai) : Trigger(ai, "team has flag") {}

    public:
        virtual bool IsActive() override;
    };

    class EnemyTeamHasFlag : public Trigger
    {
    public:
        EnemyTeamHasFlag(PlayerbotAI* ai) : Trigger(ai, "enemy team has flag") {}

    public:
        virtual bool IsActive() override;
    };

    class PlayerIsInBattleground : public Trigger
    {
    public:
        PlayerIsInBattleground(PlayerbotAI* ai) : Trigger(ai, "in battleground") {}

    public:
        virtual bool IsActive() override;
    };

    class BgWaitingTrigger : public Trigger
    {
    public:
        BgWaitingTrigger(PlayerbotAI* ai) : Trigger(ai, "bg waiting", 30) {}

    public:
        virtual bool IsActive() override;
    };

    class BgActiveTrigger : public Trigger
    {
    public:
        BgActiveTrigger(PlayerbotAI* ai) : Trigger(ai, "bg active", 1) {}

    public:
        virtual bool IsActive() override;
    };

    // Battleground mount slot: in progress, not mounted (nor in a travel
    // form), out of combat, not carrying a flag, and the current
    // "bg objective" is over 40 yd away.
    // Checked every 5 s (like mod-playerbots' timer-paced mount check), so a
    // mount cast that keeps failing costs at most one try per 5 s and the
    // objective move runs in between.
    class BgObjectiveFarTrigger : public Trigger
    {
    public:
        BgObjectiveFarTrigger(PlayerbotAI* ai) : Trigger(ai, "bg objective far", 5) {}

    public:
        virtual bool IsActive() override;
    };

    class BgInviteActiveTrigger : public Trigger
    {
    public:
        BgInviteActiveTrigger(PlayerbotAI* ai) : Trigger(ai, "bg invite active", 10) {}

    public:
        virtual bool IsActive() override;
    };

    class BgEndedTrigger : public Trigger
    {
    public:
        BgEndedTrigger(PlayerbotAI* ai) : Trigger(ai, "bg ended", 10) {}

    public:
        virtual bool IsActive() override;
    };

    // Periodic objective re-pick watchdog (SOC-P6): fires about once a
    // minute inside a match so a bot walking toward stale data drops it.
    // Donor mod-playerbots TimerBGTrigger.
    class TimerBgTrigger : public Trigger
    {
    public:
        TimerBgTrigger(PlayerbotAI* ai) : Trigger(ai, "timer bg", 1), lastCheck(0) {}

    public:
        virtual bool IsActive() override;

    private:
        time_t lastCheck;
    };

    class PlayerIsInBattlegroundWithoutFlag : public Trigger
    {
    public:
        PlayerIsInBattlegroundWithoutFlag(PlayerbotAI* ai) : Trigger(ai, "in battleground without flag") {}

    public:
        virtual bool IsActive() override;
    };

    class PlayerWantsInBattlegroundTrigger : public Trigger
    {
    public:
        PlayerWantsInBattlegroundTrigger(PlayerbotAI* ai) : Trigger(ai, "wants in bg") {}

    public:
        virtual bool IsActive() override;
    };

}
