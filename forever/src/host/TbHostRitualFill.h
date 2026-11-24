// Issue #14 round 1: the host side of the ritual fill. A real player who uses a multi-user object (a type-18 ritual, like the
// Uldaman altars, or a scripted altar like UBRS's Blackrock Altar) is joined by the bots of the player's own group: they walk to
// the object, use it and stand still until it is done. The decisions are in host/TbObjectUsePure.h (tested standalone by
// forever/tools/test_object_use_policy.cpp); this header is the world half and stays core-free, so both builds may include it.
//
// Why the host drives it: a held bot's engine does nothing (ai/playerbot/PlayerbotAI.cpp:2561), so an engine action would be dead
// while it matters. The holds go into the engine's own RitualHoldSet (runtime/RemoteCommanderTasks.h:414-432), which stops the
// bot's craft and casts (PlayerbotAISpells.cpp:632) as well as its actions.
//
// Threading: Tick and NoteLogout are world thread only. OnPacket runs from ServerScript::OnPacketReceive, which the core may call
// off the world thread (CMSG_GAME_OBJ_USE is PROCESS_INPLACE, Opcodes.cpp:467): it only copies two guids and a timestamp under a
// mutex, and touches no world object.
#pragma once

#include <cstdint>

class Player;
class WorldPacket;
class WorldSession;

namespace tbhost
{
namespace ritualfill
{
// ServerScript::OnPacketReceive: records a real player's `use` of a game object for the next tick, only while the fill is on.
// A bot's own use is ignored; the object is re-read (and type-checked) on the world thread.
void OnPacket(WorldSession* session, WorldPacket& packet);

// BotMgr::Update (world thread): the fill runs once a second, `diffMs` only drives that clock.
void Tick(std::uint32_t diffMs);

// PlayerScript::OnLogout: forgets the bot's hold (a later character reusing the guid must not inherit it) and the targets the
// player started. World thread.
void NoteLogout(Player* player);
}
}
