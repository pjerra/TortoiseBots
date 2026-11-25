// A player's equipment and bags, with the core's in-place swap: SwapItem moves the item it finds at `src` into
// `dst` and whatever sat in `dst` into `src`, so nothing is created or destroyed. Positions are the core's packed
// form (bag in the high byte, slot in the low byte); bag INVENTORY_SLOT_BAG_0 is the equipment and the backpack.
#pragma once

#include "Bag.h"
#include "Define.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectGuid.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class Player
{
public:
    Player(uint64 key, std::string name) : _guid(ObjectGuid::Create<HighGuid::Player>(key)), _name(std::move(name)) {}

    static uint16 Key(uint8 bag, uint8 slot) { return uint16(bag) << 8 | uint16(slot); }

    ObjectGuid const& GetGUID() const { return _guid; }
    std::string const& GetName() const { return _name; }

    bool IsAlive() const { return _alive; }
    bool IsInCombat() const { return _inCombat; }
    Map* GetMap() const { return _map; }
    Group* GetGroup() const { return _group; }

    void SetAlive(bool alive) { _alive = alive; }
    void SetInCombat(bool inCombat) { _inCombat = inCombat; }
    void SetMap(Map* map) { _map = map; }
    void SetGroup(Group* group) { _group = group; }

    Item* GetItemByPos(uint8 bag, uint8 slot) const
    {
        auto const it = _pos.find(Key(bag, slot));
        return it == _pos.end() ? nullptr : it->second;
    }

    Item* GetItemByGuid(ObjectGuid const& guid) const
    {
        for (auto const& entry : _pos)
            if (entry.second->GetGUID() == guid)
                return entry.second;
        return nullptr;
    }

    Bag* GetBagByPos(uint8 bagSlot) const
    {
        for (BagSlot const& slot : _bags)
            if (slot.bagSlot == bagSlot)
                return slot.bag.get();
        return nullptr;
    }

    // The equipment slot an item would fill: the test gives each template its slot.
    uint8 FindEquipSlot(Item* item, uint32, bool) const
    {
        return item ? item->GetTemplate()->inventoryType == INVTYPE_FINGER ? 10 : uint8(item->GetTemplate()->equipSlot) : uint8(NULL_SLOT);
    }

    // The core's per-slot answer: a ring fits either finger, everything else needs its own slot.
    uint8 CanEquipItem(uint8 slot, uint16& dest, Item* item, bool) const
    {
        if (!item || slot >= EQUIPMENT_SLOT_END)
            return EQUIP_ERR_CANT_EQUIP_LEVEL_I;
        if (item->GetTemplate()->inventoryType == INVTYPE_FINGER)
        {
            if (slot != 10 && slot != 11)
                return EQUIP_ERR_CANT_EQUIP_LEVEL_I;
        }
        else if (slot != uint8(item->GetTemplate()->equipSlot))
            return EQUIP_ERR_CANT_EQUIP_LEVEL_I;
        dest = Key(INVENTORY_SLOT_BAG_0, slot);
        return EQUIP_ERR_OK;
    }

    void SwapItem(uint16 src, uint16 dst)
    {
        Item* const from = At(src);
        Item* const to = At(dst);
        _pos.erase(src);
        _pos.erase(dst);
        if (from)
            _pos.emplace(dst, from);
        if (to)
            _pos.emplace(src, to);
        if (from)
            from->SetPos(dst);
        if (to)
            to->SetPos(src);
    }

    // ---- the check's own helpers (not part of the core's Player) ----

    void Place(Item* item, uint8 bag, uint8 slot)
    {
        _pos[Key(bag, slot)] = item;
        item->SetPos(Key(bag, slot));
    }

    void AddBag(uint8 bagSlot, std::unique_ptr<Bag> bag)
    {
        _bags.push_back(BagSlot{ bagSlot, std::move(bag) });
    }

    // Takes an item out of the world: a looted bag row, or a piece the bot lost.
    void Remove(Item* item)
    {
        for (auto const& entry : _bags)
            for (uint32 slot = 0; slot < entry.bag->GetBagSize(); ++slot)
                if (entry.bag->GetItemByPos(slot) == item)
                    entry.bag->SetItemByPos(slot, nullptr);
        for (auto it = _pos.begin(); it != _pos.end();)
            it = it->second == item ? _pos.erase(it) : std::next(it);
    }

    // Moves a bag item between bag positions (the check's stand-in for looting into a free slot).
    void MoveBagItem(uint8 bag, uint8 slot, Item* item)
    {
        Place(item, bag, slot);
        if (bag != INVENTORY_SLOT_BAG_0)
            if (Bag* worn = GetBagByPos(bag))
                worn->SetItemByPos(slot, item);
    }

private:
    struct BagSlot
    {
        uint8 bagSlot;
        std::unique_ptr<Bag> bag;
    };

    Item* At(uint16 pos) const
    {
        auto const it = _pos.find(pos);
        return it == _pos.end() ? nullptr : it->second;
    }

    ObjectGuid _guid;
    std::string _name;
    bool _alive = true;
    bool _inCombat = false;
    Map* _map = nullptr;
    Group* _group = nullptr;
    std::map<uint16, Item*> _pos;
    std::vector<BagSlot> _bags;
};
