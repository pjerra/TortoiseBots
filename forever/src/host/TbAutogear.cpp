#include "host/TbAutogear.h"
#include "host/TbAutogearPure.h"
#include "host/player/TbPlayerCmd.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/gear/TbGearGen.h"
#include "host/gear/TbGearSourceTier.h"
#include "host/run/TbRunHooks.h"

#include "Bag.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

namespace
{
inline void LogPlayerAutogear(std::string const& line)
{
    TC_LOG_INFO("server.worldserver", "{}", line);
}
} // namespace

#include "compat/botpch_forever.h"
#include "compat/TbItemPrototype.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/policy/item/GearSetPolicy.h"
#include "runtime/GearItemLevelCapPolicy.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace autogear
{
namespace
{

struct AutogearUpgrade
{
    uint8 slot = 0;
    uint32 oldItemId = 0;
    uint32 newItemId = 0;
    bool mailed = false;
};

void Say(Player* player, std::string const& text)
{
    if (player::CaptureReply(text))
        return;
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());
    handler.SendSysMessage(text);
}

bool HasAmmoInBagsFor(Player* target, uint32 weaponSubClass, bool& outHasArrows, bool& outHasBullets)
{
    outHasArrows = false;
    outHasBullets = false;
    if (!target)
        return false;

    auto checkItem = [&](Item* item)
    {
        if (!item)
            return;
        ItemPrototype const* proto = tbcompat::ItemProto(item->GetEntry());
        if (!proto || proto->Class != ITEM_CLASS_PROJECTILE)
            return;
        if (proto->SubClass == ITEM_SUBCLASS_ARROW)
            outHasArrows = true;
        else if (proto->SubClass == ITEM_SUBCLASS_BULLET)
            outHasBullets = true;
    };

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* bagItem = target->GetBagByPos(bag);
        if (!bagItem)
            continue;
        for (uint32 slot = 0; slot < bagItem->GetBagSize(); ++slot)
            checkItem(bagItem->GetItemByPos(slot));
    }
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        checkItem(target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    if (weaponSubClass == ITEM_SUBCLASS_WEAPON_GUN)
        return outHasBullets;
    if (weaponSubClass == ITEM_SUBCLASS_WEAPON_BOW || weaponSubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW)
        return outHasArrows;
    return true;
}

RangedAmmoKind RangedKindOf(uint32 itemClass, uint32 subClass)
{
    if (itemClass != ITEM_CLASS_WEAPON)
        return RangedAmmoKind::NoAmmo;
    if (subClass == ITEM_SUBCLASS_WEAPON_BOW || subClass == ITEM_SUBCLASS_WEAPON_CROSSBOW)
        return RangedAmmoKind::Arrow;
    if (subClass == ITEM_SUBCLASS_WEAPON_GUN)
        return RangedAmmoKind::Bullet;
    return RangedAmmoKind::NoAmmo;
}

bool StoreOrMailReplacedItem(Player* target, Item* oldItem, bool allowMail, bool& outMailed)
{
    outMailed = false;
    if (!target || !oldItem)
        return true;

    uint8 const bag = oldItem->GetBagSlot();
    uint8 const slot = oldItem->GetSlot();
    ItemPosCountVec dest;
    if (target->CanStoreItem(NULL_BAG, NULL_SLOT, dest, oldItem, false) == EQUIP_ERR_OK)
    {
        target->RemoveItem(bag, slot, true);
        target->StoreItem(dest, oldItem, true);
        return true;
    }

    if (!allowMail)
        return false;

    target->MoveItemFromInventory(bag, slot, true);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    oldItem->DeleteFromInventoryDB(trans);
    oldItem->SaveToDB(trans);
    MailDraft("Replaced by autogear", "Your previous equipment is attached.")
        .AddItem(oldItem)
        .SendMailTo(trans, MailReceiver(target), MailSender(target, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
    CharacterDatabase.CommitTransaction(trans);
    outMailed = true;
    return true;
}

bool EquipUpgradeInSlot(Player* target, uint8 slot, uint32 itemId, bool allowMail, AutogearUpgrade& outUpgrade)
{
    Item* existing = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (existing && existing->GetEntry() == itemId)
        return false;

    uint16 dest = 0;
    if (!existing)
    {
        if (target->CanEquipNewItem(slot, dest, itemId, false) != EQUIP_ERR_OK)
            return false;
        Item* worn = target->EquipNewItem(dest, itemId, ItemContext::NONE, true);
        Item* check = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!worn || !check || check->GetEntry() != itemId)
            return false;
        outUpgrade = { slot, 0, itemId, false };
        return true;
    }

    if (target->CanEquipNewItem(slot, dest, itemId, true) != EQUIP_ERR_OK)
        return false;

    uint32 const oldId = existing->GetEntry();
    bool mailed = false;
    if (!StoreOrMailReplacedItem(target, existing, allowMail, mailed))
        return false;

    uint16 emptyDest = 0;
    if (target->CanEquipNewItem(slot, emptyDest, itemId, false) != EQUIP_ERR_OK)
        return false;
    Item* worn = target->EquipNewItem(emptyDest, itemId, ItemContext::NONE, true);
    Item* check = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!worn || !check || check->GetEntry() != itemId)
        return false;

    outUpgrade = { slot, oldId, itemId, mailed };
    return true;
}

uint32 ScoreEquippedSlot(Player* target, uint8 playerClass, uint32 specId, uint8 slot)
{
    Item* existing = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!existing)
        return 0;
    ItemPrototype const* proto = tbcompat::ItemProto(existing->GetEntry());
    if (!proto)
        return 0;
    ItemSpecType itSpec;
    return sRandomItemMgr.CalculateStatWeight(playerClass, static_cast<uint8>(specId), proto, itSpec);
}

void RunAutogearPass(Player* target,
                     uint32 specId,
                     std::string const& canonicalSpec,
                     uint32 maxQuality,
                     uint32 ilvlCap,
                     bool ammoAwareRanged,
                     bool allowMail,
                     std::vector<AutogearUpgrade>& outUpgrades)
{
    outUpgrades.clear();
    gear::InitGearIndex();

    uint8 const playerClass = target->GetClass();
    uint32 const level = std::clamp<uint32>(target->GetLevel(), 1, 60);
    bool const canDualWield = target->CanDualWield();

    bool hasArrows = false;
    bool hasBullets = false;
    if (ammoAwareRanged)
        HasAmmoInBagsFor(target, 0, hasArrows, hasBullets);

    bool mainHandIsTwoHanded = false;
    std::vector<uint32> alreadyPicked;

    for (uint8 slot : ai::GearSetSlotOrder())
    {
        if (slot == EQUIPMENT_SLOT_OFFHAND && mainHandIsTwoHanded)
        {
            if (Item* oldOh = target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            {
                uint32 const oldId = oldOh->GetEntry();
                bool mailed = false;
                if (StoreOrMailReplacedItem(target, oldOh, allowMail, mailed))
                    outUpgrades.push_back({ slot, oldId, 0, mailed });
            }
            continue;
        }

        Item* existing = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemPrototype const* curProto = existing ? tbcompat::ItemProto(existing->GetEntry()) : nullptr;
        uint32 const currentScore = ScoreEquippedSlot(target, playerClass, specId, slot);
        RangedAmmoKind const currentRanged = curProto ? RangedKindOf(curProto->Class, curProto->SubClass) : RangedAmmoKind::Empty;

        std::vector<ai::GearItemCandidate> candidates;
        for (ai::GearItemCandidate const& c : gear::SlotCandidates(slot))
        {
            if (!CandidateFitsAutogearCaps(c.quality, c.itemLevel, maxQuality, ilvlCap))
                continue;
            if (!ai::CandidateFitsSlotPolicy(c, playerClass, canonicalSpec, level, slot, static_cast<uint8>(maxQuality), canDualWield, mainHandIsTwoHanded))
                continue;
            if (!gear::SourceGateAllows(c.itemId))
                continue;
            if (std::find(alreadyPicked.begin(), alreadyPicked.end(), c.itemId) != alreadyPicked.end())
                continue;
            if (c.unique && target->HasItemCount(c.itemId, 1, true))
                continue;
            candidates.push_back(c);
        }

        for (ai::GearItemCandidate& c : candidates)
        {
            if (ItemPrototype const* proto = tbcompat::ItemProto(c.itemId))
            {
                ItemSpecType itSpec;
                c.score = sRandomItemMgr.CalculateStatWeight(playerClass, static_cast<uint8>(specId), proto, itSpec);
            }
        }
        ai::SortCandidatesForSlot(candidates);

        bool const isRanged = (slot == EQUIPMENT_SLOT_RANGED);
        for (ai::GearItemCandidate const& cand : candidates)
        {
            RangedAmmoKind const candRanged = RangedKindOf(cand.itemClass, cand.subClass);
            if (ammoAwareRanged)
            {
                if (!ShouldEquipAutogearUpgrade(existing != nullptr, currentScore, cand.score, isRanged, currentRanged, candRanged, hasArrows, hasBullets))
                    continue;
            }
            else
            {
                if (!ShouldEquipAutogearUpgrade(existing != nullptr, currentScore, cand.score, false, RangedAmmoKind::Empty, RangedAmmoKind::Empty, false, false))
                    continue;
            }

            AutogearUpgrade upgrade;
            if (EquipUpgradeInSlot(target, slot, cand.itemId, allowMail, upgrade))
            {
                outUpgrades.push_back(upgrade);
                alreadyPicked.push_back(cand.itemId);
                if (slot == EQUIPMENT_SLOT_MAINHAND && cand.inventoryType == INVTYPE_2HWEAPON)
                    mainHandIsTwoHanded = true;
                break;
            }
        }
    }

    if (!outUpgrades.empty())
        target->SaveToDB();
}

void WarnMissingRangedAmmo(Player* caller, Player* target, bool ownedBot)
{
    Item const* ranged = target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    ItemPrototype const* rangedProto = ranged ? tbcompat::ItemProto(ranged->GetEntry()) : nullptr;
    if (!rangedProto || rangedProto->Class != ITEM_CLASS_WEAPON)
        return;

    bool hasArrows = false;
    bool hasBullets = false;
    if (HasAmmoInBagsFor(target, rangedProto->SubClass, hasArrows, hasBullets))
        return;

    bool const gun = (rangedProto->SubClass == ITEM_SUBCLASS_WEAPON_GUN);
    std::string const ammoWord = gun ? "bullets" : "arrows";
    if (ownedBot)
    {
        Say(caller, "Warning: " + target->GetName() + " has " + rangedProto->Name1 + " equipped but no " + ammoWord +
                        " in the bags - auto shot will not fire until " + target->GetName() +
                        " gets some (owned-bot maintenance or hand them over).");
    }
    else
    {
        Say(caller, "Warning: " + target->GetName() + " has " + rangedProto->Name1 + " equipped but no " + ammoWord +
                        " in the bags - auto shot will not fire until they get some.");
    }
}

bool HandlePlayerAutogear(Player* caller, std::string_view rest)
{
    if (!caller)
        return false;

    std::vector<std::string> const tokens = SplitWords(rest);
    if (tokens.empty())
    {
        Say(caller, "Usage: .autogear [player name] <spec> [ilvl]");
        return true;
    }

    std::string nameToken;
    std::string specToken;
    uint32 requestedIlvl = 0;
    if (!ParsePlayerAutogearArgs(tokens, nameToken, specToken, requestedIlvl))
    {
        Say(caller, "Usage: .autogear [player name] <spec> [ilvl]");
        return true;
    }

    Player* target = nullptr;
    if (!nameToken.empty())
    {
        std::string normalized = nameToken;
        if (normalizePlayerName(normalized))
        {
            if (ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(normalized); !guid.IsEmpty())
                target = ObjectAccessor::FindConnectedPlayer(guid);
        }
        if (!target || !target->IsInWorld())
        {
            Say(caller, "Player '" + nameToken + "' is not online.");
            return true;
        }
    }
    else
    {
        Unit* selected = caller->GetSelectedUnit();
        target = selected ? selected->ToPlayer() : nullptr;
        if (!target)
            target = caller;
    }

    if (BotMgr::Instance().IsBot(target->GetGUID()))
    {
        Say(caller, "'" + target->GetName() + "' is a bot - owned bots are geared with .bot autogear.");
        return true;
    }

    bool const shapeshifted = (target->GetClass() == CLASS_DRUID && target->GetShapeshiftForm() != FORM_NONE);
    switch (DecidePlayerAutogearTargetState(target->IsAlive(), target->IsInCombat(), shapeshifted))
    {
        case PlayerAutogearTargetState::Dead:
            Say(caller, "'" + target->GetName() + "' must be alive.");
            return true;
        case PlayerAutogearTargetState::InCombat:
            Say(caller, "'" + target->GetName() + "' must be out of combat.");
            return true;
        case PlayerAutogearTargetState::Shapeshifted:
            Say(caller, "'" + target->GetName() + "' must leave the shapeshift form first.");
            return true;
        case PlayerAutogearTargetState::Ready:
            break;
    }

    bool const callerIsGm = caller->GetSession() && caller->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    switch (DecidePlayerAutogearPermission(callerIsGm, sPlayerbotAIConfig.playerAutogearForAll, target == caller, false))
    {
        case PlayerAutogearPermission::Allowed:
            break;
        case PlayerAutogearPermission::GmOnly:
            Say(caller, "Autogear is GM-only (AiPlayerbot.PlayerAutogearForAll = 1 lets every player gear themselves).");
            return true;
        case PlayerAutogearPermission::NotSelf:
            Say(caller, "You may only gear yourself with .autogear.");
            return true;
        case PlayerAutogearPermission::ChallengeGmOnly:
            Say(caller, "'" + target->GetName() + "' is on a challenge mode - only a GM may gear that character.");
            return true;
    }

    uint8 const classId = target->GetClass();
    std::string const scale = MatchAutogearSpec(classId, specToken);
    uint32 const specId = scale.empty() ? 0 : sRandomItemMgr.GetSpecIdFor(classId, scale);
    if (!specId)
    {
        std::string const valid = ListAutogearSpecs(classId);
        Say(caller, "Unknown spec '" + specToken + "'" + (target == caller ? "" : " for that class") +
                        ". Valid specs: " + (valid.empty() ? "none for this class" : valid));
        return true;
    }

    uint32 const defaultCeiling = GearItemLevelCap(target->GetLevel(),
        sPlayerbotAIConfig.randomGearMaxItemLevelAboveLevel, sPlayerbotAIConfig.randomGearMaxLevel);
    PlayerAutogearIlvl const ilvl = ResolvePlayerAutogearIlvl(target->GetLevel(), kAutogearMaxLevel, requestedIlvl, defaultCeiling);
    if (ilvl.clamped)
    {
        Say(caller, "Item level " + std::to_string(requestedIlvl) + " is above the ceiling for level " +
                        std::to_string(target->GetLevel()) + "; using " + std::to_string(defaultCeiling) + ".");
    }

    bool const raisedCeiling = (ilvl.effective > defaultCeiling);
    uint32 const maxQuality = AutogearMaxQuality(target->GetLevel(), kAutogearMaxLevel, raisedCeiling);

    std::vector<AutogearUpgrade> upgrades;
    RunAutogearPass(target, specId, ai::CanonicalSpecName(classId, scale), maxQuality, ilvl.effective, false, true, upgrades);

    WarnMissingRangedAmmo(caller, target, false);

    PlayerAutogearRunCounts counts;
    for (AutogearUpgrade const& u : upgrades)
        CountPlayerAutogearRecord(counts, u.oldItemId, u.newItemId, u.mailed);

    if (upgrades.empty())
    {
        Say(caller, "No upgrades for " + target->GetName() + " (" + specToken + ") at item level " +
                        std::to_string(ilvl.effective) + " - nothing changed.");
    }
    else
    {
        Say(caller, "Geared " + target->GetName() + " as '" + specToken + "': " +
                        std::to_string(counts.slotsEquipped) + " slot(s) equipped, " +
                        std::to_string(counts.replaced - counts.mailed) + " replaced item(s) to the bags, " +
                        std::to_string(counts.mailed) + " mailed.");
    }

    std::ostringstream logLine;
    logLine << "PlayerAutogear: caller=" << caller->GetName() << " target=" << target->GetName()
            << " spec=" << scale << " ilvl=" << ilvl.effective;
    for (AutogearUpgrade const& u : upgrades)
        logLine << " [" << uint32(u.slot) << "] " << u.oldItemId << "->" << u.newItemId
                << (u.mailed ? " (mailed)" : "");
    LogPlayerAutogear(logLine.str());
    return true;
}

class tortoisebots_autogear_cmd : public PlayerScript
{
public:
    tortoisebots_autogear_cmd() : PlayerScript("tortoisebots_autogear_cmd") { }

    bool OnCommand(Player* player, std::string_view command) override
    {
        std::string_view rest;
        if (!ClaimsAutogearCommand(command, rest))
            return false;
        if (!BotMgr::Instance().Enabled() || !engine::Built())
            return false;
        return HandlePlayerAutogear(player, rest);
    }
};

} // namespace

bool HandleBotAutogear(Player* caller, bool callerGm, player::OwnerScope const& scope, std::string_view tail)
{
    if (!caller)
        return false;

    std::string botToken;
    OwnedBotAutogearRequest parsed;
    ParseBotAutogearTail(tail, sPlayerbotAIConfig.ownedBotAutogearQualityCap, sPlayerbotAIConfig.ownedBotAutogearIlvlCap, botToken, parsed);

    if (botToken.empty())
    {
        Say(caller, "Usage: .bot autogear <online bot name> [quality|ilvl] (same account only)");
        return true;
    }

    std::string normalized = botToken;
    if (!normalizePlayerName(normalized))
    {
        Say(caller, "Usage: .bot autogear <online bot name> [quality|ilvl] (same account only)");
        return true;
    }

    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByName(normalized);
    Player* bot = entry ? ObjectAccessor::FindConnectedPlayer(entry->Guid) : nullptr;
    if (!bot || !bot->IsInWorld() || !BotMgr::Instance().IsBot(bot->GetGUID()))
    {
        Say(caller, "Usage: .bot autogear <online bot name> [quality|ilvl] (same account only)");
        return true;
    }

    if (run::BotInRunningRun(bot))
    {
        Say(caller, "TortoiseBots: autogear: refused in-run: bot is in an active dungeon run.");
        return true;
    }

    if (!callerGm && !player::OwnsGameAccount(scope, entry->AccountId))
    {
        Say(caller, "TortoiseBots: autogear: refused not-yours: you may only autogear your own bots.");
        return true;
    }

    if (BotMgr::Instance().IsPoolBot(bot->GetGUID()))
    {
        Say(caller, "'" + bot->GetName() + "' is a pool bot and keeps progressing organically.");
        return true;
    }

    if (!sPlayerbotAIConfig.ownedBotAutogearEnabled)
    {
        Say(caller, "Owned-bot autogear is disabled (AiPlayerbot.OwnedBotAutogearEnabled = 0).");
        return true;
    }

    if (!bot->IsAlive())
    {
        Say(caller, "Bot '" + bot->GetName() + "' must be alive.");
        return true;
    }

    if (!parsed.ok)
    {
        Say(caller, "Unknown autogear option. Use a quality (white, green, blue, purple) or an item level number.");
        return true;
    }

    uint8 const classId = bot->GetClass();
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(classId);
    std::string const canonicalSpec = ai::CanonicalSpecName(classId, sRandomItemMgr.GetSpecName(specId));

    std::vector<AutogearUpgrade> upgrades;
    RunAutogearPass(bot, specId, canonicalSpec, parsed.quality, parsed.ilvlCap, true, false, upgrades);

    Say(caller, "Re-geared " + bot->GetName() + " within the configured caps.");
    WarnMissingRangedAmmo(caller, bot, true);
    return true;
}

void AddScripts()
{
    new tortoisebots_autogear_cmd();
}

} // namespace autogear
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace autogear
{

bool HandleBotAutogear(Player*, bool, player::OwnerScope const&, std::string_view) { return false; }
void AddScripts() { }

} // namespace autogear
} // namespace TortoiseBots

#endif
