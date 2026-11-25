#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "RandomItemMgr.h"
#include "../../runtime/policy/provision/GearBlacklistPolicy.h"
#include "ItemEnchantmentMgr.h"
#include "policy/gear/RogueWeaponPolicy.h"
#include "policy/gear/SpecWeaponPolicy.h"
#include "policy/gear/SpecArmorPolicy.h"
#include "policy/gear/FeralSpecPolicy.h"
#include "policy/item/ClassicItemStatPolicy.h"

void RandomItemMgr::InitWeightLinks()
{
    if (!weightStatLink.empty())
        return;

    viableSlots[EQUIPMENT_SLOT_HEAD].insert(INVTYPE_HEAD);
    viableSlots[EQUIPMENT_SLOT_NECK].insert(INVTYPE_NECK);
    viableSlots[EQUIPMENT_SLOT_SHOULDERS].insert(INVTYPE_SHOULDERS);
    viableSlots[EQUIPMENT_SLOT_BODY].insert(INVTYPE_BODY);
    viableSlots[EQUIPMENT_SLOT_CHEST].insert(INVTYPE_CHEST);
    viableSlots[EQUIPMENT_SLOT_CHEST].insert(INVTYPE_ROBE);
    viableSlots[EQUIPMENT_SLOT_WAIST].insert(INVTYPE_WAIST);
    viableSlots[EQUIPMENT_SLOT_LEGS].insert(INVTYPE_LEGS);
    viableSlots[EQUIPMENT_SLOT_FEET].insert(INVTYPE_FEET);
    viableSlots[EQUIPMENT_SLOT_WRISTS].insert(INVTYPE_WRISTS);
    viableSlots[EQUIPMENT_SLOT_HANDS].insert(INVTYPE_HANDS);
    viableSlots[EQUIPMENT_SLOT_FINGER1].insert(INVTYPE_FINGER);
    viableSlots[EQUIPMENT_SLOT_FINGER2].insert(INVTYPE_FINGER);
    viableSlots[EQUIPMENT_SLOT_TRINKET1].insert(INVTYPE_TRINKET);
    viableSlots[EQUIPMENT_SLOT_TRINKET2].insert(INVTYPE_TRINKET);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_WEAPON);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_2HWEAPON);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_WEAPONMAINHAND);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_WEAPON);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_2HWEAPON);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_SHIELD);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_WEAPONOFFHAND);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_HOLDABLE);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RANGED);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_THROWN);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RANGEDRIGHT);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RELIC);
    viableSlots[EQUIPMENT_SLOT_TABARD].insert(INVTYPE_TABARD);
    viableSlots[EQUIPMENT_SLOT_BACK].insert(INVTYPE_CLOAK);

    weightStatLink["sta"] = ITEM_MOD_STAMINA;
    weightStatLink["str"] = ITEM_MOD_STRENGTH;
    weightStatLink["agi"] = ITEM_MOD_AGILITY;
    weightStatLink["int"] = ITEM_MOD_INTELLECT;
    weightStatLink["spi"] = ITEM_MOD_SPIRIT;

    ItemStatLink[STAT_STAMINA] = "sta";
    ItemStatLink[STAT_STRENGTH] = "str";
    ItemStatLink[STAT_AGILITY] = "agi";
    ItemStatLink[STAT_INTELLECT] = "int";
    ItemStatLink[STAT_SPIRIT] = "spi";

}

void RandomItemMgr::LoadWeightScales()
{
    InitWeightLinks();

    for (uint32 i = 0; i <= MAX_STAT_SCALES; ++i)
    {
        WeightScale scale;
        scale.info.id = 0;
        scale.info.name = "";
        scale.info.classId = 0;
        m_weightScales[i] = scale;
    }

    // load weightscales
    sLog.outString("Loading weightscales info");
    auto results = WeightScaleQueryCompat("SELECT id, name, class FROM `ai_playerbot_weightscales`");

    if (results)
    {
        int totalcount = 0;
        int statcount = 0;

        do
        {
            Field* fields = results->Fetch();
            uint32 id = fields[0].GetUInt32();
            std::string name = FieldTextCompat(fields[1]);
            uint32 clazz = fields[2].GetUInt32();

            WeightScale scale;
            scale.info.id = id;
            scale.info.name = name;
            scale.info.classId = clazz;
            m_weightScales[id] = scale;
            totalcount++;

        } while (results->NextRow());

        sLog.outString("Loaded %d weightscale class specs", totalcount);

        auto result = WeightScaleQueryCompat("SELECT id, field, val FROM `ai_playerbot_weightscale_data`");
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 id = fields[0].GetUInt32();
                std::string field = FieldTextCompat(fields[1]);
                uint32 weight = fields[2].GetUInt32();

                WeightScaleStat stat;
                stat.stat = field;
                stat.weight = weight;

                m_weightScales[id].stats.push_back(stat);
                statcount++;

            } while (result->NextRow());
        }

        sLog.outString("Loaded %d weightscale stat weights", statcount);
    }
}

bool RandomItemMgr::ShouldEquipArmorForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto)
{
    if (!proto)
        return false;

    if (ai::IsArmorExemptFromMaterialGate(proto->InventoryType))
        return true;

    if (!spec)
        spec = static_cast<uint8>(GetFallbackSpecId(playerclass));

    if (!m_weightScales[spec].info.id)
        return false;

    return ai::SpecArmorSubClassAllowed(playerclass, m_weightScales[spec].info.name, proto->InventoryType, proto->SubClass);
}

bool RandomItemMgr::CanEquipArmor(uint8 clazz, uint8 spec, uint32 level, ItemPrototype const* proto)
{
    if (proto->InventoryType == INVTYPE_TABARD)
        return true;

    if ((clazz == CLASS_WARRIOR || clazz == CLASS_PALADIN || clazz == CLASS_SHAMAN)
            && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    if ((clazz == CLASS_WARRIOR || (clazz == CLASS_PALADIN && spec != 4)) && level >= 40)
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_PLATE && proto->InventoryType != INVTYPE_CLOAK)
            return false;
    }

    if (((clazz == CLASS_WARRIOR || (clazz == CLASS_PALADIN && spec != 4)) && level < 40) ||
            ((clazz == CLASS_HUNTER || (clazz == CLASS_SHAMAN && spec != 21)) && level >= 40))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_MAIL && proto->InventoryType != INVTYPE_CLOAK)
        {
            if (spec == 22 && (proto->SubClass == ITEM_SUBCLASS_ARMOR_LEATHER || proto->SubClass == ITEM_SUBCLASS_ARMOR_CLOTH))
                return true;

            return false;
        }
    }

    if (((clazz == CLASS_HUNTER || clazz == CLASS_SHAMAN) && level < 40) ||
            (((clazz == CLASS_DRUID && !(spec == 29 || spec == 31)))  || clazz == CLASS_ROGUE))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_LEATHER && proto->InventoryType != INVTYPE_CLOAK)
            return false;
    }

    if (proto->Quality <= ITEM_QUALITY_NORMAL)
        return true;

    return true;

    //uint8 sp = 0, ap = 0, tank = 0;
    //for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    //{
    //    // for ItemStatValue != 0
    //    if(!proto->ItemStat[j].ItemStatValue)
    //        continue;

    //    AddItemStats(proto->ItemStat[j].ItemStatType, sp, ap, tank);
    //}

    //return CheckItemStats(clazz, sp, ap, tank);
}

bool RandomItemMgr::ShouldEquipWeaponForSpec(Player const* bot, uint8 playerclass, uint8 spec, ItemPrototype const* proto, bool canDualWield)
{
    EquipmentSlots slot_mh = EQUIPMENT_SLOT_START;
    EquipmentSlots slot_oh = EQUIPMENT_SLOT_START;
    EquipmentSlots slot_rh = EQUIPMENT_SLOT_START;
    for (std::map<EquipmentSlots, std::set<InventoryType> >::iterator i = viableSlots.begin(); i != viableSlots.end(); ++i)
    {
        std::set<InventoryType> slots = viableSlots[(EquipmentSlots)i->first];
        if (slots.find((InventoryType)proto->InventoryType) != slots.end())
        {
            if (i->first == EQUIPMENT_SLOT_MAINHAND)
                slot_mh = i->first;
            if (i->first == EQUIPMENT_SLOT_OFFHAND)
                slot_oh = i->first;
            if (i->first == EQUIPMENT_SLOT_RANGED)
                slot_rh = i->first;
        }
    }

    if (slot_mh == EQUIPMENT_SLOT_START && slot_oh == EQUIPMENT_SLOT_START && slot_rh == EQUIPMENT_SLOT_START)
        return false;

    if (!spec)
        spec = static_cast<uint8>(GetFallbackSpecId(playerclass));

    if (!m_weightScales[spec].info.id)
        return false;

    // A weapon-in-each-hand spec cannot use the off hand before the core lets
    // it dual wield - Player::CanEquipItem answers EQUIP_ERR_CANT_DUAL_WIELD for
    // every off-hand weapon until Dual Wield (spell 674) is learned, and a
    // warrior only picks that up at its class level (PlayerbotFactory's
    // DualWieldLevelForClass) - so such a spec has to level on a two-hander
    // where its class can wield one. Hunter and enhancement shaman already list
    // theirs, and rogue cannot use any two-hander at all (CanUseItem refuses
    // it), so fury is the only set that changes below.
    std::unordered_set<uint32> mh_weapons;
    std::unordered_set<uint32> oh_weapons;
    std::unordered_set<uint32> r_weapons;

    switch (playerclass)
    {
    case CLASS_WARRIOR:
    {
        if (m_weightScales[spec].info.name == "prot")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        else if (m_weightScales[spec].info.name == "arms")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            if (!canDualWield)
            {
                // The off hand is unusable until Dual Wield is learned, so fury
                // levels on the same two-handers arms does. Once the flag is on
                // the one-hander pair is the spec rule again and a two-hander is
                // only kept while it still out-scores the main hand (see the
                // stand-in guard in ItemUsageValue), never downgraded on sight.
                mh_weapons.insert(ITEM_SUBCLASS_WEAPON_SWORD2);
                mh_weapons.insert(ITEM_SUBCLASS_WEAPON_AXE2);
                mh_weapons.insert(ITEM_SUBCLASS_WEAPON_MACE2);
                mh_weapons.insert(ITEM_SUBCLASS_WEAPON_POLEARM);
                mh_weapons.insert(ITEM_SUBCLASS_WEAPON_STAFF);
            }
            oh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        break;
    }
    case CLASS_PALADIN:
    {
        if (m_weightScales[spec].info.name == "prot")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        else if (m_weightScales[spec].info.name == "holy")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD, ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        break;
    }
    case CLASS_HUNTER:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_FIST, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_STAFF };
        if (canDualWield)
            oh_weapons = { ITEM_SUBCLASS_WEAPON_FIST, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE };
        r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        break;
    }
    case CLASS_ROGUE:
    {
        if (bot)
        {
            bool const hasHackAndSlash = bot->HasSpell(ROGUE_TALENT_HACK_AND_SLASH_R1) ||
                                         bot->HasSpell(ROGUE_TALENT_HACK_AND_SLASH_R2);
            bool const hasCloseQuartersCombat = bot->HasSpell(ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R1) ||
                                                bot->HasSpell(ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R2);
            bool const hasConcussiveBlows = bot->HasSpell(ROGUE_TALENT_CONCUSSIVE_BLOWS_R1) ||
                                            bot->HasSpell(ROGUE_TALENT_CONCUSSIVE_BLOWS_R2) ||
                                            bot->HasSpell(ROGUE_TALENT_CONCUSSIVE_BLOWS_R3) ||
                                            bot->HasSpell(ROGUE_TALENT_CONCUSSIVE_BLOWS_R4) ||
                                            bot->HasSpell(ROGUE_TALENT_CONCUSSIVE_BLOWS_R5);

            std::string_view const specName = m_weightScales[spec].info.name;
            for (uint32 subclass : { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST, ITEM_SUBCLASS_WEAPON_AXE })
            {
                if (ai::RogueWeaponAllowed(specName, hasHackAndSlash, hasCloseQuartersCombat, hasConcussiveBlows, subclass))
                {
                    mh_weapons.insert(subclass);
                    oh_weapons.insert(subclass);
                }
            }
        }
        else
        {
            if (RogueSpecWantsDaggers(m_weightScales[spec].info.name))
            {
                // Backstab/Ambush need a dagger main hand; the off hand keeps a
                // dagger for the spec's fast-off-hand damage.
                mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER };
                oh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER };
            }
            else if (m_weightScales[spec].info.name == "combat")
            {
                // Rogues are taught fist skill; combat uses swords/maces + fists.
                mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
                oh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            }
            else
            {
                mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
                oh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            }
        }

        r_weapons = { ITEM_SUBCLASS_WEAPON_THROWN, ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        break;
    }
    case CLASS_PRIEST:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE };
        oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
        r_weapons = { ITEM_SUBCLASS_WEAPON_WAND };
        break;
    }
    case CLASS_SHAMAN:
    {
        if (m_weightScales[spec].info.name == "shamantank")
        {
            // Shaman tank: slow 1H main hand (mace/axe/fist - no daggers, no
            // 2H), shield off hand, totem ranged. Speed preference lives in
            // the factory seed filter and the mledps weight, not here.
            mh_weapons = { ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        else if (m_weightScales[spec].info.name == "resto")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC, ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        else if (m_weightScales[spec].info.name == "enhance")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC, ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        break;
    }
    case CLASS_MAGE:
    case CLASS_WARLOCK:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD };
        oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
        r_weapons = { ITEM_SUBCLASS_WEAPON_WAND };
        break;
    }
    case CLASS_DRUID:
    {
        if (m_weightScales[spec].info.name == "feraltank")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else if (m_weightScales[spec].info.name == "resto")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else if (m_weightScales[spec].info.name == "feraldps")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        break;
    }
    }

    if (slot_mh == EQUIPMENT_SLOT_MAINHAND)
    {
        return mh_weapons.find(proto->SubClass) != mh_weapons.end();
    }
    if (slot_oh == EQUIPMENT_SLOT_OFFHAND)
    {
        return oh_weapons.find(proto->SubClass) != oh_weapons.end();
    }
    if (slot_rh == EQUIPMENT_SLOT_RANGED)
    {
        return r_weapons.find(proto->SubClass) != r_weapons.end();
    }

    return false;
}

bool RandomItemMgr::ShouldEquipWeaponForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto, bool canDualWield)
{
    return ShouldEquipWeaponForSpec(nullptr, playerclass, spec, proto, canDualWield);
}

bool RandomItemMgr::ShouldEquipWeaponForSlot(Player const* bot, uint8 playerclass, uint8 spec, ItemPrototype const* proto, uint8 slot, bool canDualWield)
{
    // Shield specs and dual-wield/slot-constrained classes have a slot-aware
    // policy; every other class keeps the plain any-slot answer (fail-open).
    if (playerclass != CLASS_WARRIOR && playerclass != CLASS_PALADIN && playerclass != CLASS_SHAMAN && playerclass != CLASS_HUNTER)
        return ShouldEquipWeaponForSpec(bot, playerclass, spec, proto, canDualWield);

    if (!spec)
        spec = static_cast<uint8>(GetFallbackSpecId(playerclass));

    if (!m_weightScales[spec].info.id)
        return false;

    // The policy header owns the sets (single source of truth with the
    // standalone test); the scale id maps back to the spec name it loaded.
    // Non-tank shaman specs stay fail-open inside the policy.
    return ai::SpecWeaponAllowed(playerclass, m_weightScales[spec].info.name,
        proto->Class, proto->SubClass, slot, canDualWield);
}

bool RandomItemMgr::ShouldEquipWeaponForSlot(uint8 playerclass, uint8 spec, ItemPrototype const* proto, uint8 slot, bool canDualWield)
{
    return ShouldEquipWeaponForSlot(nullptr, playerclass, spec, proto, slot, canDualWield);
}

bool RandomItemMgr::CanEquipWeapon(uint8 clazz, ItemPrototype const* proto)
{
    switch (clazz)
    {
    case CLASS_PRIEST:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE)
            return false;
        break;
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_WARRIOR:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
        break;
    case CLASS_PALADIN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_SHAMAN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_DRUID:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_HUNTER:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW)
            return false;
        break;
    case CLASS_ROGUE:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE)
            return false;
        break;
    }

    return true;
}

uint32 RandomItemMgr::CalculateStatWeight(uint8 playerclass, uint8 spec, ItemPrototype const* proto, ItemSpecType& itSpec)
{
    uint32 specType = ITEM_SPEC_NONE;
    uint32 statWeight = 0;
    uint32 spellPower = 0;
    uint32 spellHeal = 0;
    uint32 attackPower = 0;
    bool isCasterItem = false;
    bool isAttackItem = false;
    bool isDpsItem = false;
    bool isTankItem = false;
    bool isHealingItem = false;
    bool isSpellDamageItem = false;
    bool hasInt = false;
    bool noCaster = (Classes)playerclass == CLASS_WARRIOR || (Classes)playerclass == CLASS_ROGUE || (Classes)playerclass == CLASS_HUNTER || ai::SpecScoresAsNonCaster(spec);
    bool hasMana = !((Classes)playerclass == CLASS_WARRIOR || (Classes)playerclass == CLASS_ROGUE);

    if ((Classes)playerclass == CLASS_HUNTER && proto->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)
        return (uint32)proto->ItemLevel;

    //check relicts
    if (proto->InventoryType == INVTYPE_RELIC)
    {
        if (playerclass == CLASS_PALADIN && proto->SubClass != ITEM_SUBCLASS_ARMOR_LIBRAM)
            return 0;

        if (playerclass == CLASS_DRUID && proto->SubClass != ITEM_SUBCLASS_ARMOR_IDOL)
            return 0;

        if (playerclass == CLASS_SHAMAN && proto->SubClass != ITEM_SUBCLASS_ARMOR_TOTEM)
            return 0;

        if (playerclass == CLASS_WARRIOR
            || playerclass == CLASS_HUNTER
            || playerclass == CLASS_ROGUE
            || playerclass == CLASS_PRIEST
            || playerclass == CLASS_MAGE
            || playerclass == CLASS_WARLOCK)
            return 0;

        return proto->Quality + proto->ItemLevel;
    }

    bool isWhitelist = TortoiseBots::GearWhitelistApplies(sPlayerbotAIConfig.randomGearBlacklist,
        std::find(sPlayerbotAIConfig.randomGearWhitelist.begin(), sPlayerbotAIConfig.randomGearWhitelist.end(), proto->ItemId) != sPlayerbotAIConfig.randomGearWhitelist.end(),
        proto->ItemId);

    // whitelist pvp items, as thei have wierd stats
    if (ItemRequiredHonorRankCompat(proto))
        isWhitelist = true;

    // whitelist the ONLY feral Off Hand in vanilla
    if ((spec == 30 || spec == 32) && proto->ItemId == 13385)
        isWhitelist = true;

    // whitelist atiesh
    if (playerclass == CLASS_MAGE && proto->ItemId == 22589)
        isWhitelist = true;
    if (playerclass == CLASS_WARLOCK && proto->ItemId == 22630)
        isWhitelist = true;
    if (playerclass == CLASS_PRIEST && proto->ItemId == 22631)
        isWhitelist = true;
    if (playerclass == CLASS_DRUID && proto->ItemId == 22632)
        isWhitelist = true;

    // check basic item stats
    int32 basicStatsWeight = 0;
    for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    {
        uint32 statType = 0;
        int32 val = 0;
        std::string weightName = "";

        //if (j >= proto->StatsCount)
        //    continue;

        statType = proto->ItemStat[j].ItemStatType;
        val = proto->ItemStat[j].ItemStatValue;

        if (val == 0)
            continue;

        for (std::map<std::string, uint32 >::iterator i = weightStatLink.begin(); i != weightStatLink.end(); ++i)
        {
            uint32 modd = i->second;
            if (modd == statType)
            {
                weightName = i->first;


                break;
            }
        }

        uint32 statDivisor = 1;
        if (weightName.empty())
        {
            if (char const* classicName = ai::ClassicItemStatWeightName(statType))
            {
                weightName = classicName;
                statDivisor = ai::ClassicItemStatRatingDivisor(statType);
            }
        }

        if (weightName.empty())
            continue;

        uint32 singleStat = CalculateSingleStatWeight(playerclass, spec, weightName, val);
        if (statType == 45)  // ITEM_MOD_SPELL_POWER: heals as well as damages
        {
            uint32 const healStat = CalculateSingleStatWeight(playerclass, spec, "splheal", val);
            weightName = ai::ClassicItemStatSpellPowerWeightName(statType, singleStat, healStat);
            if (healStat > singleStat)
                singleStat = healStat;
            isCasterItem = true;
        }
        if (statDivisor > 1)
            singleStat = ai::ClassicItemStatRounded(singleStat, statDivisor);
        if (weightName == "splpwr" || weightName == "firsplpwr" || weightName == "natsplpwr" ||
            weightName == "frosplpwr" || weightName == "shasplpwr" || weightName == "arcsplpwr")
        {
            isSpellDamageItem = true;
            isCasterItem = true;
            spellPower += singleStat;
            continue;
        }
        if (weightName == "splheal")
        {
            isHealingItem = true;
            spellHeal += singleStat;
            continue;
        }
        if (weightName == "atkpwr")
        {
            isAttackItem = true;
            attackPower += singleStat;
            continue;
        }

        basicStatsWeight += singleStat;

        if (val)
        {
            if (weightName == "int" && !noCaster)
                isCasterItem = true;

            if (weightName == "int")
                hasInt = true;

            if (weightName == "spellhitrtng" || weightName == "spellcritstrkrtng" || weightName == "manargn")
                isCasterItem = true;

            if (weightName == "str" || weightName == "agi" || weightName == "hitrtng" || weightName == "critstrkrtng")
                isAttackItem = true;

            if (weightName == "defrtng" || weightName == "dodgertng" || weightName == "parryrtng" ||
                weightName == "blockrtng" || weightName == "block")
                isTankItem = true;
        }
    }

    // check defensive stats
    uint32 defenseStats = 0;
    defenseStats += CalculateSingleStatWeight(playerclass, spec, "block", proto->Block);
    defenseStats += CalculateSingleStatWeight(playerclass, spec, "armor", proto->Armor);

    // check weapon dps
    if (proto->IsWeapon())
    {
        WeaponAttackType attType = BASE_ATTACK;

        uint32 dps = 0;

        for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; i++)
        {
            if (proto->Damage[i].DamageMax == 0)
                break;

            dps = (proto->Damage[i].DamageMin + proto->Damage[i].DamageMax) / (float)(proto->Delay / 1000.0f) / 2;
            if (dps)
            {
                if (proto->IsRangedWeapon())
                    statWeight += CalculateSingleStatWeight(playerclass, spec, "rgddps", dps);
                else
                    statWeight += CalculateSingleStatWeight(playerclass, spec, "mledps", dps);
            }
        }
    }

    // check item spells
    uint32 spellDamage = 0;
    uint32 spellHealing = 0;
    uint32 auraStatWeight = 0;
    uint32 auraApStatWeight = 0;
    uint32 auraHealStatWeight = 0;
    uint32 auraDamageStatWeight = 0;
    bool isFeral = false;
    for (const auto& spellData : proto->Spells)
    {
        // no spell
        if (!spellData.SpellId)
            continue;

        // apply only at-equip spells for weapons, on use/hit for armor
        if (!(spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP || (!proto->IsWeapon() && proto->InventoryType != INVTYPE_HOLDABLE && (spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || spellData.SpellTrigger == ITEM_SPELLTRIGGER_CHANCE_ON_HIT))))
            continue;

        // check if it is valid spell
        SpellEntry const* spellproto = GetSpellEntryCompat(spellData.SpellId);
        if (!spellproto)
            continue;

        bool hasAP = false;

        uint32 effectAuraStatWeight = 0;
        uint32 effectAuraApStatWeight = 0;
        uint32 effectAuraHealStatWeight = 0;
        uint32 effectAuraDamageStatWeight = 0;

        for (uint8 j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if ((spellproto->Effect[j] == SPELL_EFFECT_APPLY_AURA) &&
                (spellproto->EffectBasePoints[j] >= 0))
            {
                // spell damage
                // SPELL_AURA_MOD_DAMAGE_DONE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_DAMAGE_DONE)
                {
                    spellDamage = spellproto->EffectBasePoints[j] + 1;
                    isSpellDamageItem = true;
                    // generic spell damage
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_MAGIC)
                    {
                        effectAuraDamageStatWeight += CalculateSingleStatWeight(playerclass, spec, "splpwr", spellDamage);
                    }
                    else
                    {
                        uint32 specialDamage = 0;
                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_ARCANE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "arcsplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FROST) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "frosplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FIRE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "firsplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_SHADOW) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "shasplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_NATURE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "natsplpwr", spellDamage);

                        effectAuraDamageStatWeight += specialDamage;
                    }
                }
                // spell healing
                // SPELL_AURA_MOD_HEALING_DONE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_HEALING_DONE)
                {
                    isHealingItem = true;
                    spellHealing = spellproto->EffectBasePoints[j] + 1;
                    effectAuraHealStatWeight += CalculateSingleStatWeight(playerclass, spec, "splheal", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla spell hit rating
                // SPELL_AURA_MOD_SPELL_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_HIT_CHANCE)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellhitrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla spell crit rating
                // SPELL_AURA_MOD_SPELL_CRIT_CHANCE, SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_CRIT_CHANCE || spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellcritstrkrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // spell penetration
                // SPELL_AURA_MOD_TARGET_RESISTANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_TARGET_RESISTANCE)
                {
                    // check if magic type
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_SPELL)
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellpenrtng", abs(spellproto->EffectBasePoints[j] + 1));
                }

                // check attack power
                if (!hasAP && spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_ATTACK_POWER)
                {
                    hasAP = true;
                    isAttackItem = true;
                    std::string SpellName = spellproto->SpellName[0];
                    if (SpellName.find("Attack Power - Feral") != std::string::npos)
                        isFeral = true;
                    if (!isWhitelist && isFeral && (playerclass != CLASS_DRUID && playerclass != CLASS_WARRIOR && playerclass != CLASS_PALADIN && proto->IsWeapon()))
                        return 0;

                    effectAuraApStatWeight += CalculateSingleStatWeight(playerclass, spec, isFeral ? "feratkpwr" : "atkpwr", spellproto->EffectBasePoints[j] + 1);
                }

                // check ranged ap
                // SPELL_AURA_MOD_RANGED_ATTACK_POWER
                if (!hasAP && spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_RANGED_ATTACK_POWER)
                {
                    // filter non ranged classes
                    if (playerclass == CLASS_SHAMAN || (!proto->IsRangedWeapon() && playerclass != CLASS_HUNTER))
                        return 0;

                    hasAP = true;
                    isAttackItem = true;
                    effectAuraApStatWeight += CalculateSingleStatWeight(playerclass, spec, "atkpwr", spellproto->EffectBasePoints[j] + 1);
                }

                // check block
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SHIELD_BLOCKVALUE)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "block", spellproto->EffectBasePoints[j] + 1);
                }

                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_PARRY_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "parryrtng", spellproto->EffectBasePoints[j] + 1);
                }

                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_DODGE_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "dodgertng", spellproto->EffectBasePoints[j] + 1);
                }

                // block chance
                // SPELL_AURA_MOD_BLOCK_PERCENT
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_BLOCK_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "blockrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // armor penetration
                // SPELL_AURA_MOD_TARGET_RESISTANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_TARGET_RESISTANCE)
                {
                    // check if physical type
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_NORMAL)
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "armorpenrtng", abs(spellproto->EffectBasePoints[j] + 1));
                }

                // Vanilla hit rating
                // SPELL_AURA_MOD_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_HIT_CHANCE)
                {
                    isAttackItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "hitrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla crit rating
                // SPELL_AURA_MOD_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_CRIT_PERCENT)
                {
                    isAttackItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "critstrkrtng", spellproto->EffectBasePoints[j] + 1);
                }

                //check defense SPELL_AURA_MOD_SKILL
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SKILL)
                {
                    if (spellproto->EffectMiscValue[j] == SKILL_DEFENSE)
                    {
                        isTankItem = true;
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "defrtng", spellproto->EffectBasePoints[j] + 1);
                    }
                }


                // mana regen
                // SPELL_AURA_MOD_POWER_REGEN
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_POWER_REGEN)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "manargn", spellproto->EffectBasePoints[j] + 1);
                }
            }
        }

        // different stat weight based on trigger
        float coverage = 1;

        if (spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
        {
            if (spellData.SpellCooldown != 0)
            {
                int32 spellDuration = GetSpellDuration(spellproto);
                coverage = static_cast<float>(spellDuration) / spellData.SpellCooldown;
            }
            else
            {
                coverage = 0.17f; //Most often trinkets have 20 seconds buff with 2 minute cooldown which means ~17%
            }
        }
        else if (spellData.SpellTrigger == ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
        {
            float averageItemDelay = 2.43f;
            coverage = (static_cast<float>(spellproto->procChance) / 100) * (static_cast<float>(GetSpellDuration(spellproto)) / averageItemDelay);

            if (coverage > 0.9f)
                coverage = 0.9f;
        }

        effectAuraStatWeight *= coverage;
        effectAuraHealStatWeight *= coverage;
        effectAuraApStatWeight *= coverage;
        effectAuraDamageStatWeight *= coverage;

        auraStatWeight += effectAuraStatWeight;
        auraHealStatWeight += effectAuraHealStatWeight;
        auraApStatWeight += effectAuraApStatWeight;
        auraDamageStatWeight += effectAuraDamageStatWeight;
    }

    // skip all 1h Maces for feral druids if they have no feral AP
    if (!isWhitelist && !isFeral && playerclass == CLASS_DRUID && proto->IsWeapon() && proto->SubClass == ITEM_SUBCLASS_WEAPON_MACE && (spec == 30 || spec == 32))
        return 0;

    statWeight += auraStatWeight;
    spellHeal += auraHealStatWeight;
    spellPower += auraDamageStatWeight;
    attackPower += auraApStatWeight;

    if (spellHeal > spellPower || isHealingItem)
        specType |= ITEM_SPEC_SPELL_HEALING;

    if (spellPower >= spellHeal)
        specType |= ITEM_SPEC_SPELL_DAMAGE;

    if (isTankItem && (noCaster || !hasMana || !spellHeal || (!isHealingItem && !isSpellDamageItem)))
        specType |= ITEM_SPEC_TANK;

    if (isAttackItem)
        specType |= ITEM_SPEC_ATTACK;

    if (!noCaster && (isCasterItem || hasInt || isSpellDamageItem))
        specType |= ITEM_SPEC_CASTER;


    // limit speed for tank weapons
    if (!isWhitelist && spec == 3 && proto->IsWeapon() && proto->Delay > 2300)
        return 0;

    if (!isWhitelist && spec == 5 && proto->IsWeapon() && proto->Delay > 2400)
        return 0;

    // check for caster item
    if (isCasterItem || hasInt || spellHeal || spellPower || isSpellDamageItem || isHealingItem)
    {
        if (!isWhitelist && (!hasMana || (noCaster && !(spec == 6 || spec == 30 || spec == 32 || spec == 21))) && (spellHeal || isHealingItem || isSpellDamageItem || spellPower))
            return 0;

        if (!isWhitelist && !hasMana && hasInt)
            return 0;

        if (!isWhitelist && !hasMana && noCaster && (spellPower > attackPower || spellHeal > attackPower))
            return 0;

        if (!isWhitelist && (spec != 6 && spec != 21) && !spellPower && !spellHeal && isSpellDamageItem)
            return 0;

        if (!isWhitelist && /*(spec != 6 && spec != 21) && */!spellHeal && isHealingItem && !isSpellDamageItem)
            return 0;

        if (!isWhitelist && (spec != 6 && spec != 21) && !noCaster && isSpellDamageItem && !spellPower && !(spellDamage && spellHealing && proto->IsWeapon() && proto->InventoryType == INVTYPE_WEAPONMAINHAND))
            return 0;

        bool playerCaster = false;
        for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
        {
            if (i->stat == "splpwr" || i->stat == "int" || i->stat == "manargn" || i->stat == "splheal" || i->stat == "spellcritstrkrtng" || i->stat == "spellhitrtng")
            {
                playerCaster = true;
            }
        }

        if (!isWhitelist && (spec != 6 && spec != 21 && playerclass != CLASS_HUNTER) && !playerCaster)
            return 0;
    }

    // check for caster item
    if (isAttackItem)
    {
        if (!isWhitelist && hasMana && !noCaster && !(hasInt || spellPower || spellHeal || isHealingItem || isSpellDamageItem))
            return 0;

        bool playerAttacker = false;
        for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
        {
            if (i->stat == "str" || i->stat == "agi" || i->stat == "atkpwr" || i->stat == "mledps" || i->stat == "rgddps" || i->stat == "hitrtng" || i->stat == "critstrkrtng")
            {
                playerAttacker = true;
            }
        }

        if (!isWhitelist && !playerAttacker)
            return 0;
    }


    itSpec = (ItemSpecType)specType;

    statWeight += spellPower;
    statWeight += spellHeal;
    statWeight += attackPower;
    statWeight += defenseStats;

    // handle negative stats
    if (basicStatsWeight < 0 && ((uint32)(abs(basicStatsWeight)) >= statWeight))
        statWeight = 0;
    else
        statWeight += basicStatsWeight;

    return statWeight;
}

uint32 RandomItemMgr::CalculateRandomEnchantId(uint8 playerclass, uint8 spec, ItemPrototype const* proto)
{
    if (!proto)
        return 0;

    // Random Property case
    if (uint32 randomPropId = RandomPropertyModIdCompat(proto))
    {
        if (!RandomPropertyEnchantIdCompat(randomPropId, 0) &&
            !RandomPropertyEnchantIdCompat(randomPropId, 1) &&
            !RandomPropertyEnchantIdCompat(randomPropId, 2))
        {
            return 0;
        }

        // check stats
        if (CalculateEnchantWeight(playerclass, spec, randomPropId))
            return randomPropId;
    }

    return 0;
}

uint32 RandomItemMgr::CalculateBestRandomEnchantId(uint8 playerclass, uint8 spec, uint32 itemId)
{
    if (!itemId)
        return 0;

    ItemPrototype const* proto = GetItemProtoByIdCompat(itemId);
    if (!proto)
        return 0;

    uint32 rawProp = RandomPropertyRawIdCompat(proto);
    if (!rawProp)
        return 0;

    std::map<uint32, std::vector<uint32> >::const_iterator tab = randomEnchantsCache.find(rawProp);
    if (tab == randomEnchantsCache.end())
        return 0;

    uint32 bestScore = 0;
    uint32 bestId = 0;

    const std::vector<uint32> propList = tab->second;
    for (auto propId : propList)
    {
        uint32 currScore = 0;
        for (uint8 i = 0; i < 3; ++i)
        {
            uint32 enchantId = RandomPropertyEnchantIdCompat(propId, i);
            currScore += CalculateEnchantWeight(playerclass, spec, enchantId);
        }

        if (currScore > bestScore)
        {
            bestScore = currScore;
            bestId = propId;
        }
    }

    return bestId;
}

uint32 RandomItemMgr::CalculateEnchantWeight(uint8 playerclass, uint8 spec, uint32 enchantId)
{
    if (!enchantId)
        return 0;

    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);

    if (!pEnchant)
        return 0;

    uint32 weight = 0;

    for (int s = 0; s < 3; ++s)
    {
        switch (EnchantEffectTypeCompat(pEnchant, s))
        {
        case 2: //Damage
            if (!EnchantEffectAmountCompat(pEnchant, s))
                continue;
            // Weapon-damage enchants (incl. scopes): melee specs score them
            // on "mledps", ranged specs on "rgddps". One of the two always
            // exists, so hunter scopes rank above zero and march up the
            // ladder via the tie-break instead of returning "no enchant".
            weight += CalculateSingleStatWeight(playerclass, spec, "mledps", EnchantEffectAmountCompat(pEnchant, s));
            weight += CalculateSingleStatWeight(playerclass, spec, "rgddps", EnchantEffectAmountCompat(pEnchant, s));
            break;
        case 3:
        {
            if (!EnchantEffectSpellCompat(pEnchant, s))
                continue;

            SpellEntry const* spellInfo = GetSpellEntryCompat(EnchantEffectSpellCompat(pEnchant, s));

            if (!spellInfo)
                continue;

            for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
            {
                if (spellInfo->Effect[j] != SPELL_EFFECT_APPLY_AURA)
                    continue;

                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_STAT)
                {
                    uint32 stat = spellInfo->EffectMiscValue[j];
                    uint32 value = spellInfo->EffectBasePoints[j] + 1;

                    if (!value)
                        continue;

                    if (ItemStatLink.find(stat) == ItemStatLink.end())
                        continue;

                    weight += CalculateSingleStatWeight(playerclass, spec, ItemStatLink[stat], value);
                }
                // spell damage
                // SPELL_AURA_MOD_DAMAGE_DONE
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_DAMAGE_DONE)
                {
                    uint32 spellDamage = spellInfo->EffectBasePoints[j] + 1;
                    // generic spell damage
                    if (spellInfo->EffectMiscValue[j] == SPELL_SCHOOL_MASK_MAGIC)
                    {
                        weight += CalculateSingleStatWeight(playerclass, spec, "splpwr", spellDamage);
                    }
                    else
                    {
                        uint32 specialDamage = 0;
                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_ARCANE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "arcsplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FROST) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "frosplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FIRE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "firsplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_SHADOW) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "shasplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_NATURE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "natsplpwr", spellDamage);

                        weight += specialDamage;
                    }
                }
                // spell healing
                // SPELL_AURA_MOD_HEALING_DONE
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_HEALING_DONE)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "splheal", spellInfo->EffectBasePoints[j] + 1);
                }
                // Flat health/mana from chest/mana enchants: stamina and
                // intellect are the closest weight scales (aura 34/35 is a
                // flat pool, not a percent — the +1 mirrors the other
                // branches' EffectBasePoints+1 convention).
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_INCREASE_HEALTH)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "sta", spellInfo->EffectBasePoints[j] + 1);
                }
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_INCREASE_ENERGY)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "int", spellInfo->EffectBasePoints[j] + 1);
                }
            }
            break;
        }
        case 4: //Armor
            if (!EnchantEffectAmountCompat(pEnchant, s))
                continue;
            weight += CalculateSingleStatWeight(playerclass, spec, "armor", EnchantEffectAmountCompat(pEnchant, s));
            break;
        case 5: //Stat
        {
            for (auto& statLink : weightStatLink)
            {
                if (statLink.second != EnchantEffectSpellCompat(pEnchant, s))
                    continue;

                weight += CalculateSingleStatWeight(playerclass, spec, statLink.first, EnchantEffectAmountCompat(pEnchant, s));
            }
            break;
        }
        case 6: //Totem
            break;
        case 7: //Use Spell
            break;
        }
    }

    return weight;
}


uint32 RandomItemMgr::CalculateRandomPropertyWeight(uint8 playerclass, uint8 spec, int32 randomPropertyId)
{
    uint32 weight = 0;
    if (randomPropertyId)
    {
        for (uint8 i = 0; i < 3; ++i)
        {
            uint32 enchantId = RandomPropertyEnchantIdCompat(abs(randomPropertyId), i);
            weight += CalculateEnchantWeight(playerclass, spec, enchantId);
        }
    }
    return weight;
}

uint32 RandomItemMgr::ItemStatWeight(Player* player, ItemQualifier& qualifier)
{
    ItemSpecType itSpec;
    uint32 weight = CalculateStatWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetProto(), itSpec);
    if(qualifier.GetEnchantId())
        weight += CalculateEnchantWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetEnchantId());
    if (qualifier.GetRandomPropertyId())
        weight += CalculateRandomPropertyWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetRandomPropertyId());
    return weight;
}

uint32 RandomItemMgr::ItemStatWeight(Player* player, Item* item)
{
    ItemQualifier itemQualifier(item);
    return ItemStatWeight(player, itemQualifier);
}

uint32 RandomItemMgr::CalculateSingleStatWeight(uint8 playerclass, uint8 spec, std::string stat, int32 value)
{
    uint32 statWeight = 0;
    for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
    {
        if (stat == i->stat)
        {
            // Compute in 64-bit and ignore non-positive contributions. A negative stat
            // value (item stat penalties, or negative aura EffectBasePoints) must not wrap
            // to a huge uint32 here — that both inflates the item's score and overflows the
            // signed MEDIUMINT scale_* cache columns, aborting the cache build on first boot.
            int64 weighted = (int64)i->weight * (int64)value;
            if (weighted <= 0)
                return 0;
            statWeight = (uint32)weighted;
            sLog.outDetail("stat: %s, val: %d, weight: %d, total: %d, class: %d, spec: %s", stat.c_str(), value, i->weight, statWeight, playerclass, m_weightScales[spec].info.name.c_str());
            return statWeight;
        }
    }

    return statWeight;
}

std::string RandomItemMgr::GetPlayerSpecName(Player* player)
{
    std::string specName;
    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->GetClass())
    {
    case CLASS_PRIEST:
        if (tab == 2)
            specName = "shadow";
        else if (tab == 1)
            specName = "holy";
        else
            specName = "disc";
        ;        break;
    case CLASS_SHAMAN:
        if (tab == 2)
            specName = "resto";
        else if (tab == 1)
        {
            // A shaman bot is a tank iff PlayerbotAI::IsTank says so (forced
            // role TANK or the tank strategy; the tank build still lives on
            // tab 1, so the tab alone cannot tell it apart from Enhancement
            // DPS). Tank bots score gear with the `shamantank` scale; when the
            // scale row is absent (DB not migrated) the id lookup below
            // returns 0 and every caller falls back as before.
            if (PlayerbotAI::IsTank(player))
                specName = "shamantank";
            else
                specName = "enhance";
        }
        else
            specName = "elem";
        break;
    case CLASS_WARRIOR:
        if (player->GetLevel() < 10)
            specName = "arms";
        else if (tab == 2)
            specName = "prot";
        else if (tab == 1)
            specName = "fury";
        else
            specName = "arms";
        break;
    case CLASS_PALADIN:
        if (tab == 0)
            specName = "holy";
        else if (tab == 1)
            specName = "prot";
        else if (tab == 2)
            specName = "retrib";
        break;
    case CLASS_DRUID:
        if (tab == 0)
            specName = "balance";
        else if (tab == 1)
        {
            specName = ResolveDruidFeralSpecName(player->GetLevel(), PlayerbotAI::IsTank(player));
        }
        else if (tab == 2)
            specName = "resto";
        break;
    case CLASS_ROGUE:
        if (tab == 0)
            specName = "assas";
        else if (tab == 1)
            specName = "combat";
        else if (tab == 2)
            specName = "subtle";
        break;
    case CLASS_HUNTER:
        if (tab == 0)
            specName = "beast";
        else if (tab == 1)
            specName = "marks";
        else if (tab == 2)
            specName = "surv";
        break;
    case CLASS_MAGE:
        if (tab == 0)
            specName = "arcane";
        else if (tab == 1)
            specName = "fire";
        else if (tab == 2)
            specName = "frost";
        break;
    case CLASS_WARLOCK:
        if (tab == 0)
            specName = "afflic";
        else if (tab == 1)
            specName = "demo";
        else if (tab == 2)
            specName = "destro";
        break;
    default:
        break;
    }
    return specName;
}

uint32 RandomItemMgr::GetPlayerSpecId(Player* player)
{
    std::string specName = GetPlayerSpecName(player);
    if (specName.empty())
        return 0;

    for (auto itr : m_weightScales)
    {
        if (itr.second.info.name == specName && itr.second.info.classId == player->GetClass())
            return itr.second.info.id;
    }
    return 0;
}

uint32 RandomItemMgr::GetFallbackSpecId(uint8 playerclass)
{
    // First weight scale registered for the class: generic enough for
    // pre-10 bots with no spent talents, filtered by weapon rules downstream.
    for (auto const& itr : m_weightScales)
        if (itr.second.info.classId == playerclass && itr.second.info.id)
            return itr.second.info.id;
    return 0;
}

std::string RandomItemMgr::GetSpecName(uint32 specId)
{
    auto itr = m_weightScales.find(specId);
    if (itr == m_weightScales.end() || !itr->second.info.id)
        return std::string();
    return itr->second.info.name;
}

uint32 RandomItemMgr::GetSpecIdFor(uint8 playerclass, std::string const& specName)
{
    for (auto const& itr : m_weightScales)
        if (itr.second.info.id && itr.second.info.classId == playerclass && itr.second.info.name == specName)
            return itr.second.info.id;
    return 0;
}

ai::ItemStatFamily RandomItemMgr::GetSpecStatFamily(uint32 specId)
{
    auto itr = m_weightScales.find(specId);
    if (itr == m_weightScales.end())
        return ai::ItemStatFamily::None;

    ai::SpecWeightSummary w;
    for (auto const& st : itr->second.stats)
    {
        if (st.stat == "str")
            w.strWeight += st.weight;
        else if (st.stat == "agi")
            w.agiWeight += st.weight;
        else if (st.stat == "int" || st.stat == "spi" || st.stat == "splpwr" ||
                 st.stat == "splheal" || st.stat == "manargn" || st.stat == "arcsplpwr" ||
                 st.stat == "frosplpwr" || st.stat == "firsplpwr" || st.stat == "shasplpwr" ||
                 st.stat == "natsplpwr" || st.stat == "spellhitrtng" || st.stat == "spellcritstrkrtng" ||
                 st.stat == "spellpenrtng")
            w.spellWeight += st.weight;
        else if (st.stat == "defrtng" || st.stat == "dodgertng" || st.stat == "parryrtng" ||
                 st.stat == "blockrtng" || st.stat == "block" || st.stat == "armor")
            w.tankWeight += st.weight;
    }

    return ai::DeriveSpecStatFamily(w);
}
