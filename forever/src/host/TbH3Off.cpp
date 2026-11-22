// Slice 3 (M): the default (option-OFF) build's answer for the nine group-fight commands. Their real handlers (TbH3.cpp, TbOrders.cpp,
// TbPartyState.cpp, TbTestKit.cpp) are engine-facing code and belong to the TORTOISEBOTS_SLICE1 build only (CMakeLists.txt filters them
// out of this one and filters THIS file out of the slice build). Every command answers the same refusal the real handlers give when
// engine::Built() is false: `error code=engine-not-built cmd=<name> msg=<percent-encoded> [#token]` (grammar version 2, forever/docs/TB-COMMANDS.md).
#include "host/TbH3.h"
#include "host/TbHostLfgFill.h"
#include "host/TbRaidLayout.h"

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
} // namespace

// Declared in TbH3.h: the default build's `ai`/`cs` commands answer through it too (TbAddonAi.cpp).
bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd)
{
    // The same percent-encoding as the real writers (TbH3.cpp Enc): every byte outside [A-Za-z0-9._-] as %XX.
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = std::string("error code=engine-not-built cmd=") + cmd + " msg=" + msg;
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}

bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "order"); }
bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "role"); }
bool HandleSpec(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "spec"); }
bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "master"); }
bool HandleParty(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "party"); }
bool HandleLayout(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "layout"); }
void ForgetLayout(Group*) {}
void NoteLayoutMemberLeft(Group*, ObjectGuid) {}
bool RaidConvertBlocked(Group*, std::string&) { return false; }
bool HandleOnyxiaOff(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "onyxia"); }
bool HandleState(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "state"); }
bool HandleThreat(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "threat"); }
bool HandleSchool(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "school"); }
bool HandleLearn(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "learn"); }
bool HandleEquip(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "equip"); }
bool HandleRemoveItem(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "removeitem"); }
bool HandleItemScan(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "itemscan"); }
bool HandleStatCensus(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "statcensus"); }
bool HandleGear(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "gear"); }
bool HandleQuest(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "quest"); }
bool HandleGuildOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "guildorder"); }
bool HandleCraftOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "craftorder"); }
bool MayLeadBots(ChatHandler* /*handler*/, ObjectGuid /*character*/) { return true; }   // N5 is an engine-build rule: the default build keeps the older commands as they were
bool CheckBotAccess(Player const* /*sender*/, ObjectGuid /*botGuid*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
bool CheckBotAccess(ChatHandler* /*handler*/, ObjectGuid /*botGuid*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
bool CheckBotAccess(ChatHandler* /*handler*/, Player const* /*bot*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
bool CheckPartyLeadAccess(Player const* /*sender*/, Player const* /*leader*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
bool CheckPartyLeadAccess(ChatHandler* /*handler*/, Player const* /*leader*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
// proof-1 A: the `.tb run` observer guard is an engine-build rule like CheckBotAccess, and in this build it allows like the others.
bool CheckRunObserverAccess(ChatHandler* /*handler*/, Player const* /*bot*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
bool CheckRunObserverAccess(ChatHandler* /*handler*/, ObjectGuid /*botGuid*/, std::string& /*code*/, std::string& /*detail*/) { return true; }
// TB-1: `.tb hurt` and `.tb power` (TbScripts.cpp, compiled in both builds) stop at RequireEngine before they ask who the target is, and
// `.tb role`, `.tb equip` and `.tb learn` are the NotBuilt stubs above, so this rule is never reached here. The symbol has to exist for the
// option-OFF link all the same; it answers what the real handlers answer without the engine.
Player* ResolveTestTarget(ChatHandler* /*handler*/, std::string_view /*name*/, std::string& code, std::string& detail)
{
    code = "engine-not-built";
    detail = "the engine is not built in (this build has no group-fight commands)";
    return nullptr;
}
bool HandleGrammar(ChatHandler* handler, Trinity::ChatCommands::Tail args)  { return NotBuilt(handler, args, "grammar"); }
}
namespace cmd
{
bool HandleCmd(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::NotBuilt(handler, args, "cmd");
}
}
namespace chatin
{
// S1-R2: `.tb hear` needs an engine brain to deliver to; the hook itself is inert in this build (TbChatIn.cpp).
bool HandleHear(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::NotBuilt(handler, args, "hear");
}
}
namespace lfg
{
// The fill needs the engine's free bots; this build has none.
bool HandleLfg(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::NotBuilt(handler, args, "lfg");
}
void RegisterScripts() { }
}
}
