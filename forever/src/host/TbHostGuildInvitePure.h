#pragma once

// What the guild invite seam (host/TbHostGuild.h InviteByName) reports back. The core's own invite handler answers
// the inviter with an error code and keeps the ignore-list refusal completely silent, so the seam checks the same
// conditions the core does and returns a code the caller can answer a player with. Core-free: forever/tools/
// test_guild_request_pure.cpp proves the codes and the answers without the server.

#include <cstdint>

namespace tbhost
{
namespace guild
{

enum class InviteOutcome
{
    Ok,
    NotInWorld,           // the inviter has no session or is not in the world
    NotInGuild,           // the inviter is in no guild
    EmptyTarget,          // the name has no letters
    TargetNotFound,       // ERR_GUILD_PLAYER_NOT_FOUND_S: the target is not in the world
    TargetIgnoring,       // the target ignores the inviter: the core would send nothing at all
    WrongFaction,         // ERR_GUILD_NOT_ALLIED
    TargetInGuild,        // ERR_ALREADY_IN_GUILD_S
    TargetAlreadyInvited, // ERR_ALREADY_INVITED_TO_GUILD_S
    NoInviteRight,        // ERR_GUILD_PERMISSIONS
    CoreRefused,          // the handler returned without leaving the invite with the target
};

inline bool InviteSucceeded(InviteOutcome outcome)
{
    return outcome == InviteOutcome::Ok;
}

// The log token (`reason=`) and the owner-whisper reason word.
inline char const* InviteOutcomeCode(InviteOutcome outcome)
{
    switch (outcome)
    {
        case InviteOutcome::Ok: return "ok";
        case InviteOutcome::NotInWorld: return "not-in-world";
        case InviteOutcome::NotInGuild: return "not-in-guild";
        case InviteOutcome::EmptyTarget: return "empty-target";
        case InviteOutcome::TargetNotFound: return "target-not-found";
        case InviteOutcome::TargetIgnoring: return "target-ignoring";
        case InviteOutcome::WrongFaction: return "wrong-faction";
        case InviteOutcome::TargetInGuild: return "target-in-guild";
        case InviteOutcome::TargetAlreadyInvited: return "target-already-invited";
        case InviteOutcome::NoInviteRight: return "no-invite-right";
        case InviteOutcome::CoreRefused: return "core-refused";
    }
    return "core-refused";
}

} // namespace guild
} // namespace tbhost
