#include "host/TbHostProfession.h"

#include "host/TbHostTown.h"
#include "playerbot/policy/profession/ProfessionPickPolicy.h"
#include "playerbot/policy/profession/GatherNodePolicy.h"
#include "playerbot/policy/profession/CraftForSelfPolicy.h"

#include "CellImpl.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectPackets.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "Trainer.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace tbhost
{
namespace prof
{

namespace
{

constexpr uint32_t kSpellHerbGathering = 2366;
constexpr uint32_t kSpellMining = 2575;
constexpr uint32_t kSpellSkinning = 8613;

constexpr uint32_t kItemMiningPick = 2901;
constexpr uint32_t kItemSkinningKnife = 7005;

bool HasRequiredGatherTool(Player const* bot, uint32_t skillId)
{
    if (!bot)
        return false;
    if (skillId == TortoiseBots::prof::SkillMining)
        return bot->HasItemCount(kItemMiningPick, 1, false);
    if (skillId == TortoiseBots::prof::SkillSkinning)
        return bot->HasItemCount(kItemSkinningKnife, 1, false);
    return true;
}

bool HasDangerNearby(Player const* bot, float radiusYd = 18.0f)
{
    if (!bot || !bot->FindMap())
        return false;
    std::list<Unit*> targets;
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, radiusYd);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, targets, check);
    Cell::VisitAllObjects(bot, searcher, radiusYd);
    for (Unit const* u : targets)
    {
        if (u && u->IsAlive() && u->IsInCombat())
            return true;
    }
    return false;
}

bool HasFreeEquipBagSlot(Player const* bot)
{
    if (!bot)
        return false;
    for (uint8_t slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            return true;
    }
    return false;
}

Creature* FindNearestTradeskillTrainer(Player* bot, float rangeYd = 10.0f)
{
    if (!bot || !bot->FindMap())
        return nullptr;
    std::list<Creature*> creatures;
    Trinity::AllCreaturesOfEntryInRange check(bot, 0, rangeYd);
    Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, rangeYd);

    Creature* best = nullptr;
    float bestDist = rangeYd + 1.0f;
    for (Creature* c : creatures)
    {
        if (!c || !c->IsAlive() || !tbhost::town::IsTrainer(c))
            continue;
        if (tbhost::town::GetTrainerKind(c) != tbhost::town::TrainerKind::Tradeskill)
            continue;
        float const d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

bool SpellTeachesSkill(SpellInfo const* si, uint32_t skillId)
{
    if (!si || !skillId)
        return false;
    for (SpellEffectInfo const& eff : si->GetEffects())
    {
        if ((eff.Effect == SPELL_EFFECT_SKILL || eff.Effect == SPELL_EFFECT_SKILL_STEP)
            && uint32_t(eff.MiscValue) == skillId)
            return true;
    }
    return false;
}

void LogW7(char const* action, Player const* bot, uint32_t skillOrSpell, uint32_t beforeVal, uint32_t afterVal, std::string const& extra = {})
{
    if (!bot)
        return;
    if (extra.empty())
    {
        TC_LOG_INFO("tortoisebots", "[W7] {} bot={} id={} before={} after={}",
            action, bot->GetName(), skillOrSpell, beforeVal, afterVal);
    }
    else
    {
        TC_LOG_INFO("tortoisebots", "[W7] {} bot={} id={} before={} after={} {}",
            action, bot->GetName(), skillOrSpell, beforeVal, afterVal, extra);
    }
}

void LogSkillUpIfChanged(Player const* bot, uint32_t skillId, uint16_t beforeVal)
{
    if (!bot || !skillId)
        return;
    uint16_t const afterVal = bot->GetSkillValue(skillId);
    if (afterVal > beforeVal)
    {
        TC_LOG_INFO("tortoisebots", "[W7] skillup bot={} skill={} before={} after={}",
            bot->GetName(), skillId, beforeVal, afterVal);
    }
}

TortoiseBots::prof::GatherNodeFacts BuildGoNodeFacts(Player const* bot, GameObject const* go, float maxDistYd)
{
    TortoiseBots::prof::GatherNodeFacts f;
    if (!bot || !go || !go->GetGOInfo())
        return f;
    f.exists = true;
    f.spawned = go->isSpawned();
    f.ready = (go->getLootState() == GO_READY) && !go->HasFlag(GO_FLAG_IN_USE);
    f.sameZone = (bot->GetMapId() == go->GetMapId()) && (bot->GetZoneId() == go->GetZoneId());
    f.distanceYd = bot->GetDistance(go);
    f.maxDistanceYd = maxDistYd;
    f.freeBagSlots = bot->GetFreeInventorySlotCount();
    f.dangerNearby = HasDangerNearby(bot);

    LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (lock)
    {
        for (uint32 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] == LOCK_KEY_SKILL)
            {
                uint32 const skillId = uint32(SkillByLockType(LockType(lock->Index[i])));
                if (skillId != 0)
                {
                    f.lockSkillId = skillId;
                    f.reqSkill = std::max<uint32>(1, lock->Skill[i]);
                    f.hasSkill = bot->HasSkill(skillId);
                    f.botSkill = bot->GetSkillValue(skillId);
                    f.hasRequiredTool = HasRequiredGatherTool(bot, skillId);
                    break;
                }
            }
        }
    }
    return f;
}

} // namespace

std::vector<SkillView> GetSkills(Player const* bot)
{
    std::vector<SkillView> out;
    if (!bot)
        return out;
    static uint32_t const kTrackedSkills[] = {
        TortoiseBots::prof::SkillMining,
        TortoiseBots::prof::SkillHerbalism,
        TortoiseBots::prof::SkillSkinning,
        TortoiseBots::prof::SkillBlacksmithing,
        TortoiseBots::prof::SkillLeatherworking,
        TortoiseBots::prof::SkillAlchemy,
        TortoiseBots::prof::SkillTailoring,
        TortoiseBots::prof::SkillEngineering,
        TortoiseBots::prof::SkillEnchanting,
        TortoiseBots::prof::SkillFirstAid,
        TortoiseBots::prof::SkillCooking
    };
    for (uint32_t id : kTrackedSkills)
    {
        if (!bot->HasSkill(id))
            continue;
        SkillView sv;
        sv.skillId = id;
        sv.value = bot->GetSkillValue(id);
        sv.maxValue = bot->GetMaxSkillValue(id);
        sv.step = bot->GetSkillStep(id);
        sv.name = TortoiseBots::prof::SkillNameFromId(id);
        out.push_back(std::move(sv));
    }
    return out;
}

std::string FormatStateProf(Player const* bot)
{
    std::vector<SkillView> const skills = GetSkills(bot);
    std::string out;
    for (size_t i = 0; i < skills.size(); ++i)
    {
        if (i > 0)
            out.push_back(',');
        out += skills[i].name + ":" + std::to_string(skills[i].value) + "/" + std::to_string(skills[i].maxValue);
    }
    return out;
}

bool LearnProfession(Player* bot, uint32_t skillId, std::string& code, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
    {
        code = "not-in-world";
        detail = "bot is not in world";
        return false;
    }
    if (!TortoiseBots::prof::IsSupportedProfession(skillId))
    {
        code = "unsupported-skill";
        detail = "unsupported profession skill";
        return false;
    }

    Creature* trainer = FindNearestTradeskillTrainer(bot, INTERACTION_DISTANCE);
    if (!trainer || !tbhost::town::NpcIfCanInteract(bot, trainer->GetGUID(), UNIT_NPC_FLAG_TRAINER))
    {
        code = "no-trainer";
        detail = "no tradeskill trainer within interaction range";
        LogW7("learn-skip", bot, skillId, bot->GetSkillValue(skillId), bot->GetSkillValue(skillId), "reason=no-trainer");
        return false;
    }

    uint16_t const beforeVal = bot->GetSkillValue(skillId);
    uint16_t const beforeMax = bot->GetMaxSkillValue(skillId);
    std::vector<tbhost::town::TrainerSpellView> const spells = tbhost::town::GetTrainerSpells(trainer);

    uint32_t chosenSpell = 0;
    std::string lastRefusal = "no-matching-spell";
    float const discountMod = bot->GetReputationPriceDiscount(trainer);
    for (tbhost::town::TrainerSpellView const& sp : spells)
    {
        SpellInfo const* si = sSpellMgr->GetSpellInfo(sp.spellId, DIFFICULTY_NONE);
        if (!si)
            continue;
        bool const teaches = SpellTeachesSkill(si, skillId) || (sp.reqSkillLine == skillId && !bot->HasSkill(skillId));
        if (!teaches)
            continue;
        std::string refusal;
        if (tbhost::town::CanTeachTrainerSpell(bot, trainer, sp, discountMod, &refusal))
        {
            chosenSpell = sp.spellId;
            break;
        }
        lastRefusal = refusal;
    }

    if (!chosenSpell)
    {
        code = lastRefusal;
        detail = "trainer cannot teach skill " + std::to_string(skillId) + " (" + lastRefusal + ")";
        LogW7("learn-skip", bot, skillId, beforeVal, beforeVal, "reason=" + lastRefusal);
        return false;
    }

    std::string teachRefusal;
    if (!tbhost::town::TeachTrainerSpell(bot, trainer, chosenSpell, &teachRefusal))
    {
        code = teachRefusal.empty() ? "teach-failed" : teachRefusal;
        detail = "trainer teach failed: " + code;
        LogW7("learn-skip", bot, skillId, beforeVal, beforeVal, "reason=" + code);
        return false;
    }

    uint16_t const afterVal = bot->GetSkillValue(skillId);
    uint16_t const afterMax = bot->GetMaxSkillValue(skillId);
    LogW7("learn", bot, skillId, beforeVal, afterVal,
        "spell=" + std::to_string(chosenSpell) + " max_before=" + std::to_string(beforeMax) + " max_after=" + std::to_string(afterMax));
    code = "ok";
    detail = "learned " + std::string(TortoiseBots::prof::SkillNameFromId(skillId));
    return true;
}

bool OpenNode(Player* bot, ObjectGuid const& guid, float maxDistYd, std::string& code, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
    {
        code = "not-in-world";
        detail = "bot is not in world";
        return false;
    }
    WorldSession* session = bot->GetSession();
    if (!session)
    {
        code = "no-session";
        detail = "bot has no session";
        return false;
    }

    Map* map = bot->FindMap();
    GameObject* go = guid.IsGameObject() ? map->GetGameObject(guid) : nullptr;
    if (!go)
    {
        code = "not-found";
        detail = "node not found";
        return false;
    }

    TortoiseBots::prof::GatherNodeFacts const facts = BuildGoNodeFacts(bot, go, maxDistYd);
    TortoiseBots::prof::GatherDecision const decision = TortoiseBots::prof::EvaluateGatherNode(facts);
    if (!decision.allow)
    {
        code = decision.code;
        detail = std::string("skipped gather (") + decision.code + ")";
        TC_LOG_INFO("tortoisebots", "[W7] gather-skip bot={} node={} skill={} req={} have={} reason={}",
            bot->GetName(), go->GetEntry(), facts.lockSkillId, facts.reqSkill, facts.botSkill, decision.code);
        return false;
    }

    if (!go->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
    {
        bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        code = "walking";
        detail = "moving toward node " + std::to_string(go->GetEntry());
        return true;
    }

    if (bot->IsMounted())
        bot->Dismount();

    uint16_t const beforeSkill = facts.lockSkillId ? bot->GetSkillValue(facts.lockSkillId) : 0;

    if (facts.lockSkillId == TortoiseBots::prof::SkillMining || facts.lockSkillId == TortoiseBots::prof::SkillHerbalism)
    {
        uint32_t const spellId = facts.lockSkillId == TortoiseBots::prof::SkillMining ? kSpellMining : kSpellHerbGathering;
        WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
        packet.Cast.SpellID = int32(spellId);
        packet.Cast.Target.Flags = TARGET_FLAG_GAMEOBJECT;
        packet.Cast.Target.Unit = go->GetGUID();
        session->HandleCastSpellOpcode(packet);
    }
    else
    {
        // Reuse slice 9 chest/object path for unlocked chests.
        WorldPackets::GameObject::GameObjUse packet{ WorldPacket(CMSG_GAME_OBJ_USE) };
        packet.Guid = go->GetGUID();
        session->HandleGameObjectUseOpcode(packet);
    }

    uint16_t const afterSkill = facts.lockSkillId ? bot->GetSkillValue(facts.lockSkillId) : 0;
    TC_LOG_INFO("tortoisebots", "[W7] gather bot={} node={} skill={} before={} after={}",
        bot->GetName(), go->GetEntry(), facts.lockSkillId, beforeSkill, afterSkill);
    LogSkillUpIfChanged(bot, facts.lockSkillId, beforeSkill);
    LootOpenNode(bot, go->GetGUID());
    code = "ok";
    detail = "gathered node " + std::to_string(go->GetEntry());
    return true;
}

bool LootOpenNode(Player* bot, ObjectGuid const& guid)
{
    if (!bot || !bot->GetSession())
        return false;
    Loot* loot = bot->GetLootByWorldObjectGUID(guid);
    if (!loot)
        return false;
    uint32_t const beforeFree = bot->GetFreeInventorySlotCount();
    uint32_t const totalSlots = uint32_t(loot->items.size());
    for (uint32_t i = 0; i < totalSlots; ++i)
    {
        WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
        packet.Loot.push_back({ guid, uint8(i) });
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
    }
    uint32_t const afterFree = bot->GetFreeInventorySlotCount();
    return afterFree <= beforeFree;
}

bool GatherNearestInSight(Player* bot, float maxDistYd, std::string& code, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
    {
        code = "not-in-world";
        detail = "bot is not in world";
        return false;
    }

    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), maxDistYd, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    GameObject* bestGo = nullptr;
    float bestDist = maxDistYd + 1.0f;
    std::string nearestSkipCode = "not-found";
    float nearestAnyDist = maxDistYd + 1.0f;

    for (GameObject* go : objects)
    {
        if (!go || !go->GetGOInfo() || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            continue;
        TortoiseBots::prof::GatherNodeFacts const facts = BuildGoNodeFacts(bot, go, maxDistYd);
        if (facts.lockSkillId != TortoiseBots::prof::SkillMining && facts.lockSkillId != TortoiseBots::prof::SkillHerbalism)
            continue;
        TortoiseBots::prof::GatherDecision const dec = TortoiseBots::prof::EvaluateGatherNode(facts);
        float const d = bot->GetDistance(go);
        if (dec.allow)
        {
            if (d < bestDist)
            {
                bestDist = d;
                bestGo = go;
            }
        }
        else if (d < nearestAnyDist)
        {
            nearestAnyDist = d;
            nearestSkipCode = dec.code;
        }
    }

    if (!bestGo)
    {
        code = nearestSkipCode;
        detail = "no gatherable node in sight (" + nearestSkipCode + ")";
        return false;
    }

    return OpenNode(bot, bestGo->GetGUID(), maxDistYd, code, detail);
}

bool Craft(Player* bot, uint32_t spellId, uint32_t count, std::string& code, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !bot->GetSession())
    {
        code = "not-in-world";
        detail = "bot is not in world";
        return false;
    }

    SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!si)
    {
        code = "unknown-recipe";
        detail = "unknown recipe spell";
        return false;
    }

    uint32_t skillId = 0;
    uint32_t reqSkill = 0;
    static uint32_t const kCraftSkills[] = {
        TortoiseBots::prof::SkillBlacksmithing,
        TortoiseBots::prof::SkillLeatherworking,
        TortoiseBots::prof::SkillAlchemy,
        TortoiseBots::prof::SkillTailoring,
        TortoiseBots::prof::SkillEngineering,
        TortoiseBots::prof::SkillEnchanting,
        TortoiseBots::prof::SkillFirstAid,
        TortoiseBots::prof::SkillCooking
    };
    for (uint32_t sid : kCraftSkills)
    {
        if (auto const* list = sDB2Manager.GetSkillLineAbilitiesBySkill(sid))
        {
            for (SkillLineAbilityEntry const* ab : *list)
            {
                if (ab && uint32_t(ab->Spell) == spellId)
                {
                    skillId = sid;
                    reqSkill = ab->MinSkillLineRank > 0 ? uint32_t(ab->MinSkillLineRank) : 0;
                    break;
                }
            }
        }
        if (skillId)
            break;
    }

    TortoiseBots::prof::CraftFacts facts;
    facts.spellId = spellId;
    facts.known = bot->HasSpell(spellId);
    facts.skillId = skillId;
    facts.reqSkill = reqSkill;
    facts.botSkill = skillId ? bot->GetSkillValue(skillId) : 0;
    facts.freeBagSlots = bot->GetFreeInventorySlotCount();
    facts.purpose = TortoiseBots::prof::CraftPurpose::Explicit;

    for (uint32_t i = 0; i < MAX_SPELL_TOTEMS; ++i)
    {
        if (si->Totem[i] > 0 && !bot->HasItemCount(uint32_t(si->Totem[i]), 1, false))
            facts.hasRequiredTool = false;
        if (si->TotemCategory[i] > 0 && !bot->HasItemTotemCategory(si->TotemCategory[i]))
            facts.hasRequiredTool = false;
    }

    uint32_t const runs = std::max<uint32_t>(1, count);
    for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
        {
            TortoiseBots::prof::ReagentCheck rc;
            rc.itemId = uint32_t(si->Reagent[i]);
            rc.need = uint32_t(si->ReagentCount[i]);
            rc.have = bot->GetItemCount(rc.itemId, false);
            facts.reagents.push_back(rc);
        }
    }

    for (SpellEffectInfo const& eff : si->GetEffects())
    {
        if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && eff.ItemType > 0)
        {
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(eff.ItemType))
            {
                if (proto->GetClass() == ITEM_CLASS_CONTAINER)
                {
                    facts.createsBag = true;
                    facts.hasFreeEquipBagSlot = HasFreeEquipBagSlot(bot);
                }
            }
        }
    }

    TortoiseBots::prof::CraftDecision const decision = TortoiseBots::prof::EvaluateCraftForSelf(facts);
    if (!decision.allow)
    {
        code = decision.code;
        detail = std::string("craft skipped (") + decision.code + ")";
        LogW7("craft-skip", bot, spellId, facts.botSkill, facts.botSkill, std::string("reason=") + decision.code);
        return false;
    }

    if (bot->IsMounted())
        bot->Dismount();

    uint16_t const beforeSkill = skillId ? bot->GetSkillValue(skillId) : 0;
    for (uint32_t i = 0; i < runs; ++i)
    {
        WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
        packet.Cast.SpellID = int32(spellId);
        packet.Cast.Target.Flags = TARGET_FLAG_NONE;
        packet.Cast.Target.Unit = bot->GetGUID();
        bot->GetSession()->HandleCastSpellOpcode(packet);
    }

    uint16_t const afterSkill = skillId ? bot->GetSkillValue(skillId) : 0;
    LogW7("craft", bot, spellId, beforeSkill, afterSkill, "count=" + std::to_string(runs));
    LogSkillUpIfChanged(bot, skillId, beforeSkill);
    code = "ok";
    detail = "crafted spell " + std::to_string(spellId);
    return true;
}

void TickFreeGather(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->IsInCombat() || bot->isMoving() || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        return;
    std::string code, detail;
    GatherNearestInSight(bot, 150.0f, code, detail);
}

namespace
{
struct CraftReagentCache
{
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> bySkill;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> byCreatedItem;
};

CraftReagentCache const& GetCraftReagentCache()
{
    static CraftReagentCache const cache = []()
    {
        CraftReagentCache c;
        static uint32_t const kCraftSkills[] = {
            TortoiseBots::prof::SkillBlacksmithing,
            TortoiseBots::prof::SkillLeatherworking,
            TortoiseBots::prof::SkillAlchemy,
            TortoiseBots::prof::SkillTailoring,
            TortoiseBots::prof::SkillEngineering,
            TortoiseBots::prof::SkillEnchanting,
            TortoiseBots::prof::SkillFirstAid,
            TortoiseBots::prof::SkillCooking
        };
        for (uint32_t sid : kCraftSkills)
        {
            auto const* list = sDB2Manager.GetSkillLineAbilitiesBySkill(sid);
            if (!list)
                continue;
            auto& skillSet = c.bySkill[sid];
            for (SkillLineAbilityEntry const* ab : *list)
            {
                if (!ab)
                    continue;
                SpellInfo const* si = sSpellMgr->GetSpellInfo(uint32_t(ab->Spell), DIFFICULTY_NONE);
                if (!si)
                    continue;
                std::vector<uint32_t> createdItems;
                for (SpellEffectInfo const& eff : si->GetEffects())
                {
                    if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && eff.ItemType > 0)
                        createdItems.push_back(uint32_t(eff.ItemType));
                }
                for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
                    {
                        uint32_t const rId = uint32_t(si->Reagent[i]);
                        skillSet.insert(rId);
                        for (uint32_t itemOut : createdItems)
                            c.byCreatedItem[itemOut].insert(rId);
                    }
                }
            }
        }
        return c;
    }();
    return cache;
}
} // namespace

bool HasGatherableNodeInSight(Player* bot, float maxDistYd)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return false;

    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), maxDistYd, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    for (GameObject* go : objects)
    {
        if (!go || !go->GetGOInfo() || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            continue;
        TortoiseBots::prof::GatherNodeFacts const facts = BuildGoNodeFacts(bot, go, maxDistYd);
        if (facts.lockSkillId != TortoiseBots::prof::SkillMining && facts.lockSkillId != TortoiseBots::prof::SkillHerbalism)
            continue;
        if (TortoiseBots::prof::EvaluateGatherNode(facts).allow)
            return true;
    }
    return false;
}

uint32_t FindCraftSpellForItem(Player* bot, uint32_t itemId)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !itemId)
        return 0;

    static uint32_t const kCraftSkills[] = {
        TortoiseBots::prof::SkillBlacksmithing,
        TortoiseBots::prof::SkillLeatherworking,
        TortoiseBots::prof::SkillAlchemy,
        TortoiseBots::prof::SkillTailoring,
        TortoiseBots::prof::SkillEngineering,
        TortoiseBots::prof::SkillEnchanting,
        TortoiseBots::prof::SkillFirstAid,
        TortoiseBots::prof::SkillCooking
    };

    for (uint32_t sid : kCraftSkills)
    {
        if (!bot->HasSkill(sid))
            continue;
        auto const* list = sDB2Manager.GetSkillLineAbilitiesBySkill(sid);
        if (!list)
            continue;
        for (SkillLineAbilityEntry const* ab : *list)
        {
            if (!ab)
                continue;
            uint32_t const spellId = uint32_t(ab->Spell);
            if (!bot->HasSpell(spellId))
                continue;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!si)
                continue;
            bool createsTarget = false;
            bool createsBag = false;
            for (SpellEffectInfo const& eff : si->GetEffects())
            {
                if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && uint32_t(eff.ItemType) == itemId)
                {
                    createsTarget = true;
                    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(eff.ItemType))
                        createsBag = (proto->GetClass() == ITEM_CLASS_CONTAINER);
                }
            }
            if (!createsTarget)
                continue;

            TortoiseBots::prof::CraftFacts facts;
            facts.spellId = spellId;
            facts.known = true;
            facts.skillId = sid;
            facts.reqSkill = ab->MinSkillLineRank > 0 ? uint32_t(ab->MinSkillLineRank) : 0;
            facts.botSkill = bot->GetSkillValue(sid);
            facts.freeBagSlots = bot->GetFreeInventorySlotCount();
            facts.createsBag = createsBag;
            facts.hasFreeEquipBagSlot = createsBag ? HasFreeEquipBagSlot(bot) : false;
            facts.purpose = TortoiseBots::prof::CraftPurpose::Explicit;
            for (uint32_t i = 0; i < MAX_SPELL_TOTEMS; ++i)
            {
                if (si->Totem[i] > 0 && !bot->HasItemCount(uint32_t(si->Totem[i]), 1, false))
                    facts.hasRequiredTool = false;
                if (si->TotemCategory[i] > 0 && !bot->HasItemTotemCategory(si->TotemCategory[i]))
                    facts.hasRequiredTool = false;
            }
            for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                if (si->Reagent[i] > 0 && si->ReagentCount[i] > 0)
                {
                    TortoiseBots::prof::ReagentCheck rc;
                    rc.itemId = uint32_t(si->Reagent[i]);
                    rc.need = uint32_t(si->ReagentCount[i]);
                    rc.have = bot->GetItemCount(rc.itemId, false);
                    facts.reagents.push_back(rc);
                }
            }
            if (TortoiseBots::prof::EvaluateCraftForSelf(facts).allow)
                return spellId;
        }
    }
    return 0;
}

bool IsReagentForBotProfessions(Player const* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return false;
    CraftReagentCache const& cache = GetCraftReagentCache();
    auto const checkSkill = [&](uint32_t sid) -> bool
    {
        if (!sid)
            return false;
        auto it = cache.bySkill.find(sid);
        return it != cache.bySkill.end() && it->second.count(itemId) > 0;
    };

    auto const pair = TortoiseBots::prof::ProfessionPairForClass(bot->GetClass());
    if (checkSkill(pair.first) || checkSkill(pair.second))
        return true;

    for (auto const& sv : GetSkills(bot))
    {
        if (checkSkill(sv.skillId))
            return true;
    }
    return false;
}

bool IsReagentForItem(uint32_t craftedItemId, uint32_t reagentItemId)
{
    if (!craftedItemId || !reagentItemId)
        return false;
    CraftReagentCache const& cache = GetCraftReagentCache();
    auto it = cache.byCreatedItem.find(craftedItemId);
    return it != cache.byCreatedItem.end() && it->second.count(reagentItemId) > 0;
}

} // namespace prof
} // namespace tbhost
