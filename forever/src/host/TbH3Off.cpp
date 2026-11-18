// Slice 3 (M): the default (option-OFF) build's answer for the nine group-fight commands. Their real handlers (TbH3.cpp, TbOrders.cpp,
// TbPartyState.cpp, TbTestKit.cpp) are engine-facing code and belong to the TORTOISEBOTS_SLICE1 build only (CMakeLists.txt filters them
// out of this one and filters THIS file out of the slice build). Every command answers the same refusal the real handlers give when
// engine::Built() is false: `error=engine-not-built cmd=<name> msg=... [#token]` (forever/docs/TB-COMMANDS.md section 3).
#include "host/TbH3.h"

#include "Chat.h"

#include <cctype>

namespace TortoiseBots
{
namespace h3
{
namespace
{
// The echo token: the last word when it starts with '#', 2-33 characters of [A-Za-z0-9_.:-] after it (as TbH3.cpp Split).
std::string TokenOf(std::string_view tail)
{
    size_t end = tail.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tail[start - 1])))
        --start;
    std::string_view const word = tail.substr(start, end - start);
    if (word.size() < 2 || word.size() > 33 || word[0] != '#')
        return {};
    for (size_t i = 1; i < word.size(); ++i)
    {
        char const c = word[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != ':' && c != '-')
            return {};
    }
    return std::string(word);
}

bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd)
{
    std::string line = std::string("error=engine-not-built cmd=") + cmd +
        " msg=the_engine_is_not_built_in_(this_build_has_no_group-fight_commands)";
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}
}

bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "order"); }
bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "role"); }
bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "master"); }
bool HandleParty(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "party"); }
bool HandleState(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "state"); }
bool HandleThreat(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "threat"); }
bool HandleLearn(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "learn"); }
bool HandleEquip(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "equip"); }
bool HandleItemScan(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "itemscan"); }
}
}
