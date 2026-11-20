// The Turtle side of the both-core helpers the ForeverCore port added in slice 8, package K8 (mage, rogue, give item).
// Same names and signatures as forever/src/compat/TbCompat_K8.h; each is the expression the bot always made (the call is moved, not changed).
// Owned by the slice-8 K8 worker: no other package edits this file. Notes: forever/docs/SLICE8-K-NOTES.md.
#pragma once

// A stunned unit, or one about to be stunned. T: the two state bits of the original test (a Sapped pack mate; RogueActions.cpp, the Sap approach).
inline bool StunnedOrPendingStunCompat(Unit const* unit) { return unit->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_PENDING_STUNNED); }

// Hand a stack over: the item's owner field becomes the receiver (GiveItemAction.cpp, between MoveItemFromInventory and MoveItemToInventory).
inline void ItemSetOwnerCompat(Item* item, Unit const* owner) { item->SetOwnerGuid(owner->getObjectGuid()); }

// The spell state a bot's interrupt rule reads off a victim's cast (PlayerbotAI::IsInterruptableSpellCasting; ai/playerbot/InterruptCastingPolicy.h
// numbers them PREPARING = 1 and CASTING = 2). T: the state the core holds, as always.
inline uint32 InterruptSpellStateCompat(Spell const* spell) { return uint32(spell->getState()); }

// The item-cheat branch of ApplyPoisonAction (RogueActions.h): one BotUseItemSpell per on-use spell of the poison, cast onto the weapon.
// T: the original loop, moved word for word (SpellType is ai::BotUseItemSpell; setDuration is the action's SetDuration(3000)).
// True = at least one spell started (the old `return count`), false = a spell was refused.
template <class SpellType, class SetDurationFn>
inline bool PoisonItemCheatCastCompat(Player* bot, Item* weapon, ItemPrototype const* poisonProto, SetDurationFn setDuration)
{
    SpellCastTargets targets;
    targets.setItemTarget(weapon);
    targets.m_targetMask = TARGET_FLAG_ITEM;

    int count = 0;
    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        const _Spell& spellData = poisonProto->Spells[i];
        if (spellData.SpellId)
        {
            const SpellEntry* spellInfo = GetSpellEntryCompat(spellData.SpellId);
            if (spellInfo)
            {
                SpellType* spell = new SpellType(bot, spellInfo, (count > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);
                // NOTE: no manual cooldown here (see UseAction::UseItemInternal): the core applies it in Spell::SendSpellCooldown
                // after validation.
                if (spell->ForceSpellStart(&targets) == SPELL_CAST_OK)
                {
                    setDuration();
                }
                else
                {
                    return false;
                }

                ++count;
            }
        }
    }

    return count != 0;
}
