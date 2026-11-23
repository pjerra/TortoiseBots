// Debug command families that need the 1.12 client: the fake spell packets and aura holders
// (DebugSpellCommands.cpp), the transport and path probes (DebugTransportCommands.cpp), the
// travel-node and RPG debug readers (DebugTravelCommands.cpp), the position path planner
// (DebugPositionCommands.cpp) and the item/trade/mail readers (DebugItemCommands.cpp).
//
// The commands themselves stay advertised (they are on the shared DebugAction dispatch): each
// answers that this build does not carry the piece, so a GM sees why instead of a dead command.
// Everything the engine can answer without those files (values, the last action, strategies,
// combat facts, quest and level state) is the real DebugEngineCommands.cpp and
// DebugMiscCommands.cpp.
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "DebugAction.h"

using namespace ai;

namespace
{
bool NotPorted(PlayerbotAI* ai, Player* requester, char const* name, char const* why)
{
    ai->TellPlayer(requester, std::string("debug ") + name + ": not ported to ForeverCore (" + why + ").");
    return true;
}
} // namespace

void DebugAction::FakeSpell(uint32, Unit*, Unit*, ObjectGuid, std::list<ObjectGuid>, std::list<ObjectGuid>,
    WorldPosition, WorldPosition, bool)
{
}

void DebugAction::addAura(uint32, Unit*)
{
}

PositionTarget DebugAction::ParseLocation(std::string const&, Player*)
{
    return PositionTarget();
}

// ---- DebugSpellCommands.cpp: 1.12 fake cast packets and SpellAuraHolder auras ----
bool DebugAction::HandleDSpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "dspell", "the 1.12 fake spell visual"); }
bool DebugAction::HandleVSpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "vspell", "the 1.12 fake spell visual"); }
bool DebugAction::HandleASpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "aspell", "SpellAuraHolder"); }
bool DebugAction::HandleCSpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "cspell", "the 1.12 fake spell visual"); }
bool DebugAction::HandleFSpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "fspell", "the 1.12 fake spell visual"); }
bool DebugAction::HandleSpell(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "spell", "the 1.12 fake spell visual"); }
bool DebugAction::HandleTSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "tspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleUSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "uspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleDSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "dspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleVSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "vspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleISpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "ispellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleCSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "cspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleASpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "aspellmap", "SpellAuraHolder"); }
bool DebugAction::HandleGSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "gspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleMSpellMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "mspellmap", "the 1.12 fake spell visual"); }
bool DebugAction::HandleSoundMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "soundmap", "the 1.12 sound table"); }
bool DebugAction::HandleSounds(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "sounds", "the 1.12 sound table"); }
bool DebugAction::HandleDSound(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "dsound", "the 1.12 sound table"); }
bool DebugAction::HandleSound(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "sound", "the 1.12 sound table"); }

// ---- DebugTransportCommands.cpp: transports, flight paths and the map-ray probes ----
bool DebugAction::HandleMotion(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "motion", "the map ray probes"); }
bool DebugAction::HandleTransport(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "transport", "the transport table"); }
bool DebugAction::HandlePointOnTrans(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "pointontrans", "the transport table"); }
bool DebugAction::HandleDoTransport(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "dotrans", "the transport table"); }
bool DebugAction::HandleOnTrans(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "ontrans", "the transport table"); }
bool DebugAction::HandleOffTrans(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "offtrans", "the transport table"); }
bool DebugAction::HandlePathable(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "pathable", "the map ray probes"); }
bool DebugAction::HandleRandomSpot(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "randomspot", "the map ray probes"); }
bool DebugAction::HandlePrintMap(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "printmap", "the transport table"); }
bool DebugAction::HandleTransanal(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "transanal", "the transport table"); }
bool DebugAction::HandleUpdownspace(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "updownspace", "the map ray probes"); }
bool DebugAction::HandlePatharound(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "patharound", "the map ray probes"); }
bool DebugAction::HandleHeightForLos(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "heightforlos", "the map ray probes"); }

// ---- DebugTravelCommands.cpp: travel nodes and RPG targets ----
bool DebugAction::HandlePOI(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "poi", "the travel node table"); }
bool DebugAction::HandleRPG(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "rpg", "the RPG debug readers"); }
bool DebugAction::HandleRPGTargets(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "rpgtargets", "the RPG debug readers"); }
bool DebugAction::HandleTravel(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "travel", "the travel node table"); }
bool DebugAction::HandlePrintTravel(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "print travel", "the travel node table"); }
bool DebugAction::HandleSaveNode(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "save node", "the travel node table"); }
bool DebugAction::HandleShowNode(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "show node", "the travel node table"); }
bool DebugAction::HandleNodes(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "nodes", "the travel node table"); }

// ---- DebugMiscCommands.cpp: the quest-log internals (1.12 quest credit and field names) ----
bool DebugAction::HandleQuest(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "quest", "the 1.12 quest-log internals"); }

// ---- DebugPositionCommands.cpp: the path planner and location parser ----
bool DebugAction::HandlePosition(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "position", "the path planner; the engine's `debug position` remote still answers"); }

// ---- DebugItemCommands.cpp: item, trade, mail, loot, drop and taxi readers ----
bool DebugAction::HandleTrade(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "trade", "the trade debug readers"); }
bool DebugAction::HandleMail(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "mail", "the mail debug readers"); }
bool DebugAction::HandleItem(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "item", "the item debug readers"); }
bool DebugAction::HandleLoot(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "loot", "the loot debug readers"); }
bool DebugAction::HandleDrops(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "drops", "the loot debug readers"); }
bool DebugAction::HandleTaxi(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "taxi", "the taxi debug readers"); }
bool DebugAction::HandlePrice(Event&, Player* r, std::string const&) { return NotPorted(ai, r, "price", "the item debug readers"); }
