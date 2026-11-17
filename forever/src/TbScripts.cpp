// Script registration for the module: the world tick, the config load and the `.tb` chat commands.
#include "TbBotMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "SpellInfo.h"

using namespace Trinity::ChatCommands;
using TortoiseBots::BotMgr;

namespace
{
ObjectGuid GuidByName(std::string name)
{
    if (!normalizePlayerName(name))
        return ObjectGuid::Empty;
    return sCharacterCache->GetCharacterGuidByName(name);
}

class tortoisebots_world : public WorldScript
{
public:
    tortoisebots_world() : WorldScript("tortoisebots_world") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        BotMgr::Instance().LoadConfig();
    }

    void OnWorldInitialized() override
    {
        BotMgr::Instance().LoadConfig();
        TC_LOG_INFO("server.loading", "TortoiseBots (ForeverCore port, spike): {}", BotMgr::Instance().Enabled() ? "enabled" : "disabled");
    }

    void OnBeforeMapsUpdate(uint32 diff) override
    {
        BotMgr::Instance().Update(diff);
    }

    void OnShutdown() override
    {
        // Save and remove every bot while the world is still up.
        BotMgr::Instance().LogoutAll("server shutdown");
        BotMgr::Instance().Update(0);
    }
};

class tortoisebots_player : public PlayerScript
{
public:
    tortoisebots_player() : PlayerScript("tortoisebots_player") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (BotMgr::Instance().IsBot(player->GetGUID()))
            TC_LOG_INFO("tortoisebots", "bot {} entered the world on map {}", player->GetName(), player->GetMapId());
    }

    void OnLogout(Player* player) override
    {
        if (BotMgr::Instance().IsBot(player->GetGUID()))
            TC_LOG_INFO("tortoisebots", "bot {} left the world", player->GetName());
    }
};

class tortoisebots_commands : public CommandScript
{
public:
    tortoisebots_commands() : CommandScript("tortoisebots_commands") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable tbTable =
        {
            { "login",  HandleLogin,  rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "logout", HandleLogout, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "cast",   HandleCast,   rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "status", HandleStatus, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "tb", tbTable },
        };
        return commandTable;
    }

    // .tb login <character>: the character joins the world as a bot and follows the caller.
    static bool HandleLogin(ChatHandler* handler, std::string name)
    {
        std::string const error = BotMgr::Instance().Login(GuidByName(name), handler->GetPlayer());
        if (!error.empty())
        {
            handler->PSendSysMessage("TortoiseBots: %s not logged in: %s.", name.c_str(), error.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->PSendSysMessage("TortoiseBots: %s is logging in.", name.c_str());
        return true;
    }

    // .tb logout <character>
    static bool HandleLogout(ChatHandler* handler, std::string name)
    {
        if (!BotMgr::Instance().Logout(GuidByName(name), "command"))
        {
            handler->PSendSysMessage("TortoiseBots: %s is not a logged-in bot.", name.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->PSendSysMessage("TortoiseBots: %s is logging out.", name.c_str());
        return true;
    }

    // .tb cast <character> <spell id>: the bot casts the spell on the caller's target (on the caller with no target).
    static bool HandleCast(ChatHandler* handler, std::string name, uint32 spellId)
    {
        Player* bot = BotMgr::Instance().FindBot(GuidByName(name));
        if (!bot)
        {
            handler->PSendSysMessage("TortoiseBots: %s is not a logged-in bot.", name.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (!sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
        {
            handler->PSendSysMessage("TortoiseBots: spell %u does not exist.", spellId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* caller = handler->GetPlayer();
        Unit* target = caller->GetSelectedUnit();
        if (!target)
            target = caller;

        SpellCastResult const result = bot->CastSpell(target, spellId);
        handler->PSendSysMessage("TortoiseBots: %s casts %u on %s: result %u (0 = ok).", name.c_str(), spellId,
            target->GetName().c_str(), uint32(result));
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("TortoiseBots: %s, %u bot session(s).", BotMgr::Instance().Enabled() ? "enabled" : "disabled",
            uint32(BotMgr::Instance().Count()));
        return true;
    }
};
}

void AddSC_tortoisebots()
{
    new tortoisebots_world();
    new tortoisebots_player();
    new tortoisebots_commands();
}
