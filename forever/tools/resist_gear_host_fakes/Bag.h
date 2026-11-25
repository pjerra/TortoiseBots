// One worn bag: its size and the item in each of its slots.
#pragma once

#include "Define.h"
#include "Item.h"

#include <vector>

class Bag
{
public:
    explicit Bag(uint32 size) : _items(size, nullptr) {}

    uint32 GetBagSize() const { return uint32(_items.size()); }

    Item* GetItemByPos(uint32 slot) const
    {
        return slot < _items.size() ? _items[slot] : nullptr;
    }

    void SetItemByPos(uint32 slot, Item* item)
    {
        if (slot < _items.size())
            _items[slot] = item;
    }

private:
    std::vector<Item*> _items;
};
