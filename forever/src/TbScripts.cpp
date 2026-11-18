// Script registration for the module: the world tick, the config load and the `.tb` chat commands.
#include "TbBotMgr.h"
#include "compat/TbItemPrototype.h"
#include "AccountMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Unit.h"
#include "WorldSession.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include <list>
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
        // The item store is loaded by now (World.cpp:1608, and item_classic_block at :1631); fill every mirror once so the
        // bot brain looks items up without a lock (TbItemPrototype.cpp).
        tbcompat::FillAll();
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
            { "create", HandleCreate, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "login",  HandleLogin,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "logout", HandleLogout, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "cast",   HandleCast,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "go",     HandleGo,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "attack", HandleAttack, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "where",  HandleWhere,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "status", HandleStatus, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "item",   HandleItem,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "tb", tbTable },
        };
        return commandTable;
    }

    static Player* Caller(ChatHandler* handler)
    {
        return handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    }

    static bool Fail(ChatHandler* handler, std::string const& text)
    {
        handler->PSendSysMessage("TortoiseBots: %s", text.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    // .tb create <account> <name> <race id> <class id> [gender 0|1]: a new level 1 character for a bot.
    static bool HandleCreate(ChatHandler* handler, std::string account, std::string name, uint8 race, uint8 playerClass, Optional<uint8> gender)
    {
        if (!normalizePlayerName(name))
            return Fail(handler, "that name is not allowed.");
        std::string const error = BotMgr::Instance().CreateCharacter(AccountMgr::GetId(account), name, race, playerClass, gender.value_or(0));
        if (!error.empty())
            return Fail(handler, name + " not created: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s created.", name.c_str());
        return true;
    }

    // .tb login <character> [master]: the character joins the world as a bot. It follows <master>, or the caller when
    // no master is named and the command comes from a player. From the console without a master it stands still.
    static bool HandleLogin(ChatHandler* handler, std::string name, Optional<std::string> masterName)
    {
        ObjectGuid master;
        if (masterName)
        {
            master = GuidByName(*masterName);
            if (master.IsEmpty())
                return Fail(handler, "no character named " + *masterName + ".");
        }
        else if (Player* caller = Caller(handler))
            master = caller->GetGUID();

        std::string const error = BotMgr::Instance().Login(GuidByName(name), master);
        if (!error.empty())
            return Fail(handler, name + " not logged in: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s is logging in.", name.c_str());
        return true;
    }

    // .tb go <character> <dx> <dy>: the bot walks that many yards east/north of where it stands (no master only).
    static bool HandleGo(ChatHandler* handler, std::string name, float dx, float dy)
    {
        if (!BotMgr::Instance().WalkBy(GuidByName(name), dx, dy))
            return Fail(handler, name + " is not a logged-in bot.");
        handler->PSendSysMessage("TortoiseBots: %s walks.", name.c_str());
        return true;
    }

    // .tb attack <character>: the bot attacks the nearest thing it may attack within 40 yards.
    static bool HandleAttack(ChatHandler* handler, std::string name)
    {
        std::string const result = BotMgr::Instance().AttackNearest(GuidByName(name), 40.0f);
        handler->PSendSysMessage("TortoiseBots: %s: %s.", name.c_str(), result.c_str());
        return true;
    }

    // .tb where <character>: where the bot is and what it is doing.
    static bool HandleWhere(ChatHandler* handler, std::string name)
    {
        Player* bot = BotMgr::Instance().FindBot(GuidByName(name));
        if (!bot)
            return Fail(handler, name + " is not a logged-in bot.");
        Unit* victim = bot->GetVictim();
        handler->PSendSysMessage("TortoiseBots: %s level %u map %u at %.1f %.1f %.1f, health %u/%u, %s, victim %s (health %u).",
            name.c_str(), uint32(bot->GetLevel()), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            uint32(bot->GetHealth()), uint32(bot->GetMaxHealth()), bot->IsInCombat() ? "in combat" : "out of combat",
            victim ? victim->GetName().c_str() : "none", victim ? uint32(victim->GetHealth()) : 0u);

        // What the bot has around it: proves its part of the map is loaded and populated.
        std::list<Creature*> creatures;
        bot->GetCreatureListWithEntryInGrid(creatures, 0, 60.0f);
        Creature* nearest = nullptr;
        for (Creature* creature : creatures)
            if (creature->IsAlive() && (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest)))
                nearest = creature;
        if (nearest)
        {
            handler->PSendSysMessage("TortoiseBots: %u creature(s) within 60 yd, nearest alive %s at %.1f yd (may attack: %s, friendly: %s).",
                uint32(creatures.size()), nearest->GetName().c_str(), bot->GetDistance(nearest),
                bot->IsValidAttackTarget(nearest) ? "yes" : "no", bot->IsFriendlyTo(nearest) ? "yes" : "no");
            handler->PSendSysMessage("TortoiseBots: sees it: %s, bot unit flags 0x%X player flags 0x%X, target unit flags 0x%X, target faction %u, uninteractible: %s.",
                bot->CanSeeOrDetect(nearest) ? "yes" : "no", uint32(*bot->m_unitData->Flags), uint32(*bot->m_playerData->PlayerFlags),
                uint32(*nearest->m_unitData->Flags), nearest->GetFaction(), nearest->IsUninteractible() ? "yes" : "no");
        }
        else
            handler->PSendSysMessage("TortoiseBots: %u creature(s) within 60 yd, none alive.", uint32(creatures.size()));
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
            return Fail(handler, name + " is not a logged-in bot.");
        if (!sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
        {
            handler->PSendSysMessage("TortoiseBots: spell %u does not exist.", spellId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Target: the caller's selection; from the console the bot's own victim; otherwise the bot itself.
        Unit* target = nullptr;
        if (Player* caller = Caller(handler))
            target = caller->GetSelectedUnit() ? caller->GetSelectedUnit() : caller;
        else
            target = bot->GetVictim() ? bot->GetVictim() : bot;

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

    // .tb item <entry>: what the item mirror (TbItemPrototype.h) holds for one item, for comparing it with the database.
    static bool HandleItem(ChatHandler* handler, uint32 entry)
    {
        ItemPrototype const* proto = tbcompat::ItemProto(entry);
        if (!proto)
            return Fail(handler, "item " + std::to_string(entry) + " does not exist.");

        if (proto->Incomplete)
            handler->PSendSysMessage("TortoiseBots: INCOMPLETE: %s", proto->IncompleteReason.c_str());

        handler->PSendSysMessage("TortoiseBots: item %u \"%s\": class %u/%u, quality %u, inventory %u, item level %u, required level %u, buy %u sell %u, bag family %u (mask 0x%X).",
            proto->ItemId, proto->Name1.c_str(), proto->Class, proto->SubClass, proto->Quality, proto->InventoryType,
            proto->ItemLevel, proto->RequiredLevel, proto->BuyPrice, proto->SellPrice, proto->BagFamily, proto->BagFamilyMask);
        handler->PSendSysMessage("TortoiseBots: armor %u, block %u (base %u + stat %u, not added), damage %.1f-%.1f type %u, delay %u, bonding %u, max stack %u, durability %u.",
            uint32(proto->Armor), proto->Block, proto->BlockBase, proto->BlockStat, proto->Damage[0].DamageMin, proto->Damage[0].DamageMax,
            proto->Damage[0].DamageType, proto->Delay, proto->Bonding, proto->Stackable, proto->MaxDurability);

        std::string stats;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            if (proto->ItemStat[i].ItemStatValue)
                stats += (stats.empty() ? "" : ", ") + std::to_string(proto->ItemStat[i].ItemStatType) + ":" + std::to_string(proto->ItemStat[i].ItemStatValue);

        std::string spells;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            if (proto->Spells[i].SpellId)
                spells += (spells.empty() ? "" : ", ") + std::to_string(proto->Spells[i].SpellId) +
                    " (trigger " + std::to_string(proto->Spells[i].SpellTrigger) + ", charges " + std::to_string(proto->Spells[i].SpellCharges) + ")";

        handler->PSendSysMessage("TortoiseBots: stats %s; spells %s; resistances %u/%u/%u/%u/%u/%u.",
            stats.empty() ? "none" : stats.c_str(), spells.empty() ? "none" : spells.c_str(),
            uint32(proto->HolyRes), uint32(proto->FireRes), uint32(proto->NatureRes), uint32(proto->FrostRes),
            uint32(proto->ShadowRes), uint32(proto->ArcaneRes));
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
