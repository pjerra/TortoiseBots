#include "playerbot/playerbot.h"
#include "MimicConsumableAction.h"
#include "playerbot/ServerFacade.h"
#include "../../../runtime/BotManager.h"
#include "../../../runtime/policy/provision/ClassConsumablePolicy.h"
#include "../../../runtime/policy/provision/MimicConsumablePolicy.h"

using namespace ai;
using namespace TortoiseBots;

// Sighting range for mimicry: the bot must see the master drink.
static constexpr float MIMIC_MASTER_SIGHT_YARDS = 40.0f;

bool MimicConsumableAction::isPossible()
{
    if (!ai->HasStrategy("mimic consumables", BotState::BOT_STATE_NON_COMBAT))
        return false;
    // Strictly out of combat on both sides; dead/ghost/mounted/taxi bots skip.
    if (!bot->IsAlive() || bot->IsInCombat())
        return false;
    if (bot->HasAuraType(SPELL_AURA_GHOST))
        return false;
    if (bot->IsMounted() || bot->IsTaxiFlying() || bot->IsBeingTeleported())
        return false;
    Player* master = ai->GetMaster();
    if (!master || !master->IsAlive() || master->IsInCombat())
        return false;
    if (bot->GetMapId() != master->GetMapId())
        return false;
    return bot->GetDistance(master) <= MIMIC_MASTER_SIGHT_YARDS;
}

bool MimicConsumableAction::isUseful()
{
    return isPossible();
}
uint32_t MimicConsumableAction::ResolveBotItem(MimicPurpose purpose, uint32_t masterItemId,
    MimicResolution const& resolution)
{
    switch (purpose)
    {
        case MimicPurpose::BATTLE_PHYS_STR:
        case MimicPurpose::BATTLE_PHYS_AGI:
        case MimicPurpose::BATTLE_CASTER:
            // Tanks keep their own guardian; only the battle slot changes.
            return resolution.battleItemId;
        case MimicPurpose::GUARDIAN_ARMOR:
        case MimicPurpose::GUARDIAN_HEALTH:
        case MimicPurpose::GUARDIAN_MANA:
            return resolution.guardianItemId;
        case MimicPurpose::SCROLL_STAT:
        case MimicPurpose::FLASK:
        case MimicPurpose::PROT_POTION:
        case MimicPurpose::FOOD_BUFF:
        case MimicPurpose::ALCOHOL_BUFF:
        case MimicPurpose::SPECIAL_RAID:
        case MimicPurpose::UTILITY:
        case MimicPurpose::WEAPON_IMBUE:
            return resolution.singleItemId;
        default:
            break;
    }
    // Silence unused-parameter warnings on the empty path.
    (void)masterItemId;
    return 0;
}

void MimicConsumableAction::RemoveConflictingAura(MimicPurpose purpose, uint32_t botItemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(botItemId);
    if (!proto || !proto->Spells[0].SpellId)
        return;
    uint32_t mySpell = proto->Spells[0].SpellId;

    // Core does not enforce the 1 battle + 1 guardian rule for standard
    // elixirs (mask 0), so the module guards it: drinking one side clears
    // only that side's previous aura. Flasks clear both sides + old flasks.
    auto clearSide = [this, mySpell](MimicTier const* ladder, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            ItemPrototype const* other = sObjectMgr.GetItemPrototype(ladder[i].itemId);
            if (!other || !other->Spells[0].SpellId || other->Spells[0].SpellId == mySpell)
                continue;
            if (bot->HasAura(other->Spells[0].SpellId))
                bot->RemoveAurasDueToSpell(other->Spells[0].SpellId);
        }
    };

    if (IsBattleElixirPurpose(purpose))
    {
        clearSide(BATTLE_STR_LADDER, sizeof(BATTLE_STR_LADDER) / sizeof(BATTLE_STR_LADDER[0]));
        clearSide(BATTLE_AGI_LADDER, sizeof(BATTLE_AGI_LADDER) / sizeof(BATTLE_AGI_LADDER[0]));
        clearSide(BATTLE_FIRE_LADDER, sizeof(BATTLE_FIRE_LADDER) / sizeof(BATTLE_FIRE_LADDER[0]));
        clearSide(BATTLE_FROST_LADDER, sizeof(BATTLE_FROST_LADDER) / sizeof(BATTLE_FROST_LADDER[0]));
        clearSide(BATTLE_ARCANE_LADDER, sizeof(BATTLE_ARCANE_LADDER) / sizeof(BATTLE_ARCANE_LADDER[0]));
        clearSide(BATTLE_SHADOW_LADDER, sizeof(BATTLE_SHADOW_LADDER) / sizeof(BATTLE_SHADOW_LADDER[0]));
        clearSide(BATTLE_NATURE_LADDER, sizeof(BATTLE_NATURE_LADDER) / sizeof(BATTLE_NATURE_LADDER[0]));
    }
    else if (IsGuardianElixirPurpose(purpose))
    {
        clearSide(GUARDIAN_ARMOR_LADDER, sizeof(GUARDIAN_ARMOR_LADDER) / sizeof(GUARDIAN_ARMOR_LADDER[0]));
        clearSide(GUARDIAN_HEALTH_LADDER, sizeof(GUARDIAN_HEALTH_LADDER) / sizeof(GUARDIAN_HEALTH_LADDER[0]));
        clearSide(GUARDIAN_MANA_LADDER, sizeof(GUARDIAN_MANA_LADDER) / sizeof(GUARDIAN_MANA_LADDER[0]));
    }
    else if (purpose == MimicPurpose::FLASK)
    {
        // Flasks knock out both elixir slots plus the previous flask.
        RemoveConflictingAura(MimicPurpose::BATTLE_PHYS_STR, botItemId);
        RemoveConflictingAura(MimicPurpose::GUARDIAN_ARMOR, botItemId);
        bot->RemoveAurasDueToSpell(FLASK_TITANS_AURA_ID);
        bot->RemoveAurasDueToSpell(FLASK_WISDOM_AURA_ID);
        bot->RemoveAurasDueToSpell(FLASK_SUPREME_AURA_ID);
        bot->RemoveAurasDueToSpell(FLASK_CHROMATIC_AURA_ID);
    }
    else if (purpose == MimicPurpose::FOOD_BUFF)
    {
        // 1 Well Fed rule: the new tier overwrites the previous one.
        bot->RemoveAurasDueToSpell(FOOD_SQUID_AURA_ID);
        bot->RemoveAurasDueToSpell(FOOD_CHIMAEROK_AURA_ID);
        bot->RemoveAurasDueToSpell(FOOD_DUMPLINGS_AURA_ID);
        bot->RemoveAurasDueToSpell(FOOD_NIGHTFIN_AURA_ID);
        bot->RemoveAurasDueToSpell(FOOD_TUBER_AURA_ID);
    }
    else if (purpose == MimicPurpose::SPECIAL_RAID)
    {
        // 1 Zanza rule: a new Zanza overwrites the previous one
        // (24382 spirit, 24417 sheen, 24383 swiftness).
        bot->RemoveAurasDueToSpell(24382);
        bot->RemoveAurasDueToSpell(24417);
        bot->RemoveAurasDueToSpell(24383);
    }
}

bool MimicConsumableAction::ApplyWeaponImbue(uint32_t stoneItemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(stoneItemId);
    if (!proto || !proto->Spells[0].SpellId)
        return false;

    Item* mainWeapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!mainWeapon)
        return false;
    // Shaman class imbues and existing temp enchants win over stones.
    if (bot->GetClass() == CLASS_SHAMAN)
        return false;
    if (mainWeapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0)
        return false;
    // Feral druids in animal forms use stat-sticks: skip weapon imbues.
    if (bot->GetClass() == CLASS_DRUID)
    {
        uint32_t form = bot->GetShapeshiftForm();
        if (form == FORM_CAT || form == FORM_BEAR)
            return false;
    }
    // Blunt weapons take weightstones, edged weapons sharpening stones:
    // swap to the same-tier stone of the matching family.
    uint32_t subClass = GetItemProtoCompat(mainWeapon) ? GetItemProtoCompat(mainWeapon)->SubClass : 0;
    bool blunt = subClass == ITEM_SUBCLASS_WEAPON_MACE || subClass == ITEM_SUBCLASS_WEAPON_MACE2 ||
                 subClass == ITEM_SUBCLASS_WEAPON_AXE || subClass == ITEM_SUBCLASS_WEAPON_AXE2;
    bool isWeight = false;
    for (MimicTier const& tier : WEIGHTSTONE_LADDER)
        if (tier.itemId == proto->ItemId)
            isWeight = true;
    if (blunt != isWeight)
    {
        uint32_t level = bot->GetLevel();
        proto = sObjectMgr.GetItemPrototype(blunt ? BestTierForLevel(WEIGHTSTONE_LADDER, level)
                                                 : BestTierForLevel(SHARPENING_LADDER, level));
        if (!proto || !proto->Spells[0].SpellId)
            return false;
    }

    auto imbueOne = [this, proto](Item* weapon) -> bool
    {
        if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0)
            return false;
        SpellCastTargets targets;
        targets.setItemTarget(weapon);
        targets.m_targetMask = TARGET_FLAG_ITEM;
        int casts = 0;
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            uint32_t spellId = proto->Spells[i].SpellId;
            if (!spellId)
                continue;
            SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
            if (!spellInfo)
                continue;
            BotUseItemSpell* spell = BotUseItemSpell::Create(bot, spellInfo,
                (casts > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);
            if (!spell)
                continue;
            // NOTE: no manual cooldown here (see UseAction::UseItemInternal):
            // the core applies it in Spell::SendSpellCooldown after
            // validation. Pre-applying cancelled the deferred cast via its
            // own cooldown.
            if (spell->ForceSpellStart(&targets) != SPELL_CAST_OK)
                return false;
            ++casts;
        }
        return casts > 0;
    };

    bool ok = imbueOne(mainWeapon);
    // Dual-wield warriors mirror the stone onto the off-hand weapon.
    // A shield in the off-hand is not a weapon: MH only.
    if (bot->GetClass() == CLASS_WARRIOR)
    {
        Item* offWeapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        if (offWeapon && GetItemProtoCompat(offWeapon) &&
            GetItemProtoCompat(offWeapon)->Class == ITEM_CLASS_WEAPON)
            ok = imbueOne(offWeapon) || ok;
    }
    if (ok)
        SetDuration(3000);
    return ok;
}

// True when the item prototype carries a usable on-use spell.
static bool HasUseSpell(ItemPrototype const* proto)
{
    if (!proto)
        return false;
    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32_t spellId = proto->Spells[i].SpellId;
        if (!spellId)
            continue;
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE &&
            proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            continue;
        if (sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            return true;
    }
    return false;
}

bool MimicConsumableAction::CastMimicItem(uint32_t botItemId, bool withEatEmote)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(botItemId);
    // Broken custom items (no spell on the prototype) are skipped.
    if (!HasUseSpell(proto))
        return false;

    SpellCastTargets targets;
    targets.setUnitTarget(bot);
    targets.m_targetMask = TARGET_FLAG_SELF;

    int casts = 0;
    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        uint32_t spellId = proto->Spells[i].SpellId;
        if (!spellId)
            continue;
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE &&
            proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            continue;
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        if (!spellInfo)
            continue;
        // Spell-bound targeting (e.g. weapon imbues resolved earlier) is
        // handled by the caller; self-targeted buffs land here.
        if (spellInfo->Targets & TARGET_FLAG_ITEM)
            continue;
        BotUseItemSpell* spell = BotUseItemSpell::Create(bot, spellInfo,
            (casts > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);
        if (!spell)
            continue;
        // NOTE: no manual cooldown here (see UseAction::UseItemInternal):
        // the core applies it in Spell::SendSpellCooldown after validation.
        if (spell->ForceSpellStart(&targets) != SPELL_CAST_OK)
            return false;
        ++casts;
    }
    if (!casts)
        return false;

    if (withEatEmote)
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
    SetDuration(3000);
    return true;
}

void MimicConsumableAction::Feedback(char const* itemName, bool silent)
{
    if (silent || !itemName)
        return;
    Player* master = ai->GetMaster();
    if (!master)
        return;
    std::string text = std::string("Using ") + itemName;
    ai->TellPlayerNoFacing(master, text);
}

bool MimicConsumableAction::Execute(Event& event)
{
    if (!isPossible())
        return false;

    Player* master = ai->GetMaster();
    if (!master)
        return false;

    // CMSG_USE_ITEM layout (core HandleUseItemOpcode): bagIndex, slot,
    // spell_count. The bot reads which bag slot the master used.
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    uint8_t bagIndex = 0, slot = 0, spellCount = 0;
    if (p.size() < 3)
        return false;
    p >> bagIndex >> slot >> spellCount;

    Item* masterItem = master->GetItemByPos(bagIndex, slot);
    if (!masterItem)
        return false;
    ItemPrototype const* masterProto = GetItemProtoCompat(masterItem);
    if (!masterProto || !masterProto->Spells[0].SpellId)
        return false;
    uint32_t masterItemId = masterItem->GetEntry();

    MimicPurpose purpose = PurposeForMasterItem(masterItemId);
    if (purpose == MimicPurpose::NONE)
        return false;

    PlayerTalentSpec spec = ai->GetTalentSpec();
    // A shaman tank still reports SPEC_SHAMAN_ENHANCEMENT (tab 1): the
    // policy's spec enum cannot express it, so thread the AI tank read
    // through and the tank ladders (Titans, armor, STA) apply.
    MimicResolution resolution = ResolveMimicItem(masterItemId, bot->GetClass(),
        static_cast<uint32_t>(spec), bot->GetLevel(), ai->IsTank(bot));

    // Raid anti-spam: with more than kMaxWhisperBots bots on the same
    // master every bot still acts, but none whispers; the master sees one
    // summary line (sent by BotCommands) instead of N whispers.
    size_t botCount = BotManager::Instance().GetBotsForMaster(master->GetObjectGuid()).size();
    bool silent = botCount > kMaxWhisperBots;

    if (purpose == MimicPurpose::WEAPON_IMBUE)
    {
        if (resolution.skipWeaponImbue)
            return false;
        if (bot->GetClass() == CLASS_ROGUE)
        {
            // Levels 1-19 (before poisons unlock at 20): stones on
            // daggers/swords like everyone else.
            if (resolution.singleItemId)
            {
                ItemPrototype const* stoneProto = sObjectMgr.GetItemPrototype(resolution.singleItemId);
                if (!stoneProto)
                    return false;
                if (!ApplyWeaponImbue(resolution.singleItemId))
                    return false;
                Feedback(stoneProto->Name1.c_str(), silent);
                return true;
            }
            // Level 20+: poisons through the existing upkeep actions
            // (cheat or real item path inside ApplyPoisonAction).
            bool mh = ai->DoSpecificAction("apply instant poison main hand");
            bool oh = ai->DoSpecificAction("apply deadly poison off hand");
            if (!mh && !oh)
                return false;
            Feedback("poisons", silent);
            return true;
        }
        if (!resolution.singleItemId)
            return false;
        ItemPrototype const* stoneProto = sObjectMgr.GetItemPrototype(resolution.singleItemId);
        if (!stoneProto)
            return false;
        if (TortoiseBots::IsOilEntry(resolution.singleItemId))
        {
            if (!CastMimicItem(resolution.singleItemId, false))
                return false;
            Feedback(stoneProto->Name1.c_str(), silent);
            return true;
        }
        if (!ApplyWeaponImbue(resolution.singleItemId))
            return false;
        Feedback(stoneProto->Name1.c_str(), silent);
        return true;
    }

    uint32_t botItemId = ResolveBotItem(purpose, masterItemId, resolution);
    if (!botItemId)
        return false;

    RemoveConflictingAura(purpose, botItemId);

    bool eatEmote = purpose == MimicPurpose::BATTLE_PHYS_STR || purpose == MimicPurpose::BATTLE_PHYS_AGI ||
                    purpose == MimicPurpose::BATTLE_CASTER || IsGuardianElixirPurpose(purpose) ||
                    purpose == MimicPurpose::FLASK || purpose == MimicPurpose::FOOD_BUFF ||
                    purpose == MimicPurpose::ALCOHOL_BUFF || purpose == MimicPurpose::SPECIAL_RAID;
    if (!CastMimicItem(botItemId, eatEmote))
        return false;

    ItemPrototype const* botProto = sObjectMgr.GetItemPrototype(botItemId);
    Feedback(botProto ? botProto->Name1.c_str() : nullptr, silent);
    return true;
}
