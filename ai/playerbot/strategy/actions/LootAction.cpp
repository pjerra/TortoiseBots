
#include "playerbot/playerbot.h"
#include "../../policy/loot/SkinningLootPolicy.h"
#include "../../policy/loot/LootWindowPolicy.h"
#include "../../policy/loot/LootHistoryPolicy.h"
#include "../../policy/loot/LootSlotRightPolicy.h"
#include "../../policy/loot/CorpseLootPolicy.h"
#include "LootAction.h"
#include "playerbot/CastKeepsRunningPolicy.h"

#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/LootStrategyValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/quest/QuestLogPolicy.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "../../../../runtime/RemoteCommanderTasks.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "playerbot/GuildOrderSources.h"
#include "../../../../runtime/GuildOrderStore.h"


using namespace ai;

bool LootAction::Execute(Event& event)
{
    // The gather cast clears the loot target when it starts, so without this
    // the chain re-selected the node mid-cast and "open loot" re-fired into
    // the cast gate every tick, burning the three open attempts before the
    // running cast finished (#1062).
    if (ai::LootChainBlockedByCast(bot->IsNonMeleeSpellCasted(false, true, true)))
        return false;

    if (!AI_VALUE(bool, "has available loot"))
        return false;

    LootObject prevLoot = AI_VALUE(LootObject, "loot target");

    // The stack already dropped corpses this bot may not loot (round robin, another player's
    // tap) and corpses out of the master's safe range, so the nearest remaining entry is the
    // target. Re-filtering here duplicated those rules, and when they drifted apart the loot
    // action kept firing without ever selecting anything.
    LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);

    if (lootObject.IsEmpty())
    {
        // The stack filtered to nothing (stale corpses, master-range, TTL):
        // the trigger cache still offers "loot" for up to 5 s, and every
        // tick failed at full speed (ACTION_LOOP). Reset the caches so the
        // next tick re-evaluates instead of failing again on the same stale
        // answer.
        RESET_AI_VALUE(LootObject, "loot target");
        RESET_AI_VALUE(bool, "has available loot");
        return false;
    }

    bool released = false;
    if (!prevLoot.IsEmpty() && prevLoot.guid != lootObject.guid)
    {
        LootReleaseCompat(bot, prevLoot.guid);
        released = true;
    }

    sLog.outDebug("[BOT LOOT] %s: select target=%lu (prev=%lu released=%d)",
        CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid), GuidKeyCompat(prevLoot.guid), released ? 1 : 0);

    context->GetValue<LootObject>("loot target")->Set(lootObject);
    return true;
}

enum ProfessionSpells
{
    HERB_GATHERING               = 2366,
    MINING                       = 2575,
    SKINNING                      = 8613
};

bool OpenLootAction::Execute(Event& event)
{
    // mod-playerbots parity (OpenLootAction::Execute): never re-issue the
    // gather cast while one is running. The failure count below must not see
    // this tick either, or the three-strike abandon wedged the node before
    // the running cast finished (#1062).
    if (ai::LootChainBlockedByCast(bot->IsNonMeleeSpellCasted(false, true, true)))
        return false;

    LootObject lootObject = AI_VALUE(LootObject, "loot target");
    // DoLoot re-validates the object (Refresh), which empties lootObject.guid when it is
    // no longer lootable; keep the guid the attempt was made on.
    ObjectGuid const lootGuid = lootObject.guid;

    // A bot in a commanded group opens a game object only on the commander's
    // order (that order watches the object): a chest it picked before the bind
    // is dropped, not opened. The stack offers no other one (OrderByDistance).
    if (lootGuid.IsGameObject())
    {
        uint32 onlyItem = 0;
        bool const ordered = TortoiseBots::RemoteCommander::LootOrders().Watching(
            GuidKeyCompat(bot->GetObjectGuid()), GuidKeyCompat(lootGuid), onlyItem);
        if (!TortoiseBots::RemoteCommander::OwnLootTakesObject(true,
                !ordered && TortoiseBots::BotCommands::IsInCommandedGroup(bot), ordered))
        {
            context->GetValue<LootObject>("loot target")->Set(LootObject());
            return false;
        }
    }

    stoppedForCast = false;
    // The duration is a member: a corpse open set it to lootDelay, and a cast
    // must not inherit that (or any earlier value) as its sleep.
    SetDuration(sPlayerbotAIConfig.reactDelay);
    bool result = DoLoot(lootObject);

    // A cast on the object (gather, skin, open) sleeps through its cast time,
    // like any cast action; waking after 1 s let "update follow" walk the bot
    // off the node and the core cancelled the cast (#1062).
    {
        Spell* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        uint32 const castMs = (result && cast && cast->getState() == SPELL_STATE_PREPARING && cast->GetCastTime() > 0)
            ? ai->GetSpellCastDuration(cast) : 0;
        SetDuration(ai::DecideOpenLootDuration(result, castMs, GetDuration()));
    }
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");

    // Bounded give-up: count every open attempt per object on the stack's failure memory
    // (the same one "move to loot" uses for unreachable corpses). A failed DoLoot
    // ("not now": failed cast, contended game object, no opening spell) is an attempt,
    // and so is a "successful" one: for a skin/herb/ore/lock open, success only means the
    // cast started, and a CMSG_LOOT only means the request was sent. When that cast never
    // completes or the server refuses the window, the object stays gatherable, the next
    // "add gathering loot"/"add all loot" sweep re-queues it, and the bot cycled
    // open -> add -> select -> open on the spot for minutes (live: 11 of 108 standstill runs
    // of 3+ min, no loot and no skill-up). StoreLootAction forgets the count once a window
    // pays out, so only opens that never yield anything add up; after three the object is
    // abandoned for 120 s and the bot moves on. Player-ordered looting only issues the
    // action once per order, so a counter that needs repeated attempts can never veto a
    // fresh order.
    // A stop before the cast (DoLoot, StopForObjectCast) is no attempt: the
    // cast comes on the next tick.
    if (!lootGuid.IsEmpty() && lootStack && !stoppedForCast)
        lootStack->NoteApproachFailure(lootGuid);

    if (result)
    {
        if (lootStack)
            lootStack->Remove(lootGuid);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
        return true;
    }

    if (!lootGuid.IsEmpty() && lootStack && lootStack->IsAbandoned(lootGuid))
    {
        sLog.outDebug("[BOT LOOT] %s: giving up on guid=%lu after repeated failed opens",
            CStrCompat(bot->GetName()), GuidKeyCompat(lootGuid));
        lootStack->Remove(lootGuid);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
    }
    return false;
}

bool OpenLootAction::DoLoot(LootObject& lootObject)
{
    if (lootObject.IsEmpty())
        return false;

    sLog.outDebug("[BOT LOOT] %s: DoLoot target=%lu", CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid));

    Creature* creature = ai->GetCreature(lootObject.guid);

    // Re-confirm the creature is still a fresh, lootable corpse before sending CMSG_LOOT.
    // The cached UNIT_DYNFLAG_LOOTABLE can lag behind a corpse that has despawned or
    // respawned while the bot was busy chain-killing (corpses age up to 30s in the loot
    // stack). A stale entry makes the server reply with a loot error (DIDNT_KILL) and the
    // bot kneel/abort on a non-corpse. Same predicate LootObjectStack::Refresh uses.
    if (creature && sServerFacade.GetDeathState(creature) != CORPSE)
    {
        sLog.outDebug("[BOT LOOT] %s: not a fresh CORPSE (deathstate=%d), dropping guid=%lu",
            CStrCompat(bot->GetName()), (int)sServerFacade.GetDeathState(creature), GuidKeyCompat(lootObject.guid));
        AI_VALUE(LootObjectStack*, "available loot")->Remove(lootObject.guid);
        RESET_AI_VALUE(LootObject, "loot target");
        return false;
    }

    // Gate the loot send on the SERVER's exact loot-range rule (LootObject::IsInLootRange: 3D
    // GetMaxLootDistance for corpses, INTERACTION_DISTANCE for game objects). The old creature
    // gate here was 3D but "can loot" still compared 2D distance, so on a slope the bot fired
    // CMSG_LOOT from a few yards above/below a corpse it had not reached, the server replied
    // TOO_FAR, and the same 8.0-priority action re-fired every tick while "move to loot" (7.0)
    // never got to walk the last yards.
    if (!lootObject.IsInLootRange(bot))
    {
        if (creature)
            sLog.outDebug("[BOT LOOT] %s: not in loot range (dist2d=%.1f maxLoot=%.1f), keep approaching guid=%lu",
                CStrCompat(bot->GetName()), sServerFacade.getDistance2d(bot, creature), LootDistanceCompat(bot, creature), GuidKeyCompat(lootObject.guid));
        else
            sLog.outDebug("[BOT LOOT] %s: not in loot range, keep approaching guid=%lu",
                CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid));
        return false;
    }

    // Items first, also on a corpse the core already flagged skinnable: skinning is refused
    // while any corpse loot is left (Spell::CheckCast -> SPELL_FAILED_TARGET_NOT_LOOTED), so the
    // old !UNIT_FLAG_SKINNABLE requirement made beast corpses unlootable (non-skinners ignored
    // them) and left skinners casting Skinning on an unlooted corpse every tick forever.
    // ai::CorpseHasBodyLootFor also matches the viewer's own loot right, so an allowed corpse
    // without the raw flag (its only loot left this bot's quest item) sends CMSG_LOOT here,
    // not a skinning/gather spell.
    if (creature && ai::CorpseHasBodyLootFor(bot, creature))
    {
        // Dismount in the open step (mod-playerbots LootAction.cpp): "loot available"
        // selects loot while mounted, so this is where a mounted bot gets off.
        if (bot->IsMounted())
            ai->Unmount();

        if (!lootObject.IsLootPossible(bot)) //Clear loot if bot can't loot it.
        {
            sLog.outDebug("[BOT LOOT] %s: IsLootPossible=false on lootable corpse, clearing (corpse stays lootable -> may re-add)",
                CStrCompat(bot->GetName()));
            return true;
        }

        sLog.outDebug("[BOT LOOT] %s: loot ctx guid=%lu alive=%d tapped=%d dist2d=%.1f maxLoot=%.1f",
            CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid),
            creature->IsAlive() ? 1 : 0, CreatureIsTappedByCompat(creature, bot) ? 1 : 0,
            sServerFacade.getDistance2d(bot, creature), LootDistanceCompat(bot, creature));

        sLog.outDebug("[BOT LOOT] %s: sending CMSG_LOOT (crouch emote) guid=%lu lootDelay=%u",
            CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid), sPlayerbotAIConfig.lootDelay);

        // Open the window (T: the packet; F: the core's own handler and the "loot response" event, TbCompat_I2.h). A corpse the core
        // refuses to open (F sends nothing back) is a failed open: false, counted by OpenLootAction's attempt memory.
        bool const windowOpened = LootOpenCompat(ai, bot, lootObject.guid);
        SetDuration(sPlayerbotAIConfig.lootDelay);

        if (isRealPlayer_Helper(bot))
        {
            WorldPacket data(SMSG_EMOTE, 4 + 8);
            data << uint32(EMOTE_ONESHOT_LOOT);
            data << bot->getObjectGuid();
            bot->GetSession()->SendPacket(&data);
        }

        return windowOpened;
    }

    // Gathering, skinning and opening game objects (a cast on the object and the window that follows) are not ported: lead decision D2,
    // nothing below this line runs on ForeverCore ("loot: gathering, skinning and game objects"). False = the open failed, which
    // OpenLootAction counts and abandons after three tries.
    if (LootGatherUnportedCompat())
        return false;

    if (creature)
    {
        SkillType skill = (SkillType)GetRequiredLootSkillCompat(creature->GetCreatureInfo());
        sLog.outDebug("[BOT LOOT] %s: gather/skin path skill=%u reqValue=%u", CStrCompat(bot->GetName()), skill, lootObject.reqSkillValue);
        if (!CanOpenLock(skill, lootObject.reqSkillValue))
        {
            sLog.outDebug("[BOT LOOT] %s: CanOpenLock=false (skill %u), abort", CStrCompat(bot->GetName()), skill);
            return false;
        }

        switch (skill)
        {
        case SKILL_ENGINEERING:
            return false;
        case SKILL_HERBALISM:
            return ai->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(HERB_GATHERING, creature) : false;
        case SKILL_MINING:
            return ai->HasSkill(SKILL_MINING) ? ai->CastSpell(MINING, creature) : false;
        default:
            return ai->HasSkill(SKILL_SKINNING) ? ai->CastSpell(SKINNING, creature) : false;
        }
    }

    // Dismount for a game-object node like the corpse path above: a bot that
    // rode to its node would otherwise burn its approach failures on casts
    // that never start, the node is abandoned, and the travel action
    // re-walks the same last yards.

    if (bot->IsMounted())
        ai->Unmount();

    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && (go->getLootState() == GO_ACTIVATED || go->GetGoState() == GO_STATE_ACTIVE))
        return false;

    // The cast on the object (Opening, Mining, Herb Gathering, a key) breaks on
    // the first step, and a bot that just walked here may still be on its last
    // yards. Stop it and cast on a later tick, once it stands still.
    if (ai->StopForObjectCast())
    {
        stoppedForCast = true;
        sLog.outDebug("[BOT LOOT] %s: still moving, stopped before the cast on guid=%lu", CStrCompat(bot->GetName()), GuidKeyCompat(lootObject.guid));
        return false;
    }

    if (lootObject.skillId == SKILL_MINING)
        return ai->HasSkill(SKILL_MINING) ? ai->CastSpell(MINING, bot) : false;

    if (lootObject.skillId == SKILL_HERBALISM)
    {
        // herb-like quest objects
        bool isForQuest = false;
        if (go && sObjectMgr.IsGameObjectForQuests(lootObject.guid.GetEntry()))
        {
            if (go->ActivateToQuest(bot))
            {
                std::list<uint32> lootItems = GAI_VALUE2_COMPAT(std::list<uint32>, "entry loot list", -1*int32(go->GetEntry()));
                isForQuest = !lootItems.empty() || go->getLootState() != GO_READY;
            }
        }

        if (!isForQuest)
        {
            return ai->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(HERB_GATHERING, bot) : false;
        }
    }

    uint32 spellId = GetOpeningSpell(lootObject);
    if (!spellId)
    {
        sLog.outDebug("[BOT LOOT] %s: GO no opening spell, abort", CStrCompat(bot->GetName()));
        return false;
    }

    if (!lootObject.IsLootPossible(bot)) //Clear loot if bot can't loot it.
    {
        sLog.outDebug("[BOT LOOT] %s: GO IsLootPossible=false, clearing", CStrCompat(bot->GetName()));
        return true;
    }

    sLog.outDebug("[BOT LOOT] %s: GO opening with spell=%u guid=%lu", CStrCompat(bot->GetName()), spellId, GuidKeyCompat(lootObject.guid));

    //Keys need to use the key
    if (spellId == sPlayerbotAIConfig.openGoSpell && go && lootObject.reqItem && bot->HasItemCount(lootObject.reqItem,1,false))
    {
        return ai->DoSpecificAction("use", Event("do loot", chat->formatQItem(lootObject.reqItem) + " " + chat->formatGameobject(go)));
    }

    return ai->CastSpell(spellId, bot);
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)
{
    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && sServerFacade.isSpawned(go))
        return GetOpeningSpell(lootObject, go);

    return 0;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject, GameObject* go)
{
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

		if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(spellId))
			continue;

		if (spellId == MINING || spellId == HERB_GATHERING)
			continue;

		const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
		if (!pSpellInfo)
			continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    for (uint32 spellId = 0; spellId < sServerFacade.GetSpellInfoRows(); spellId++)
    {
        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

		const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
		if (!pSpellInfo)
            continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    return sPlayerbotAIConfig.openGoSpell;
}

bool OpenLootAction::CanOpenLock(LootObject& lootObject, const SpellEntry* pSpellInfo, GameObject* go)
{
    for (int effIndex = 0; effIndex <= EFFECT_INDEX_2; effIndex++)
    {
        if (pSpellInfo->Effect[effIndex] != SPELL_EFFECT_OPEN_LOCK && pSpellInfo->Effect[effIndex] != SPELL_EFFECT_SKINNING)
            return false;

        uint32 lockId = go->GetGOInfo()->GetLockId();
        if (!lockId)
            return false;

        LockEntry const *lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        bool reqKey = false;                                    // some locks not have reqs

        for(int j = 0; j < 8; ++j)
        {
            switch(lockInfo->Type[j])
            {
            /*
            case LOCK_KEY_ITEM:
                return true;
            */
            case LOCK_KEY_SKILL:
                {
                    if(uint32(pSpellInfo->EffectMiscValue[effIndex]) != lockInfo->Index[j])
                        continue;

                    uint32 skillId = SkillByLockType(LockType(lockInfo->Index[j]));
                    if (skillId == SKILL_NONE)
                        return true;

                    if (CanOpenLock(skillId, lockInfo->Skill[j]))
                        return true;
                }
            }
        }
    }

    return false;
}

bool OpenLootAction::CanOpenLock(uint32 skillId, uint32 reqSkillValue)
{
    uint32 skillValue = bot->GetSkillValue(skillId);
    return skillValue >= reqSkillValue || !reqSkillValue;
}

// The gathering skill a game object's lock asks for (herbalism / mining / ...), i.e. the value
// LootObject::Refresh derives when it classifies the node. Refresh only looks at an object that
// is still GO_READY, so it returns nothing for the node whose loot window is open right now.
uint32 ai::GetNodeGatherSkill(GameObject* go)
{
    if (!go)
        return SKILL_NONE;

    LockEntry const* lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lockInfo)
        return SKILL_NONE;

    for (int i = 0; i < 8; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;

        if (uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i])))
            return skillId;
    }

    return SKILL_NONE;
}

// Where one loot window came from, for the history row: the corpse, chest or
// gathering node name the addon shows. The object's live name wins (it carries
// any rename), the template name is the fallback once the object is gone. A
// skin keeps its corpse: the GatherLoot row already says it was skinned.
static void DescribeLootSource(PlayerbotAI* ai, ObjectGuid guid, uint32 gatherSkill,
    std::string& type, std::string& name)
{
    type = "other";
    name.clear();

    if (guid.IsCreature())
    {
        type = "corpse";
        if (Creature* creature = ai->GetCreature(guid))
            name = creature->GetName();
        if (name.empty())
            if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(guid.GetEntry()))
                name = CreatureNameCompat(info);
        return;
    }

    GameObjectInfo const* goInfo = nullptr;
    if (GameObject* go = ai->GetGameObject(guid))
        goInfo = go->GetGOInfo();
    if (!goInfo)
        goInfo = sObjectMgr.GetGameObjectInfo(guid.GetEntry());

    if (goInfo && goInfo->type == GAMEOBJECT_TYPE_CHEST)
        type = "chest";
    else if (gatherSkill != SKILL_NONE)
        type = "gathering";
    else if (guid.IsGameObject())
        type = "object";

    if (goInfo)
        name = goInfo->name;
}

// Live copy of a stored item row for every real player in the bot's group:
// the same TBM:LOOT line the .bot lootlog reply sends, pushed as the bot loots.
// Sent on the addon channel so group members without the addon never see raw lines.
static void NotifyOwnerOfLoot(PlayerbotAI* ai, Player* bot, std::string item, uint32 itemId,
    std::string const& sourceType, std::string source, uint32 quality)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return;

    std::string timestamp = sPlayerbotAIConfig.GetTimestampStr() + "+00";
    std::string botName = bot->GetName();
    std::string qualityText = std::to_string(quality);
    std::string head = "TBM:LOOT|" + timestamp + "|" + botName + "|StoreLootAction|" + std::to_string(itemId) + "|";
    size_t fixedBytes = head.size() + 1 + sourceType.size() + 1 + 1 + qualityText.size();
    ai::LootHistoryPolicy::FitLootWireLine(item, source, fixedBytes);
    for (std::string* field : { &item, &source })
        for (char& character : *field)
            if (character == '|' || character == '\r' || character == '\n')
                character = ' ';

    std::string wireLine = head + item + "|" + sourceType + "|" + source + "|" + qualityText;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || member == bot || !member->IsInWorld() || !ai->IsRealPlayer(member))
            continue;
        SendAddonMessageCompat(*member, "TBM", wireLine);
    }
}

bool StoreLootAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p(event.GetPacket()); // (8+1+4+1+1+4+4+4+4+4+1)
    ObjectGuid guid;
    uint8 loot_type = 0;
    uint32 gold = 0;
    uint8 items = 0;
    uint8 lootError = 0;

    // The head of the loot response: T reads it from SMSG_LOOT_RESPONSE; F's event carries only the corpse guid and the money
    // comes from the core's Loot object (LootWindowReadCompat, TbCompat_I2.h). False = no window (T: a loot error packet).
    if (!LootWindowReadCompat(bot, p, guid, loot_type, gold, items, lootError))
    {
        // Not a loot response but a loot ERROR packet from Player::SendLootError:
        // [guid][uint8 loot_type=0][uint8 errorCode]. A real loot response (even an empty
        // one) always carries the gold + item-count block, so size <= 10 means the server
        // rejected the loot. Decode the trailing error byte so the rejection isn't a mystery.

        const char* errName =
            lootError == 0 ? "DIDNT_KILL/no-permission" :
            lootError == 4 ? "TOO_FAR" : "other";
        sLog.outDebug("[BOT LOOT] %s: loot REJECTED guid=%lu error=%u (%s)",
            CStrCompat(bot->GetName()), GuidKeyCompat(guid), lootError, errName);

        // Drop the corpse so the bot stops retrying a loot the server won't grant. Abandon it
        // rather than just unqueue it: the core still shows it lootable, so "add all loot"
        // re-queued it on the next sweep and the refused open repeated on the same spot. A
        // player's explicit loot order is only unqueued, so the order is never silently
        // vetoed by a transient refusal (TOO_FAR, a scripted mob not yet lootable).
        LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
        if (DecideAfterLootWindow(true, false, lootStack->TakePlayerOrder(guid)) == LootWindowVerdict::Abandon)
            lootStack->Abandon(guid);
        else
            lootStack->Remove(guid);
        RESET_AI_VALUE(LootObject, "loot target");
        // A remote commander's loot order waits for this window: tell it the server refused.
        TortoiseBots::RemoteCommander::LootOrders().NoteWindowEnd(GuidKeyCompat(bot->GetObjectGuid()),
            GuidKeyCompat(guid), true);
        return false;
    }

    sLog.outDebug("[BOT LOOT] %s: StoreLoot guid=%lu type=%u gold=%u items=%u",
        CStrCompat(bot->GetName()), GuidKeyCompat(guid), loot_type, gold, items);

    // Profession loot is worth its own event: a StoreLootAction row cannot be told apart from
    // ordinary corpse loot afterwards, and skinning/gathering had no telemetry at all - the
    // "zero leather in five weeks" reading had to be dug out of item ids in loot.log. The wire
    // loot_type cannot identify a skin (#403): the core rewrites LOOT_SKINNING to LOOT_PICKPOCKETING
    // for the 1.12 client (Player::SendLoot), so every skin arrives as LOOT_PICKPOCKETING. The
    // server-side Loot still carries the real type (loot->loot_type), which is what marks a skin.
    // A node window (herb, ore, opened chest) also arrives as LOOT_SKINNING (EffectOpenLock), so a
    // game object's skill comes from its lock, never from the loot type.
    SetLootGuidCompat(bot, guid);

    Loot* loot = LootOfWindowCompat(bot, guid);

    if (!loot)
    {
        sLog.outDebug("[BOT LOOT] %s: StoreLoot GetLoot returned null, releasing to avoid crouch loop", CStrCompat(bot->GetName()));
        // Release the corpse so the bot stops the loot (kneel) animation instead of staying
        // crouched forever, and drop it from the loot stack so it isn't retried next tick.
        LootReleaseCompat(bot, guid);
        AI_VALUE(LootObjectStack*, "available loot")->Remove(guid);
        RESET_AI_VALUE(LootObject, "loot target");
        TortoiseBots::RemoteCommander::LootOrders().NoteWindowEnd(GuidKeyCompat(bot->GetObjectGuid()),
            GuidKeyCompat(guid), true);
        return false;
    }

    static_assert((uint32)LOOT_SKINNING == ai::kSkinningLootType, "SkinningLootPolicy kSkinningLootType drifted from core LOOT_SKINNING");
    static_assert((uint32)SKILL_HERBALISM == ai::kHerbalismSkillId, "SkinningLootPolicy kHerbalismSkillId drifted from core SKILL_HERBALISM");
    static_assert((uint32)SKILL_MINING == ai::kMiningSkillId, "SkinningLootPolicy kMiningSkillId drifted from core SKILL_MINING");
    static_assert((uint32)SKILL_SKINNING == ai::kSkinningSkillId, "SkinningLootPolicy kSkinningSkillId drifted from core SKILL_SKINNING");
    LootType const internalLootType = LootAccess(loot).lootType();
    uint32 gatherSkill = ai::DecideGatherSkillForLoot((uint32)internalLootType, guid.IsGameObject(),
        guid.IsGameObject() ? ai::GetNodeGatherSkill(ai->GetGameObject(guid)) : (uint32)SKILL_NONE);

    uint32 itemsTaken = 0;

    // A remote commander's loot order on this very object (cmd loot / cmd open): an explicit
    // order takes its item (or everything the group rules give this bot) without the strategy's
    // loot filter, and the window reports what it took.
    uint64 const botRaw = GuidKeyCompat(bot->GetObjectGuid());
    uint32 orderedItem = 0;
    bool const commanderOrder = TortoiseBots::RemoteCommander::LootOrders().Watching(botRaw, GuidKeyCompat(guid), orderedItem);

    std::string lootSourceType;
    std::string lootSourceName;
    DescribeLootSource(ai, guid, gatherSkill, lootSourceType, lootSourceName);

    // An order for one item leaves the money on the corpse.
    // T: the packet, always taken. F: the core's own handler, refused (false) only for a window the core would divide by zero on
    // (LootMoneyCompat); nothing was taken then, so the window "gave nothing" below.
    if (gold > 0 && !(commanderOrder && orderedItem) && !LootMoneyCompat(bot))
        gold = 0;
    else if (gold > 0 && !(commanderOrder && orderedItem))
    {
        if (commanderOrder)
            TortoiseBots::RemoteCommander::LootOrders().NoteCopper(botRaw, GuidKeyCompat(guid), gold);

        // Money taken before the item loop, so gold-only corpses (most low-level
        // kills) used to leave no trace: income was unmeasurable and a bot that
        // opened a corpse but took only money looked identical to one that never
        // opened it. Log the copper amount so bot_events.csv can correlate kills
        // with looting.
        sPlayerbotAIConfig.logEvent(ai, "LootMoney", std::to_string(gold), std::to_string(guid.GetEntry()),
            lootSourceType, lootSourceName);
    }

    for (uint8 i = 0; i < items; ++i)
    {
        uint32 itemid;
        uint32 randomPropertyId;
        uint32 itemcount;
        uint8 lootslot_type;
        uint8 itemindex;
        bool grab = false;

        p >> itemindex;
        p >> itemid;
        p >> itemcount;
        p.read_skip<uint32>();  // display id
        p.read_skip<uint32>();  // randomSuffix
        p >> randomPropertyId;  // randomPropertyId
        p >> lootslot_type;     // 0 = can get, 1 = look only, 2 = master get

        ItemQualifier itemQualifier(itemid, ((int32)randomPropertyId));

		if (lootslot_type != LOOT_SLOT_NORMAL
            )
		{
			sLog.outDebug("[BOT LOOT] %s: skip item=%u slot_type=%u (not normal/owner)", CStrCompat(bot->GetName()), itemid, lootslot_type);
			continue;
		}

        if (commanderOrder)
        {
            if (!TortoiseBots::RemoteCommander::LootItemWanted(orderedItem, itemid))
                continue;
            ItemPrototype const* orderedProto = GetItemProtoByIdCompat(itemid);
            if (!orderedProto || (orderedProto->MaxCount > 0 && bot->HasItemCount(itemid, orderedProto->MaxCount, true)))
                continue;
        }
        else if (!ai::ShouldSkipLootFilter((uint32)internalLootType, guid.IsGameObject(), gatherSkill) &&
            !IsLootAllowed(itemQualifier, ai))
        {
            // A Working skin order needs the corpse empty before the
            // Skinning cast is legal, so kills of order mobs give up
            // their grey loot too.
            if (!guid.IsCreature() || !GuildOrderSkinMobFor(bot, guid.GetEntry()))
            {
                sLog.outDebug("[BOT LOOT] %s: skip item=%u (IsLootAllowed=false)", CStrCompat(bot->GetName()), itemid);
                continue;
            }
        }

        if (AI_VALUE2(uint32, "stack space for item", itemid) < itemcount)
        {
            sLog.outDebug("[BOT LOOT] %s: skip item=%u (no stack space, need=%u)", CStrCompat(bot->GetName()), itemid, itemcount);
            std::optional<TortoiseBots::GuildOrders::OrderRecord> order =
                TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
            if (order && order->itemId == itemid && order->state == TortoiseBots::GuildOrders::OrderState::Working &&
                bot->GetItemCount(itemid) > 0)
                TortoiseBots::GuildOrders::NoteBagsFull(bot->GetGUIDLow());
            continue;
        }

        ItemPrototype const *proto = sItemStorage.LookupEntry<ItemPrototype>(itemid);
        if (!proto)
            continue;

        // The facts of the slot in the 1.12 loot view (T) behind the take decision; F reads no slots (items are not taken, D2).
        bool slotAllowedForPlayer = false, slotIsQuest = false, slotIsFfa = false, slotIsBlocked = false, slotSharedAllowed = false;
        if (!LootSlotFactsCompat(bot, loot, itemindex, slotAllowedForPlayer, slotIsQuest, slotIsFfa, slotIsBlocked, slotSharedAllowed))
            continue;

        if (!ai::BotMayTakeLootSlot(slotAllowedForPlayer, slotIsQuest, slotIsFfa, slotIsBlocked, slotSharedAllowed))
        {
            sLog.outDebug("[BOT LOOT] %s: skip item=%u (no right: blocked=%u quest=%u ffa=%u)", CStrCompat(bot->GetName()), itemid,
                slotIsBlocked ? 1 : 0, slotIsQuest ? 1 : 0, slotIsFfa ? 1 : 0);
            continue;
        }

        Player* master = ai->GetMaster();
        if (sRandomBotFacade.IsRandomBot(bot) && master)
        {
            uint32 price = itemcount * ItemUsageValue::GetBotBuyPrice(proto, bot) + gold;
            if (price)
                sRandomBotFacade.AddTradeDiscount(bot, master, price);
        }

        uint32 const countBefore = commanderOrder ? bot->GetItemCount(itemid) : 0;
        LootAutostoreCompat(bot, itemindex);
        ++itemsTaken;
        if (commanderOrder)
        {
            // Only what really reached the bags counts (the core refuses a full bag or a unique item).
            uint32 const countAfter = bot->GetItemCount(itemid);
            if (countAfter > countBefore)
                TortoiseBots::RemoteCommander::LootOrders().NoteItem(botRaw, GuidKeyCompat(guid), itemid,
                    std::min(itemcount, countAfter - countBefore));
        }
        sLog.outDebug("[BOT LOOT] %s: take item=%u x%u", CStrCompat(bot->GetName()), itemid, itemcount);

        if (proto->Quality > ITEM_QUALITY_NORMAL && !urand(0, 50) && ai->HasStrategy("emote", BotState::BOT_STATE_NON_COMBAT)) ai->PlayEmote(TEXTEMOTE_CHEER);
        if (proto->Quality >= ITEM_QUALITY_RARE && !urand(0, 1) && ai->HasStrategy("emote", BotState::BOT_STATE_NON_COMBAT)) ai->PlayEmote(TEXTEMOTE_CHEER);

        if (requester && (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT) || (requester->GetMapId() != bot->GetMapId() || WorldPosition(requester).sqDistance2d(bot) > (sPlayerbotAIConfig.sightDistance * sPlayerbotAIConfig.sightDistance))))
        {
            std::map<std::string, std::string> args;
            args["%item"] = chat->formatItem(itemQualifier);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("loot_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        sPlayerbotAIConfig.logEvent(ai, "StoreLootAction", proto->Name1, std::to_string(proto->ItemId),
            lootSourceType, lootSourceName, std::to_string(proto->Quality));
        NotifyOwnerOfLoot(ai, bot, proto->Name1, proto->ItemId, lootSourceType, lootSourceName, proto->Quality);

        if (gatherSkill != SKILL_NONE)
            sPlayerbotAIConfig.logEvent(ai, "GatherLoot", std::to_string(gatherSkill), std::to_string(proto->ItemId));
    }

    // The open paid out: forget the attempts OpenLootAction counted on this object, so a
    // multi-use ore vein or a loot-then-skin corpse is never cut short. A window that gave
    // this bot nothing (every item under a roll, view-only, unwanted, or no bag room) leaves
    // the corpse lootable for the core: abandon it so the next "add all loot" sweep does not
    // re-queue it and re-open the same empty window. A player's explicit order is only
    // unqueued and keeps its open count (LootWindowPolicy).
    LootObjectStack* lootStack = AI_VALUE(LootObjectStack*, "available loot");
    bool const playerOrdered = lootStack->TakePlayerOrder(guid);
    switch (DecideAfterLootWindow(false, itemsTaken || gold, playerOrdered))
    {
    case LootWindowVerdict::ForgetFailures:
        lootStack->ForgetFailures(guid);
        break;
    case LootWindowVerdict::Abandon:
        sLog.outDebug("[BOT LOOT] %s: abandoning guid=%lu (window gave nothing)",
            CStrCompat(bot->GetName()), GuidKeyCompat(guid));
        lootStack->Abandon(guid);
        break;
    case LootWindowVerdict::KeepCount:
        break;
    }

    lootStack->Remove(guid);
    RESET_AI_VALUE(LootObject, "loot target");
    RESET_AI_VALUE2(bool, "should loot object", GuidQualifierCompat(guid));

    sLog.outDebug("[BOT LOOT] %s: StoreLoot done guid=%lu items_taken=%u gold=%u, release sent",
        CStrCompat(bot->GetName()), GuidKeyCompat(guid), itemsTaken, gold);

    // release loot
    LootReleaseCompat(bot, guid);

    ai->AccelerateRespawn(guid);

    if (commanderOrder)
        TortoiseBots::RemoteCommander::LootOrders().NoteWindowEnd(botRaw, GuidKeyCompat(guid), false);

    return true;
}

bool StoreLootAction::IsLootAllowed(ItemQualifier& itemQualifier, PlayerbotAI *ai)
{
    AiObjectContext *context = ai->GetAiObjectContext();

    // Item looting is not ported on ForeverCore (lead decision D2): no item is allowed. (T: false, the checks below run.)
    if (ItemLootingUnportedCompat())
        return false;

    ItemPrototype const* proto = GetItemProtoByIdCompat(itemQualifier.GetId());
    if (!proto)
        return false;

    std::set<uint32>& lootItems = AI_VALUE(std::set<uint32>&, "always loot list");
    if (lootItems.find(itemQualifier.GetId()) != lootItems.end())
        return true;

    // Bone Chew Toy (item 51751): the only quest needing it is the inactive
    // 40298, so it is never worth a bag slot. Keyed off the item id, not a
    // generic quest-class rule, so quest starters (Free Ticket Voucher
    // 19338 etc.) keep looting normally.
    if (itemQualifier.GetId() == ai::kBoneChewToyItemId)
        return false;

    std::set<uint32>& skipItems = AI_VALUE(std::set<uint32>&, "skip loot list");
    if (skipItems.find(itemQualifier.GetId()) != skipItems.end())
        return false;

    uint32 max = proto->MaxCount;
    if (max > 0 && ai->GetBot()->HasItemCount(itemQualifier.GetId(), max, true))
        return false;

    if (proto->StartQuest)
    {
        if (sPlayerbotAIConfig.syncQuestWithPlayer)
            return false; //Quest is autocomplete for the bot so no item needed.
        else
            return true;
    }

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 entry = GetQuestSlotIdCompat(ai->GetBot(), slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(entry);
        if (!quest)
            continue;

        for (int i = 0; i < 4; i++)
        {
            uint32 questItemId = 0;
            uint32 questItemCount = 0;
            if (!QuestRequiredItemCompat(quest, i, questItemId, questItemCount))
                continue;

            if (questItemId == itemQualifier.GetId())
            {
                if (ai->GetMaster() && sPlayerbotAIConfig.syncQuestWithPlayer)
                    return false; //Quest is autocomplete for the bot so no item needed.

                if (AI_VALUE2(uint32, "item count", proto->Name1) >= questItemCount)
                    return false;
            }
        }
    }

    //if (proto->Bonding == BIND_QUEST_ITEM ||  //Still testing if it works ok without these lines.
    //    proto->Bonding == BIND_QUEST_ITEM1 || //Eventually this has to be removed.
    //    proto->Class == ITEM_CLASS_QUEST)
    //{

    bool canLoot = LootStrategyValue::CanLoot(itemQualifier, ai);

    //if (canLoot && proto->Bonding == BIND_WHEN_PICKED_UP && ai->HasActivePlayerMaster())
    //    canLoot = sPlayerbotAIConfig.IsInRandomAccountList(sObjectMgr.GetPlayerAccountIdByGUID(ai->GetBot()->getObjectGuid()));

    return canLoot;
}

bool ReleaseLootAction::Execute(Event& event)
{
    std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid> >("nearest game objects no los")->Get();
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
    {
        LootReleaseCompat(bot, *i);
    }

    std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid> >("nearest corpses")->Get();
    for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
    {
        LootReleaseCompat(bot, *i);
    }

    return true;
}

// ---------- bot-only green/blue drop boost ----------
//
// The server's Rate.Drop.Item.Uncommon/Rare apply to every player equally, so a bot-only
// boost cannot come from mangosd.conf: the module rolls the creature's own loot template
// one extra time per kill and keeps only quality-2/3 entries. It runs in the xpgain
// handler, which fires exactly once per kill and before the corpse can be opened, so the
// bonus items are part of the loot window the bot later stores.
//
// The extra roll's chance is (multiplier - 1) x the entry's DB chance, gated by the
// chance of every reference row on the path. The base roll already happened at death, so
// the expected number of greens/blues per kill is approximately multiplier x normal.
// Groups are walked entry by entry (a group's own pick consumes the base roll only) and
// no quality-2/3 row of a low-level creature uses equal-chanced (chance = 0) groups, so
// those are skipped rather than guessed at.
namespace
{
    constexpr int MAX_LOOT_BONUS_REF_DEPTH = 4;

    // Function templates: they read the 1.12 loot template layout (LootStoreItem fields, the loot groups), which ForeverCore does not
    // have, so what they do with their arguments is only looked at where they are instantiated (T). ApplyBotLootBonus calls them
    // through UnportedBlockCompat: on ForeverCore the boost is the named feature "loot: items, quest items and rolls".
    template <class Access>
    void BoostRareLootTemplate(Access const* access, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth);

    template <class Entry>
    void BoostRareLootEntry(Entry const& entry, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth)
    {
        if (entry.mincountOrRef < 0)                            // reference row: gate the subtree by its own chance
        {
            LootTemplate const* referenced = LootTemplates_Reference.GetLootFor(uint32(-entry.mincountOrRef));
            if (!referenced)
                return;

            float refChance = entry.chance >= 100.0f ? 1.0f : entry.chance / 100.0f;
            BoostRareLootTemplate(reinterpret_cast<LootTemplateAccess const*>(referenced),
                pathChance * refChance, bot, loot, creature, ai, depth + 1);
            return;
        }

        if (entry.mincountOrRef <= 0 || entry.chance <= 0.0f || entry.needs_quest)
            return;

        ItemPrototype const* proto = GetItemProtoByIdCompat(entry.itemid);
        if (!proto || (proto->Quality != ITEM_QUALITY_UNCOMMON && proto->Quality != ITEM_QUALITY_RARE))
            return;

        // Quest rules: a quest-only drop or quest-starting item belongs to the quest system,
        // never to a drop-rate boost. Unique rules: adding something the bot already holds at
        // its max_count would only leave an item it can never pick up.
        if (proto->Class == ITEM_CLASS_QUEST || proto->StartQuest)
            return;

        if (proto->MaxCount > 0 && bot->HasItemCount(entry.itemid, proto->MaxCount, true))
            return;

        float rate = proto->Quality == ITEM_QUALITY_UNCOMMON
            ? sPlayerbotAIConfig.botLootRateUncommon
            : sPlayerbotAIConfig.botLootRateRare;

        if (rate <= 1.0f)
            return;

        // Stack on the server's own quality rate so the multiplier means the same thing
        // whichever Rate.Drop.Item.* mangosd.conf ships.
        float serverRate = DropRateOfQualityCompat(LootTemplates_Creature, proto->Quality);

        float chance = entry.chance * pathChance * serverRate * (rate - 1.0f);
        if (chance > 100.0f)
            chance = 100.0f;

        if (chance <= 0.0f || !RollChanceCompat(chance))
            return;

        loot.AddItem(entry);

        sPlayerbotAIConfig.logEvent(ai, "BotLootBonus", creature->GetObjectGuid(),
            std::to_string(proto->Quality) + ":" + std::to_string(proto->ItemId) + ":" + proto->Name1);
    }

    template <class Access>
    void BoostRareLootTemplate(Access const* access, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth)
    {
        if (!access || depth > MAX_LOOT_BONUS_REF_DEPTH)
            return;

        for (auto const& entry : access->Entries)
            BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);

        for (auto const& group : access->Groups)
        {
            for (auto const& entry : group.ExplicitlyChanced)
                BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);

            for (auto const& entry : group.EqualChanced)
                BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);
        }
    }
}

void ai::ApplyBotLootBonus(PlayerbotAI* ai, Player* bot, ObjectGuid victimGuid)
{
    if (sPlayerbotAIConfig.botLootRateUncommon <= 1.0f && sPlayerbotAIConfig.botLootRateRare <= 1.0f)
        return;

    // Masterless pool bots only: a player character or a hired/alt bot keeps the server's
    // rates, so the boost can never leak into a real player's economy.
    if (!sRandomBotFacade.IsRandomBot(bot))
        return;

    Creature* creature = ai->GetCreature(victimGuid);
    if (!creature || creature->IsAlive())
        return;

    // The rest reads the creature's 1.12 loot object and loot templates: T runs it as it was, F counts the feature ("loot: items,
    // quest items and rolls": a random bot's drop boost; a host bot is never a random bot, so it is not reached there anyway).
    UnportedBlockCompat(tbcompat::Feature::LootItems, [&](auto* corpse)
    {
        // Personal loot of this bot's own kill. m_personal is false whenever the core rolled the
        // corpse for a group, which is exactly the "loot shared with a real player" case.
        auto& loot = corpse->loot;
        if (!loot.m_personal || corpse->GetLootRecipient() != bot)
            return;

        uint32 lootId = corpse->GetLootId();
        if (!lootId)
            return;

        auto const* lootTemplate = LootTemplates_Creature.GetLootFor(lootId);
        if (!lootTemplate)
            return;

        BoostRareLootTemplate(reinterpret_cast<LootTemplateAccess const*>(lootTemplate),
            1.0f, bot, loot, corpse, ai, 0);
    }, creature);
}
