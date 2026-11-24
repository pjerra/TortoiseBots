// Turtle side of the both-core helpers of the open-world PvP seek port. Same names and signatures as
// forever/src/compat/TbCompat_PvP.h; each one is the expression the bot always made (the call is moved, not
// changed), so the Turtle behaviour is untouched. Found through ai/playerbot/TbCompatPvP.h (`__has_include`).
#pragma once

inline bool HasPvpChallengeModeCompat(Player* target)
{
    return target->HasChallenge(CHALLENGE_SLOW_AND_STEADY) || target->HasChallenge(CHALLENGE_EXHAUSTION_MODE) ||
        target->HasChallenge(CHALLENGE_HARDCORE) || target->HasChallenge(CHALLENGE_VAGRANT_MODE) ||
        target->HasChallenge(CHALLENGE_BOARING_MODE) || target->HasChallenge(CHALLENGE_CRAFTMASTER) ||
        target->HasChallenge(CHALLENGE_LUNATIC) || target->HasChallenge(CHALLENGE_BREWMASTER) ||
        target->HasChallenge(CHALLENGE_HEROIC);
}

inline bool IsGmVisibleCompat(Player* target)
{
    return target->IsGMVisible();
}

inline float MaxEnemyPlayerAttackDistanceCompat(Player* bot)
{
    if (!bot->GetBattleGround())
        return 60.0f;

    if (bot->InBattleGround())
    {
        BattleGround* bg = bot->GetBattleGround();
        if (!bg)
            return 40.0f;

        BattleGroundTypeId bgType = bg->GetTypeID();

        if (bgType == BATTLEGROUND_AV)
        {
            bool strifeTime = bg->GetStartTime() < (uint32)(20 * MINUTE * IN_MILLISECONDS);
            return strifeTime ? 40.0f : 10.0f;
        }
    }

    return 40.0f;
}
