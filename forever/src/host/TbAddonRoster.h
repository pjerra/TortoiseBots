// The addon roster verbs (TBF #130 piece f-roster): `roster`/`list` (one full
// TB-shaped reply) and `alts` (the away-alt summary). Read data only through
// the existing helpers (BotMgr, party state, the character cache); the chat
// `.bot` entry in host/player/TbPlayerCmd.cpp is untouched.
//
// Threading: the handlers run on the world thread inside the addon drain.
#pragma once

namespace TortoiseBots
{
namespace roster
{
// Called from TbScripts.cpp right after addon::AddScripts(). Registers the
// three verbs; each registration also advertises its bare token for TBM:VERBS.
void RegisterAddonVerbsRoster();
} // namespace roster
} // namespace TortoiseBots
