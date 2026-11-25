// A worn or bagged item as the resist-gear host reads it: its guid, its entry, the template (the equip slot and the
// inventory type) and where it currently sits in the player's bags or equipment.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

class ItemTemplate
{
public:
    uint32 inventoryType = 0;   // the core's INVTYPE_* id (a ring is INVTYPE_FINGER and fits either finger)
    uint32 equipSlot = 0;       // the equipment slot this item fills (the check sets it)
    uint32 GetInventoryType() const { return inventoryType; }
};

class Item
{
public:
    Item(uint64 counter, uint32 entry, ItemTemplate const* tmpl) : _guid(ObjectGuid::Create<HighGuid::Item>(counter)), _entry(entry), _tmpl(tmpl) {}

    ObjectGuid const& GetGUID() const { return _guid; }
    uint32 GetEntry() const { return _entry; }
    ItemTemplate const* GetTemplate() const { return _tmpl; }
    uint16 GetPos() const { return _pos; }
    void SetPos(uint16 pos) { _pos = pos; }

private:
    ObjectGuid _guid;
    uint32 _entry = 0;
    ItemTemplate const* _tmpl = nullptr;
    uint16 _pos = 0;
};
