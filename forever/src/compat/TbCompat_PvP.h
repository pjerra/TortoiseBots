// ForeverCore side of the both-core helpers of the open-world PvP seek port. Turtle side: ai/cmangos-compat-pvp.h
// (same names and signatures; there each helper is the expression the bot always made). Both are found through
// ai/playerbot/TbCompatPvP.h (`__has_include`), so the shared shim headers stay untouched.
#pragma once

// The Turtle core's challenge modes (Slow and Steady, Hardcore, ...) do not exist here, so no player carries one.
inline bool HasPvpChallengeModeCompat(Player* /*target*/)
{
    return false;
}

// A GM with visibility off cannot be attacked; a visible GM can. F spells it with a small i (Player.h:1341).
inline bool IsGmVisibleCompat(Player* target)
{
    return target->isGMVisible();
}

// The aggro range for an enemy player. T `EnemyPlayerValue::GetMaxAttackDistance`: 60 yd in the world, 40 in a
// battleground (10 in Alterac Valley after its first 20 minutes). F has the same battlemaster data under its own
// names (Battleground.h:279-284, Player.h:2716); battlegrounds are not ported, so in practice the world answer runs.
inline float MaxEnemyPlayerAttackDistanceCompat(Player* bot)
{
    if (!bot->GetBattleground())
        return 60.0f;

    if (bot->InBattleground())
    {
        Battleground* bg = bot->GetBattleground();
        if (!bg)
            return 40.0f;

        if (bg->GetTypeID() == BATTLEGROUND_AV)
        {
            bool strifeTime = bg->GetElapsedTime() < (uint32)(20 * MINUTE * IN_MILLISECONDS);
            return strifeTime ? 40.0f : 10.0f;
        }
    }

    return 40.0f;
}
