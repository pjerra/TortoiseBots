// Commander surface of the tactical orders, M1: `.tb cmd stop|flee|guard|stance|engage|rebuff`.
// The per-bot work is the same queued order `.tb order` uses (host/TbTactics.h): this file only resolves the caller's binding, parses the
// words and answers in the `cmd` reply. It runs before the lifted judge (TbCmdOrders.cpp) because these verbs are settings and actions the
// judge does not know. World thread only.
#pragma once

#include "ChatCommand.h"
#include "ObjectGuid.h"
#include "host/TbH3.h"

#include <string>
#include <vector>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace cmd
{
// Handles `.tb cmd <verb> ...` when `verb` is a tactical verb. False when it is not one (no reply written). When handled, true and
// `*outOk` says whether the command was accepted (a refusal has already been answered).
bool TryHandleTacticalVerb(ChatHandler* handler, h3::Words const& args, std::string const& verb, ObjectGuid lead,
    std::vector<ObjectGuid> const& members, Player* sender, bool* outOk = nullptr);
}
}
