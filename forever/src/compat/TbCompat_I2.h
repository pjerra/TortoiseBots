// ForeverCore side of the both-core helpers and names of slice 2, package I2 (loot, add loot and use item actions).
// Owned by package I2: no other package edits this file. Turtle side: ai/cmangos-compat-s2i.h.
// Notes: forever/docs/SLICE2-I2-NOTES.md (every helper below, with the file:line of the core call it makes).
#pragma once

#include "LootPackets.h"           // WorldPackets::Loot::LootUnit / LootRelease / LootMoney (the typed packets of the loot handlers)
#include "ItemUseCastPolicy.h"     // ShouldStopMovingForItemCast / ItemCastDurationMs (the action duration of an item use)
#include "../TbBotSession.h"     // TortoiseBots::BotSession, CastResult (the in-tick cast refusal of the item cast, R0 notes U1)

// ===============================================================================================================
// NAMES: the 1.12 spellings of constants the loot and item code uses
// ===============================================================================================================
// LootSlotType 0 "the player can loot the item" (Loot.h:158 LOOT_SLOT_TYPE_ALLOW_LOOT); T's LOOT_SLOT_NORMAL = 0 "can get".
#define LOOT_SLOT_NORMAL LOOT_SLOT_TYPE_ALLOW_LOOT
// The cheer text emote (T TEXTEMOTE_CHEER, F's TextEmotes TEXT_EMOTE_CHEER), like TEXTEMOTE_NOD in TbCompat_Names.h:173.
#define TEXTEMOTE_CHEER TEXT_EMOTE_CHEER

// Declared here, defined in TbCompat_S9.h (included after this file): LootOpenCompat tells the window memo that it raised the event itself.
inline void NoteLootWindowAnnouncedCompat(Player const* bot, ObjectGuid const& owner);

// ===============================================================================================================
// LOOT: money first (lead decision D2), items since slice 9 (package L9). The bot opens a corpse, reads its money from the core's Loot object, takes it and releases
// the window, through the core's own handlers (the calls a client's packets end in). Everything that takes ITEMS out of a window
// is the named feature "loot: items, quest items and rolls".
// Threading: the engine ticks on the world thread, before the maps update (World.cpp:2490, :2475). The loot handlers are
// PROCESS_THREADUNSAFE (Opcodes.cpp:689-693), i.e. the world thread is where the core itself runs them, and they read the map
// (ObjectAccessor::GetCreature, Cell::VisitGridObjects, GetMap()->GetCreature) which no map thread writes while the engine runs.
// ===============================================================================================================

// Open the loot window of a corpse: WorldSession::HandleLootOpcode(LootUnit&) (LootHandler.cpp:213-259). It checks, in this order,
// that the player is alive and the guid is a creature (:216), that the creature exists (:219), AELootCreatureCheck::IsValidLootTarget
// (:223: the corpse is dead, within 30 yards of the player (LootHandler.cpp:46,:55), and Player::isAllowedToLoot = this player is an
// allowed looter and has something to loot, Player.cpp:20370-20412), then interrupts a running cast (:228-229), and calls
// Player::SendLoot(*creature->GetLootForPlayer(player)) (:254), which puts the loot into the player's loot view (Player.cpp:10115-10142:
// SetLootGUID, m_AELootView[loot guid], UNIT_FLAG_LOOTING). A refusal sends NOTHING (the handler returns), so the answer here is read
// back from the loot view. With AE loot on (CONFIG_ENABLE_AE_LOOT) the core also opens up to 49 other corpses of the area (:233-251);
// their money is taken with the first one's (HandleLootMoneyOpcode walks the whole view) and Release below only closes the named one.
// The engine's "loot response" event (what a client's SMSG_LOOT_RESPONSE is on Turtle) is raised HERE, into the bot's own trigger
// (the trigger the packet handler table feeds on Turtle): its packet is the owner guid, which StoreLootAction reads back with
// LootWindowReadCompat. Nothing is needed from the host (NEEDS FROM: none). The window is not opened while the previous window's event
// has not been consumed yet (one event per trigger): the corpse stays and the caller counts the attempt as a failed open.
// Returns true when the corpse is in the player's loot view after the call.
template <class AiType>
inline bool LootOpenCompat(AiType* ai, Player* bot, ObjectGuid const& guid)
{
    auto* trigger = ai->GetAiObjectContext()->GetTrigger("loot response");
    if (!trigger)
        return false;                                   // the strategy that owns the event is not active: nothing would store the window
    auto pending = trigger->Check();
    if (!pending.getSource().empty())
        return false;                                   // the last window has not been stored yet

    WorldSession* session = bot->GetSession();
    if (!session)
        return false;

    WorldPackets::Loot::LootUnit packet{ WorldPacket(CMSG_LOOT_UNIT) };
    packet.Unit = guid;
    session->HandleLootOpcode(packet);

    if (!bot->GetLootByWorldObjectGUID(guid))
        return false;

    WorldPacket event;
    event << guid;
    NoteLootWindowAnnouncedCompat(bot, guid);
    trigger->ExternalEvent(event, nullptr);
    return true;
}

// Release a window: WorldSession::HandleLootReleaseOpcode(LootRelease&) (LootHandler.cpp:261-267) finds the loot by the OWNER guid in the
// player's view (Player::GetLootByWorldObjectGUID, Player.cpp:10023) and calls DoLootRelease (:269-404: SetLootGUID(empty), RemoveLooter,
// the view entry, UNIT_FLAG_LOOTING, and for a corpse that is fully looted the lootable flag). A guid with no window does nothing.
inline void LootReleaseCompat(Player* bot, ObjectGuid const& guid)
{
    WorldPackets::Loot::LootRelease packet{ WorldPacket(CMSG_LOOT_RELEASE) };
    packet.Unit = guid;
    bot->GetSession()->HandleLootReleaseOpcode(packet);
}

// Release EVERY window of the bot's loot view (R5 item 2). T: the named window only (T opens one window per corpse and has no area loot, so this is
// what it always did). F: with Loot.EnableAELoot on (World.cpp:725, default 1) HandleLootOpcode also opened up to 49 other corpses within 30 yards into
// m_AELootView and HandleLootMoneyOpcode took the money of the whole view, so after the money take every window in the view is closed through the same
// handler (LootReleaseCompat per owner guid; the owner guids are copied first, as the release erases from the view): the bot leaves every looters list,
// loses UNIT_FLAG_LOOTING, and the other corpses reach the looted-corpse cleanup on their normal timer. `guid` (the named corpse) is released first.
inline void LootReleaseAllCompat(Player* bot, ObjectGuid const& guid)
{
    std::vector<ObjectGuid> owners;
    for (std::pair<ObjectGuid const, Loot*> const& view : bot->GetAELootView())
        if (view.second && view.second->GetOwnerGUID() != guid)
            owners.push_back(view.second->GetOwnerGUID());
    LootReleaseCompat(bot, guid);
    for (ObjectGuid const& owner : owners)
        LootReleaseCompat(bot, owner);
}

// Take the money of the open windows: WorldSession::HandleLootMoneyOpcode(LootMoney&) (LootHandler.cpp:142-209). It walks the player's
// WHOLE loot view; for a corpse window of a player in a group the money is split between the group members that are allowed looters
// of that loot and within the group reward distance (:151-179), everybody else's share goes to them with their own money messages;
// alone, or for a non-corpse window, the player takes all of it (:181-191). The split divides by the number of such members (:162):
// with none the core would divide by zero. The looter itself always counts (IsAtGroupRewardDistance(self) is true, Player.cpp:28205-28218)
// as long as it is an allowed looter of the loot, which opening the window already required; the same test is made here first, and a
// window that fails it is not taken (false) instead of reaching the division. Different answer: a bot in a party shares the money
// with the party (the master gets a part of the bot's corpse gold), as every player of this core does.
inline bool LootMoneyCompat(Player* bot)
{
    Group* group = bot->GetGroup();
    for (std::pair<ObjectGuid const, Loot*> const& view : bot->GetAELootView())
    {
        Loot const* loot = view.second;
        if (!loot || !group || loot->loot_type != LOOT_CORPSE)
            continue;

        bool anyoneNear = false;
        for (GroupReference const& itr : group->GetMembers())
        {
            Player* member = itr.GetSource();
            if (member && loot->HasAllowedLooter(member->GetGUID()) && bot->IsAtGroupRewardDistance(member))
            {
                anyoneNear = true;
                break;
            }
        }

        if (!anyoneNear)
            return false;
    }

    WorldPackets::Loot::LootMoney packet{ WorldPacket(CMSG_LOOT_MONEY) };
    bot->GetSession()->HandleLootMoneyOpcode(packet);
    return true;
}

// The event of a window (T: the head of the SMSG_LOOT_RESPONSE packet [guid][loot type][gold u32][item count u8][item rows]). F's event packet is the
// owner guid (LootOpenCompat, or the poll of a game object's window, TbCompat_S9.h); the money is read from the core's Loot object in the player's view
// (Loot::gold, Loot.h:298) and the ITEM ROWS are rebuilt into `p` in the layout StoreLootAction's loop reads (T: [slot u8][item id u32][count u32]
// [display id u32][random suffix u32][random property id u32][slot type u8]), one row per item the core would show this player in its loot window
// (Loot::BuildLootResponse, Loot.cpp:1243: LootItem::GetUiTypeForPlayer has a value). The slot type is the loop's: 0 = this bot may take it (the core's
// ALLOW_LOOT and OWNER), anything else is skipped by the loop (1 a roll is running, 2 locked, 3 master loot). The slot number is the core's loot list id,
// the very number HandleAutostoreLootItemOpcode takes. Currencies and tracking quests are not rows (a Classic corpse holds none). The display id and the
// random suffix / property are 0 (F has item bonus lists, Unported "item random properties"). Returns false when the player has no window of that guid
// (T: a loot error).
inline bool LootWindowReadCompat(Player* bot, WorldPacket& p, ObjectGuid& guid, uint8& loot_type, uint32& gold, uint8& items, uint8& lootError)
{
    lootError = 0;
    if (p.size() == 0)
        return false;

    p.rpos(0);
    p >> guid;

    Loot const* loot = bot->GetLootByWorldObjectGUID(guid);
    if (!loot)
        return false;

    loot_type = uint8(loot->loot_type);
    gold = loot->gold;
    items = 0;

    ByteBuffer rows;
    for (LootItem const& item : loot->items)
    {
        if (items == 255 || item.type != LootItemType::Item)
            continue;
        Optional<LootSlotType> const uiType = item.GetUiTypeForPlayer(bot, *loot);
        if (!uiType)
            continue;
        uint8 const slotType = (*uiType == LOOT_SLOT_TYPE_ALLOW_LOOT || *uiType == LOOT_SLOT_TYPE_OWNER) ? uint8(LOOT_SLOT_TYPE_ALLOW_LOOT) : uint8(*uiType);
        rows << uint8(item.LootListId) << uint32(item.itemid) << uint32(item.count) << uint32(0) << uint32(0) << uint32(0) << slotType;
        ++items;
    }

    p.clear();
    p << guid;
    size_t const head = p.wpos();
    p.append(rows);
    p.rpos(head);
    return true;
}

// The loot of the open window (Player::GetLootByWorldObjectGUID) and the loot target: the core's SendLoot already set it (Player.cpp:10123).
inline Loot* LootOfWindowCompat(Player* bot, ObjectGuid const& guid) { return bot->GetLootByWorldObjectGUID(guid); }
inline void SetLootGuidCompat(Player* /*bot*/, ObjectGuid const& /*guid*/) {}

// Gathering, skinning and opening game objects: a cast on the object or the corpse and the window of items that follows (lead decision
// D2: money only). True on F: DoLoot stops there and answers "the open failed" (OpenLootAction counts it, three failures abandon the
// object). The skill a corpse asks for is still named for the code behind the line (T's helper is ai/cmangos-compat-shim.h:59; F: the
// creature difficulty's own accessor, CreatureData.h:475).
// Slice 9: a CHEST (GAMEOBJECT_TYPE_CHEST) is opened by the opening cast and its window is read like a corpse's (the poll of TbCompat_S9.h), so a chest
// is not "unported": false = the code below runs for it. Skin is ported too (TbCompat_V2.h CorpseSkinnableByCompat): a creature's Skinning cast runs,
// and the poll announces its LOOT_SKINNING window. Everything else that needs a cast (a herb or ore node through the brain, a lock the bot picks)
// stays the named feature and answers true. Spec unknown U3: whether the lock of the gunpowder chest opens for a bot through `open loot`, one boot decides.
inline bool LootGatherUnportedCompat(WorldObject* object)
{
    if (object)
    {
        if (object->ToCreature())
            return false;                               // the skinning cast below is ported
        if (GameObject* chest = object->ToGameObject())
            if (chest->GetGoType() == GAMEOBJECT_TYPE_CHEST)
                return false;
    }
    tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return true;
}
inline uint32 GetRequiredLootSkillCompat(CreatureTemplate const* tpl)
{
    CreatureDifficulty const* difficulty = tpl ? tpl->GetDifficulty(DIFFICULTY_NONE) : nullptr;
    return difficulty ? uint32(difficulty->GetRequiredLootSkill()) : uint32(SKILL_NONE);
}

// The facts of one slot of an open window behind the take decision (ai::BotMayTakeLootSlot): what the core's own loot view says about the slot for this bot.
// `itemindex` is the core's loot list id. Returns false when the slot holds no item this bot sees. Mapping of LootItem::GetUiTypeForPlayer (Loot.cpp:212):
// ALLOW_LOOT / OWNER = the bot may take it (sharedAllowed), ROLL_ONGOING = blocked (a roll runs), LOCKED / MASTER = no right (it never reaches here: the
// window rows skip them). allowedForPlayer is true for every slot with a UI type (the core only shows an item to its allowed looters, Loot.cpp:217);
// a personal quest copy is `needs_quest` outside the loot rules, a free-for-all copy is `freeforall` (Loot.h:187-190). The take itself is made by
// the core (Player::StoreLootItem, Player.cpp:28977), which repeats every check.
inline bool LootSlotFactsCompat(Player* bot, Loot* loot, uint8 itemindex, bool& allowedForPlayer, bool& isQuest, bool& isFfa,
    bool& isBlocked, bool& sharedAllowed)
{
    LootItem const* item = loot ? loot->GetItemInSlot(itemindex) : nullptr;
    if (!item)
        return false;
    Optional<LootSlotType> const uiType = item->GetUiTypeForPlayer(bot, *loot);
    if (!uiType)
        return false;

    allowedForPlayer = true;
    isQuest = item->needs_quest && !item->follow_loot_rules;
    isFfa = item->freeforall;
    isBlocked = *uiType == LOOT_SLOT_TYPE_ROLL_ONGOING;
    sharedAllowed = *uiType == LOOT_SLOT_TYPE_ALLOW_LOOT || *uiType == LOOT_SLOT_TYPE_OWNER;
    return true;
}

// Take one item of the window: CMSG_LOOT_ITEM into WorldSession::HandleAutostoreLootItemOpcode (LootHandler.cpp:77). The request names the Loot's own
// guid (the key of the player's loot view, Player.cpp:10138) and the loot list id. The core checks the loot view, the distance of a game object or
// corpse, then Player::StoreLootItem: the free bag slot (CanStoreNewItem), the allowed-looter and roll-winner rules, and tells the player. An item
// that cannot be stored is left in the window (the core sends an equip error to the headless session). The caller counts the take; whether the item
// reached the bags is read from the item count by the commander's order code.
inline void LootAutostoreCompat(Player* bot, Loot* loot, uint8 itemindex)
{
    if (!bot->GetSession() || !loot)
        return;
    WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
    WorldPackets::Loot::LootRequest request;
    request.Object = loot->GetGUID();
    request.LootListID = itemindex;
    packet.Loot.push_back(request);
    bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
}

// Does the legacy item loop stand aside? F: no, since slice 9 items are taken (false = the checks of IsLootAllowed run).
inline bool ItemLootingUnportedCompat() { return false; }

// The quest's required item i of the first four item objectives (T Quest::ReqItemId / ReqItemCount, four slots). F: the objectives of type item
// (QuestDef.h:356, :477), in order; a quest with fewer item objectives answers false for the rest.
inline bool QuestRequiredItemCompat(Quest const* quest, int index, uint32& itemId, uint32& count)
{
    int seen = 0;
    for (QuestObjective const& objective : quest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_ITEM)
            continue;
        if (seen++ == index)
        {
            itemId = uint32(objective.ObjectID);
            count = uint32(std::max(0, objective.Amount));
            return true;
        }
    }
    return false;
}

// How far from a corpse the core lets a player open it: AELootCreatureCheck::LootDistance = 30 yards (LootHandler.cpp:46), measured from
// the player's position to the creature (IsWithinDist). For the debug lines only; the in-range decision is the loot object's (V2).
inline float LootDistanceCompat(Player* /*bot*/, Creature* /*creature*/) { return 30.0f; }

// The group's id as a key: F's group guid through the one guid key (GuidKeyCompat, TbCompat_Unit.h).
inline uint64 GroupIdKeyCompat(Group* group) { return GuidKeyCompat(group->GetGUID()); }

// Loot-boost helpers (ApplyBotLootBonus: the bot-only extra roll of green and blue drops reads T's loot templates; part of the item
// feature, never reached on F). Failure values: no extra rate, the roll never succeeds.
template <class LootStoreType>
inline float DropRateOfQualityCompat(LootStoreType& /*creatureLootStore*/, uint32 /*quality*/) { return 1.0f; }
inline bool RollChanceCompat(float /*chance*/) { tbcompat::Unported(tbcompat::Feature::LootBoost); return false; }

// Whether a corpse still has loot this bot may take. T: the raw lootable flag OR the viewer's own right. F: the core's own right
// Player::isAllowedToLoot (Player.cpp:20370-20412: dead, the viewer's loot exists and is not looted, the viewer is an allowed looter,
// gold always counts (Loot.cpp:1198-1202), and the round-robin / group-loot turn). Different answer: the raw lootable dynamic flag is not
// read (on T the core masks it per viewer so it mostly equalled the right; on F it is one value for everybody, so it would also say
// yes for a corpse another player tapped).
// R3 merge: the same core call as V2's CorpseAllowedToLootCompat (TbCompat_V2.h, included first): one F definition. On T the two differ
// (this one ORs the raw lootable flag, V2's is the right alone: each keeps the line it replaced), see SLICE2-R3-NOTES.md.
inline bool CorpseHasBodyLootForCompat(Player* bot, Creature* creature) { return CorpseAllowedToLootCompat(bot, creature); }

// ai::HasVisiblePersonalItemFor: the personal items (quest, free-for-all) the viewer may see while another member holds the turn.
// F: Loot::hasItemFor(viewer) (Loot.cpp:1211-1223: quest items of the viewer and its free-for-all items), the very test
// Player::isAllowedToLoot makes for the same situation (Player.cpp:20395, :20294). The policy's own T algorithm (`body`) is not run.
template <class LootType, class ViewerType, class Body>
inline bool PersonalItemVisibleCompat(LootType const& loot, ViewerType* viewer, Body&&) { return loot.hasItemFor(viewer); }

// ===============================================================================================================
// ITEM USE: an owned item's on-use spell (self, unit or item target) goes through the core's own item cast. A game-object
// target, an openable and a cast with no owned Item* keep the named feature (UseItemAction.cpp, lead decisions D3 and
// "decided one by one")
// ===============================================================================================================

struct ItemSpellUseCompat
{
    bool skipped = false;
    bool cast = false;
    uint32 castDuration = 0;
    Unit* unit = nullptr;
    Item* item = nullptr;
    GameObject* gameObject = nullptr;
};

// An item of the bags by its entry (T: the same scan of the backpack and the bags, ai/cmangos-compat-shim.h:69). Player::GetItemByEntry
// with ItemSearchLocation::Inventory is the backpack and the bags (Player.h:1508, :853-863), without the equipment and the bank.
inline Item* FindItemByEntryCompat(Player* player, uint32 itemEntry)
{
    return player ? player->GetItemByEntry(itemEntry, ItemSearchLocation::Inventory) : nullptr;
}

// One on-use spell of an item (the loop of UseAction::UseItemInternal). T builds its own Spell (BotUseItemSpell) for every kind of
// target. F uses the core's own item cast (Player::CastItemUseSpell) for owned items in bags/equipment with self, unit, or item targets.
// GameObject targets and item-cheat casts without an owned Item* remain unported.
template <class SpellType, class AiType, class ItemForSpellFn>
inline ItemSpellUseCompat CastItemSpellCompat(AiType* ai, Player* bot, ItemPrototype const* /*proto*/, _ItemSpell const& spellData,
    SpellEntry const* /*spellInfo*/, uint32 itemId, uint8 successCasts, Item* itemUsed, Unit* unit, GameObject* gameObject, Item* item,
    ItemForSpellFn&&, bool foodOrDrink)
{
    ItemSpellUseCompat out;

    if (!itemUsed)
    {
        tbcompat::Unported(tbcompat::Feature::ItemCasts);
        return out;
    }
    if (gameObject)
    {
        tbcompat::Unported(tbcompat::Feature::ItemUseOther);
        return out;
    }

    // A second on-use spell of the item: not cast (the first one spent the item); the loop goes on to its end.
    if (successCasts > 0)
    {
        out.skipped = true;
        return out;
    }

    // The core's own validity test of the use effect (Player.cpp:9812-9840), made first so that a refused item is not an error line per tick.
    ItemTemplate const* tpl = itemUsed->GetTemplate();
    bool onUseEffect = false;
    if (!tpl->HasFlag(ITEM_FLAG_LEGACY))
        for (ItemEffectEntry const* effect : itemUsed->GetEffects())
            if (effect->SpellID == int32(spellData.SpellId) && effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
                onUseEffect = true;
    if (!onUseEffect)
    {
        tbcompat::Unported(tbcompat::Feature::ItemUseOther);
        return out;
    }

    auto* session = dynamic_cast<TortoiseBots::BotSession*>(bot->GetSession());
    if (!session)
    {
        tbcompat::Unported(tbcompat::Feature::ItemUseOther);
        return out;
    }

    SpellInfo const* coreSpell = sSpellMgr.GetSpellInfo(spellData.SpellId, DIFFICULTY_NONE);
    int32 const castTimeMs = coreSpell ? coreSpell->CalcCastTime() : 0;
    bool const channeled = coreSpell && coreSpell->IsChanneled();
    if (TortoiseBots::ShouldStopMovingForItemCast(foodOrDrink, castTimeMs, channeled))
        ai->StopMoving();

    TortoiseBots::CastResult stale;
    session->TakeInTickRefusal(stale);                  // forget a refusal of an earlier call in this tick

    uint32 const countBefore = bot->GetItemCount(itemId);
    SpellCastTargets targets;
    switch (TortoiseBots::DecideItemCastTargetKind(item != nullptr, unit != nullptr))
    {
        case TortoiseBots::ItemCastTargetKind::Item:
            targets.SetItemTarget(item);
            out.item = item;
            break;
        case TortoiseBots::ItemCastTargetKind::Unit:
            targets.SetUnitTarget(unit);
            out.unit = unit;
            break;
        case TortoiseBots::ItemCastTargetKind::Self:
            break;
    }

    bot->CastItemUseSpell(itemUsed, spellData.SpellId, targets, ObjectGuid::Empty, { 0, 0, 0 });
    // itemUsed may be gone now (the last of a stack is consumed by the cast): not touched again.

    // Only a refusal OF THIS spell is the item's: the in-tick slot may hold the failure of an earlier, interrupted cast (R5 item 4), which is not
    // this item's answer and is left out (the checks below read what the cast really did).
    TortoiseBots::CastResult refusal;
    if (session->TakeInTickRefusal(refusal) && refusal.spellId == spellData.SpellId)
    {
        ai->NoteCastCheckFail(SpellCastResult(refusal.reason));
        return out;
    }

    bool const started = bot->GetItemCount(itemId) < countBefore || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) ||
        bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) || bot->HasAura(spellData.SpellId) ||
        (bot->GetSpellHistory() && bot->GetSpellHistory()->HasCooldown(spellData.SpellId));
    if (!started)
        return out;

    out.cast = true;
    out.castDuration = TortoiseBots::ItemCastDurationMs(foodOrDrink, castTimeMs, channeled, coreSpell ? coreSpell->GetDuration() : 0);
    return out;
}

// Item cast onto a unit target (e.g. Soulstone via CastItemTargetAction::Execute).
template <class SpellType>
inline bool CastItemOnTargetCompat(Player* bot, Unit* target, Item* item, ItemPrototype const* /*proto*/)
{
    if (!item)
    {
        tbcompat::Unported(tbcompat::Feature::ItemCasts);
        return false;
    }
    if (!bot || !target)
        return false;

    ItemTemplate const* tpl = item->GetTemplate();
    uint32 spellId = 0;
    if (!tpl->HasFlag(ITEM_FLAG_LEGACY))
        for (ItemEffectEntry const* effect : item->GetEffects())
            if (effect->SpellID > 0 && effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
            {
                spellId = uint32(effect->SpellID);
                break;
            }
    if (!spellId)
        return false;

    SpellInfo const* coreSpell = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (coreSpell && (coreSpell->CalcCastTime() > 0 || coreSpell->IsChanneled()))
        bot->StopMoving();

    auto* session = dynamic_cast<TortoiseBots::BotSession*>(bot->GetSession());
    if (session)
    {
        TortoiseBots::CastResult stale;
        session->TakeInTickRefusal(stale);
    }

    uint32 const itemId = item->GetEntry();
    uint32 const countBefore = bot->GetItemCount(itemId);
    SpellCastTargets targets;
    targets.SetUnitTarget(target);
    bot->CastItemUseSpell(item, spellId, targets, ObjectGuid::Empty, { 0, 0, 0 });

    if (session)
    {
        TortoiseBots::CastResult refusal;
        if (session->TakeInTickRefusal(refusal) && refusal.spellId == spellId)
            return false;
    }

    return bot->GetItemCount(itemId) < countBefore || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) ||
        bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) || target->HasAura(spellId) ||
        (bot->GetSpellHistory() && bot->GetSpellHistory()->HasCooldown(spellId));
}

// Use / imbue an item by bag/slot/target (used by ApplyPoisonAction via PlayerbotAI::ImbueItem).
inline void QueueUseItemPacketCompat(Player* bot, uint8 bagIndex, uint8 slot, uint8 spellIndex, uint16 targetFlag, ObjectGuid const& targetGuid)
{
    if (!bot)
        return;
    Item* item = bot->GetItemByPos(bagIndex, slot);
    if (!item)
        return;

    ItemPrototype const* proto = GetItemProtoCompat(item);
    if (!proto || spellIndex >= MAX_ITEM_PROTO_SPELLS)
        return;
    uint32 const spellId = proto->Spells[spellIndex].SpellId;
    if (!spellId)
        return;

    SpellCastTargets targets;
    if ((targetFlag & TARGET_FLAG_ITEM) || targetGuid.IsItem())
    {
        Item* targetItem = bot->GetItemByGuid(targetGuid);
        if (!targetItem)
            return;
        targets.SetItemTarget(targetItem);
    }
    else if ((targetFlag & TARGET_FLAG_UNIT) || targetGuid.IsUnit() || targetGuid.IsPlayer())
    {
        Unit* targetUnit = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!targetUnit)
            return;
        targets.SetUnitTarget(targetUnit);
    }

    SpellInfo const* coreSpell = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (coreSpell && (coreSpell->CalcCastTime() > 0 || coreSpell->IsChanneled()))
        bot->StopMoving();

    bot->CastItemUseSpell(item, spellId, targets, ObjectGuid::Empty, { 0, 0, 0 });
}

inline bool ItemProtoHasDisplayIdCompat(ItemPrototype const* proto, uint32 displayId)
{
    if (!proto)
        return false;
    if (ItemModifiedAppearanceEntry const* modifiedAppearance = TransmogMgr::GetItemModifiedAppearance(proto->ItemId, 0))
        if (ItemAppearanceEntry const* itemAppearance = sItemAppearanceStore.LookupEntry(modifiedAppearance->ItemAppearanceID))
            return itemAppearance->ItemDisplayInfoID == displayId;
    return false;
}

// The count of a stack the bot has just used from. T: the item object's own count (the item is still there). F: an instant-use item is consumed
// synchronously by the cast and a just-created item can be deleted at once (Item.cpp:1326-1335), so the object may be freed; the count is asked of the
// player by item id, which reads the bags (R5 item 5). F counts every stack of the id, T the one stack used: the message only says "some left".
inline uint32 ItemCountAfterUseCompat(Player* bot, Item* /*itemUsed*/, uint32 itemId) { return bot->GetItemCount(itemId); }

// CMSG_OPEN_ITEM (open a bag item that holds loot): not ported. Failure value: false (not opened).
inline bool OpenItemCompat(Player* /*bot*/, Item* /*item*/) { tbcompat::Unported(tbcompat::Feature::ItemUseOther); return false; }

// ai::BotUseItemSpell::OpenLockCheck (a Spell subclass reading Spell internals, for keys and lockpicking): not ported.
template <class SpellType>
inline bool BotUseItemOpenLockCheckCompat(SpellType* /*self*/) { tbcompat::Unported(tbcompat::Feature::ItemCasts); return false; }

// The plain use of a game object (the use packet T queues): F uses typed packets and a different interact check (feature "game object
// use"). Failure value: false (not used).
inline bool QueueGameObjectUseCompat(Player* /*bot*/, GameObject* /*go*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }

inline void RemoveStealthAurasCompat(Player* bot)
{
    if (bot)
        bot->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
}
inline bool GameObjectUseUnportedCompat() { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return true; }
