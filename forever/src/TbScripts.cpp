// Script registration for the module: the world tick, the config load and the `.tb` chat commands.
#include "TbBotMgr.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSpellEntry.h"
#include "AccountMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Unit.h"
#include "WorldSession.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include <bitset>
#include <list>
#include <string>
#include <unordered_set>
#include "ChatCommand.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "DB2Stores.h"

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
        // The spell store is loaded too (World.cpp:1471). This hook is what tells the mirror so: it answers 0 and null
        // without touching the store until the flag is set, so a lookup from any earlier hook cannot index a half-loaded
        // store into a table that is never rebuilt.
        tbcompat::MarkSpellStoreReady();
        tbcompat::InitSpellTable();
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
            { "spellscan", HandleSpellScan, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "spell",  HandleSpell,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
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

    // .tb spellscan: build the spell mirror for every DIFFICULTY_NONE spell the core has, and report what it cost and
    // what the mirror could not hold. Read-only. An assert anywhere in the core accessors the fill makes would abort
    // here, which is what the check is for.
    static bool HandleSpellScan(ChatHandler* handler)
    {
        uint32 const started = getMSTime();
        uint32 spells = 0, incomplete = 0, moreEffects = 0, multiSchool = 0, effects = 0;
        uint32 nonNoneRecords = 0;
        std::string firstTen, moreEffectsIds;
        std::unordered_set<uint32> nonNoneIds, nonNoneOnlyIds;

        sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
        {
            if (!info)
                return;

            // The slot holds only DIFFICULTY_NONE records; the other variants are counted, and so are the ids that have
            // no NONE record at all (SpellProto answers those with nullptr).
            if (info->Difficulty != DIFFICULTY_NONE)
            {
                ++nonNoneRecords;
                if (nonNoneIds.insert(info->Id).second)
                {
                    SpellInfo const* none = sSpellMgr->GetSpellInfo(info->Id, DIFFICULTY_NONE);
                    if (!none || none->Difficulty != DIFFICULTY_NONE)
                        nonNoneOnlyIds.insert(info->Id);
                }
                return;
            }

            SpellEntry const* entry = tbcompat::SpellProto(info);
            if (!entry)
                return;
            ++spells;
            effects += uint32(info->GetEffects().size());
            if (entry->MoreEffects)
            {
                ++moreEffects;
                if (moreEffects <= 5)
                    moreEffectsIds += (moreEffectsIds.empty() ? "" : ", ") + std::to_string(entry->Id) + " (" + std::to_string(entry->MoreEffects) + ")";
            }
            if (std::bitset<32>(info->SchoolMask).count() > 1)
                ++multiSchool;
            if (entry->Incomplete)
            {
                ++incomplete;
                if (incomplete <= 10)
                    firstTen += (firstTen.empty() ? "" : "; ") + std::to_string(entry->Id) + " (" + entry->IncompleteReason + ")";
            }
        });

        std::string firstNonNoneOnly;
        for (uint32 id : nonNoneOnlyIds)
        {
            if (!firstNonNoneOnly.empty())
                firstNonNoneOnly += ", ";
            if (firstNonNoneOnly.size() < 200)
                firstNonNoneOnly += std::to_string(id);
            else
            {
                firstNonNoneOnly += "...";
                break;
            }
        }

        uint64 const indexBytes = tbcompat::SpellIndexBytes();
        uint64 const slotBytes = tbcompat::SpellSlotBytes();
        uint32 const ms = getMSTimeDiff(started, getMSTime());
        handler->PSendSysMessage("TortoiseBots: spell mirrors: %u spells in %u ms, spell id bound %u (index %llu B + slots %llu B), %u incomplete%s",
            spells, ms, tbcompat::MaxSpellId(), (unsigned long long)indexBytes, (unsigned long long)slotBytes,
            incomplete, incomplete ? (" (first: " + firstTen + ")").c_str() : "");
        handler->PSendSysMessage("TortoiseBots: effects: %u effect slots in all, %u spells with more than 3 effects%s, %u spells with more than one school.",
            effects, moreEffects, moreEffects ? (" (first: " + moreEffectsIds + ")").c_str() : "", multiSchool);
        handler->PSendSysMessage("TortoiseBots: difficulty: %u SpellInfo records with a difficulty other than NONE, %u of their ids have no DIFFICULTY_NONE record (mirror null; %u such lookups counted%s).",
            nonNoneRecords, uint32(nonNoneOnlyIds.size()), tbcompat::NonNoneOnlyLookups(),
            nonNoneOnlyIds.empty() ? "" : (" (first: " + firstNonNoneOnly + ")").c_str());
        return true;
    }

    // .tb spell <id>: the fields the spell mirror (TbSpellEntry.h) holds for one spell, for comparing it with the client's data.
    static bool HandleSpell(ChatHandler* handler, uint32 spellId)
    {
        SpellEntry const* entry = tbcompat::SpellProto(spellId);
        if (!entry)
            return Fail(handler, "spell " + std::to_string(spellId) + " does not exist.");

        if (entry->Incomplete)
            handler->PSendSysMessage("TortoiseBots: INCOMPLETE: %s", entry->IncompleteReason.c_str());

        char const* name = entry->SpellName[LOCALE_enUS].empty() ? "?" : entry->SpellName[LOCALE_enUS].c_str();
        char const* rank = entry->Rank[LOCALE_enUS].empty() ? "" : entry->Rank[LOCALE_enUS].c_str();

        // The range the bot looks up from rangeIndex, the same lookup (SERVER side: SpellRange.dbc).
        float minRange = 0.0f, maxRange = 0.0f;
        if (SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(entry->rangeIndex))
        {
            minRange = range->RangeMin[0];
            maxRange = range->RangeMax[0];
        }

        handler->PSendSysMessage("TortoiseBots: spell %u \"%s %s\": school %u (mask 0x%X), category %u, cast time %u ms, range %.1f-%.1f (idx %u), duration %d ms, levels %u/%u/%u (base/max/spell), recovery %u/%u ms, GCD %u ms (category %u), mana %u (%u%%), power %u.",
            entry->Id, name, rank, entry->School, entry->Core ? entry->Core->SchoolMask : 0u, entry->Category,
            entry->GetCastTime(nullptr), minRange, maxRange, entry->rangeIndex, entry->GetDuration(),
            entry->baseLevel, entry->maxLevel, entry->spellLevel,
            entry->RecoveryTime, entry->CategoryRecoveryTime, entry->StartRecoveryTime, entry->StartRecoveryCategory,
            entry->manaCost, entry->ManaCostPercentage, entry->powerType);

        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (!entry->Effect[i] && !entry->EffectApplyAuraName[i])
                continue;
            handler->PSendSysMessage("TortoiseBots: effect %u: type %u, aura %u, base points %d + 1, die sides %d, misc %d, target A/B %u/%u, radius %u, trigger %u, item %u, chain %u, amp %.0f, points/level %.2f.",
                i, entry->Effect[i], entry->EffectApplyAuraName[i], entry->EffectBasePoints[i], entry->EffectDieSides[i],
                entry->EffectMiscValue[i], entry->EffectImplicitTargetA[i], entry->EffectImplicitTargetB[i],
                entry->EffectRadiusIndex[i], entry->EffectTriggerSpell[i], entry->EffectItemType[i],
                entry->EffectChainTarget[i], entry->Core && i < entry->Core->GetEffects().size() ? entry->Core->GetEffects()[i].Amplitude : 0.0f,
                entry->EffectRealPointsPerLevel[i]);
        }

        handler->PSendSysMessage("TortoiseBots: targets 0x%X, family %u flags 0x%llX, attributes 0x%X/0x%X/0x%X, dispel %u, mechanic %u, stack %u, proc %u x%u, reagents %d/%u totem %u/%u, max targets %u, more effects %u.",
            entry->Targets, entry->SpellFamilyName, (unsigned long long)entry->SpellFamilyFlags, entry->Attributes,
            entry->AttributesEx, entry->AttributesEx2, entry->Dispel, entry->Mechanic, entry->StackAmount,
            entry->procChance, entry->procCharges, entry->Reagent[0], entry->ReagentCount[0], entry->Totem[0],
            entry->Totem[1], entry->MaxAffectedTargets, entry->MoreEffects);
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
