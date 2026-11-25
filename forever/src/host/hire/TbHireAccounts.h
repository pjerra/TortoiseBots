// Hire accounts on ForeverCore: the game accounts the module creates for hired companions, kept apart from the pool's
// (`TBPOOL*`, counted by the pool cap) and from players' accounts. One hire account carries one character at a time,
// because one game account carries one bot session.
//
// `AcquireHireAccount` is the only way a hire gets an account (package P2 calls it before it creates the character). It
// reuses an existing `TBHIRE<nnnn>` account with no character, whose e-mail still carries the module's stamp, and creates
// a new one through `AccountMgr::CreateAccount` when none has room. The stamp - not the name alone - is what proves the
// account is the module's; `IsHireAccount` is the same proof for the deletion guard.
#pragma once

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace hire
{

// An account with room for a hire's character, creating one when needed. 0 = failed, and `error` says why.
std::uint32_t AcquireHireAccount(std::string& error);

// False for 0, for a name outside `TBHIRE<nnnn>`, and for an account whose e-mail does not carry the module's stamp.
bool IsHireAccount(std::uint32_t accountId);

// False when the characters database does not answer, or the account already holds a character.
bool HireAccountHasRoom(std::uint32_t accountId);

} // namespace hire
} // namespace TortoiseBots
