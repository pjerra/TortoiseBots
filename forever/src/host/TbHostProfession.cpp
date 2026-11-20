#include "host/TbHostProfession.h"

#include "host/TbHostTown.h"
#include "host/run/TbRunTasks.h"
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

TortoiseBots::prof::GatherNodeFacts BuildGoNodeFacts(Player const* bot, GameObject const* go, float maxDistYd, bool checkDanger = false)
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
    f.dangerNearby = checkDanger ? HasDangerNearby(bot) : false;

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

void CastGatherOnNode(Player* bot, GameObject* go, uint32_t lockSkillId)
{
    if (!bot || !go || !bot->GetSession())
        return;
    if (bot->IsMounted())
        bot->Dismount();

    WorldSession* session = bot->GetSession();
    if (lockSkillId == TortoiseBots::prof::SkillMining || lockSkillId == TortoiseBots::prof::SkillHerbalism)
    {
        uint32_t const spellId = lockSkillId == TortoiseBots::prof::SkillMining ? kSpellMining : kSpellHerbGathering;
        WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
        packet.Cast.SpellID = int32(spellId);
        packet.Cast.Target.Flags = TARGET_FLAG_GAMEOBJECT;
        packet.Cast.Target.Unit = go->GetGUID();
        session->HandleCastSpellOpcode(packet);
    }
    else
    {
        TortoiseBots::run::tasks::SendGameObjectUse(bot, go);
    }
}

bool LootOpenNodeWithStats(Player* bot, ObjectGuid const& guid, uint32_t& outItemId, std::string& outItemName, uint32_t& outGained)
{
    outItemId = 0;
    outItemName.clear();
    outGained = 0;
    if (!bot || !bot->GetSession())
        return false;

    Loot* loot = bot->GetLootByWorldObjectGUID(guid);
    if (!loot)
        return false;

    std::unordered_map<uint32_t, uint32_t> beforeCounts;
    for (LootItem const& li : loot->items)
    {
        if (li.itemid && beforeCounts.find(li.itemid) == beforeCounts.end())
            beforeCounts.emplace(li.itemid, bot->GetItemCount(li.itemid, false));
    }

    uint32_t const totalSlots = uint32_t(loot->items.size());
    for (uint32_t i = 0; i < totalSlots; ++i)
    {
        WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
        packet.Loot.push_back({ guid, uint8(i) });
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
    }

    for (auto const& kv : beforeCounts)
    {
        uint32_t const after = bot->GetItemCount(kv.first, false);
        if (after > kv.second)
        {
            uint32_t const delta = after - kv.second;
            outGained += delta;
            if (outItemId == 0)
            {
                outItemId = kv.first;
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(kv.first))
                    outItemName = proto->GetName(LOCALE_enUS);
                if (outItemName.empty())
                    outItemName = std::to_string(kv.first);
            }
        }
    }

    if (Loot* afterLoot = bot->GetLootByWorldObjectGUID(guid))
    {
        if (afterLoot->isLooted() || outGained > 0)
            bot->GetSession()->DoLootRelease(afterLoot);
    }

    return outGained > 0;
}

struct GatherErrandState
{
    bool active = false;
    uint32_t orderSeq = 0;
    ObjectGuid nodeGuid;
    uint32_t nodeEntry = 0;
    uint32_t lockSkillId = 0;
    uint16_t beforeSkill = 0;
    TortoiseBots::prof::GatherPhase phase = TortoiseBots::prof::GatherPhase::Idle;
    uint32_t phaseElapsedMs = 0;
    bool lootAttempted = false;
    uint32_t itemsGained = 0;
    uint32_t gainedItemId = 0;
    std::string gainedItemName;
};

struct CraftErrandState
{
    bool active = false;
    uint32_t orderSeq = 0;
    uint32_t spellId = 0;
    uint32_t skillId = 0;
    uint16_t beforeSkill = 0;
    uint32_t craftedItemId = 0;
    std::string craftedItemName;
    uint32_t lastItemCount = 0;
    uint32_t totalGained = 0;
    uint32_t completedRuns = 0;
    uint32_t targetRuns = 1;
    TortoiseBots::prof::CraftPhase phase = TortoiseBots::prof::CraftPhase::Idle;
    uint32_t phaseElapsedMs = 0;
};

struct ErrandOutcome
{
    uint32_t orderSeq = 0;
    std::string result;
    bool finished = false;
};

struct BotProfessionErrands
{
    GatherErrandState gather;
    CraftErrandState craft;
    ErrandOutcome outcome;
};

std::unordered_map<ObjectGuid, BotProfessionErrands>& Errands()
{
    static std::unordered_map<ObjectGuid, BotProfessionErrands> map;
    return map;
}

GameObject* FindBestGatherNodeInSight(Player* bot, float maxDistYd, TortoiseBots::prof::GatherNodeFacts& outFacts, std::string& outSkipCode)
{
    outSkipCode = "not-found";
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;

    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), maxDistYd, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    GameObject* bestGo = nullptr;
    float bestDist = maxDistYd + 1.0f;
    float nearestAnyDist = maxDistYd + 1.0f;

    for (GameObject* go : objects)
    {
        if (!go || !go->GetGOInfo() || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
            continue;
        TortoiseBots::prof::GatherNodeFacts const facts = BuildGoNodeFacts(bot, go, maxDistYd, false);
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
                outFacts = facts;
            }
        }
        else if (d < nearestAnyDist)
        {
            nearestAnyDist = d;
            outSkipCode = dec.code;
        }
    }
    if (bestGo && HasDangerNearby(bot))
    {
        outFacts.dangerNearby = true;
        outSkipCode = "danger";
        return nullptr;
    }
    return bestGo;
}

bool ValidateAndPrepareCraft(Player* bot, uint32_t spellId, uint32_t& outSkillId, uint32_t& outCraftedItemId, std::string& outCraftedName, std::string& outCode, std::string& outDetail)
{
    outSkillId = 0;
    outCraftedItemId = 0;
    outCraftedName.clear();

    SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!si)
    {
        outCode = "unknown-recipe";
        outDetail = "unknown recipe spell";
        return false;
    }

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
                    outSkillId = sid;
                    reqSkill = ab->MinSkillLineRank > 0 ? uint32_t(ab->MinSkillLineRank) : 0;
                    break;
                }
            }
        }
        if (outSkillId)
            break;
    }

    TortoiseBots::prof::CraftFacts facts;
    facts.spellId = spellId;
    facts.known = bot->HasSpell(spellId);
    facts.skillId = outSkillId;
    facts.reqSkill = reqSkill;
    facts.botSkill = outSkillId ? bot->GetSkillValue(outSkillId) : 0;
    facts.freeBagSlots = bot->GetFreeInventorySlotCount();
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

    for (SpellEffectInfo const& eff : si->GetEffects())
    {
        if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && eff.ItemType > 0)
        {
            if (outCraftedItemId == 0)
                outCraftedItemId = uint32_t(eff.ItemType);
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(eff.ItemType))
            {
                if (outCraftedName.empty())
                    outCraftedName = proto->GetName(LOCALE_enUS);
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
        outCode = decision.code;
        outDetail = std::string("craft skipped (") + decision.code + ")";
        LogW7("craft-skip", bot, spellId, facts.botSkill, facts.botSkill, std::string("reason=") + decision.code);
        return false;
    }

    return true;
}

void CastCraftSpell(Player* bot, uint32_t spellId)
{
    if (!bot || !bot->GetSession())
        return;
    if (bot->IsMounted())
        bot->Dismount();
    WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
    packet.Cast.SpellID = int32(spellId);
    packet.Cast.Target.Flags = TARGET_FLAG_NONE;
    packet.Cast.Target.Unit = bot->GetGUID();
    bot->GetSession()->HandleCastSpellOpcode(packet);
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

    TortoiseBots::prof::GatherNodeFacts facts = BuildGoNodeFacts(bot, go, maxDistYd, false);
    TortoiseBots::prof::GatherDecision decision = TortoiseBots::prof::EvaluateGatherNode(facts);
    if (decision.allow && HasDangerNearby(bot))
    {
        facts.dangerNearby = true;
        decision = TortoiseBots::prof::EvaluateGatherNode(facts);
    }
    if (!decision.allow)
    {
        code = decision.code;
        detail = std::string("skipped gather (") + decision.code + ")";
        TC_LOG_INFO("tortoisebots", "[W7] gather-skip bot={} node={} skill={} req={} have={} reason={}",
            bot->GetName(), go->GetEntry(), facts.lockSkillId, facts.reqSkill, facts.botSkill, decision.code);
        return false;
    }

    BotProfessionErrands& entry = Errands()[bot->GetGUID()];
    entry.craft = CraftErrandState{};
    GatherErrandState& g = entry.gather;
    g = GatherErrandState{};
    g.active = true;
    g.orderSeq = 0;
    g.nodeGuid = go->GetGUID();
    g.nodeEntry = go->GetEntry();
    g.lockSkillId = facts.lockSkillId;
    g.beforeSkill = facts.lockSkillId ? bot->GetSkillValue(facts.lockSkillId) : 0;

    if (!go->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
    {
        g.phase = TortoiseBots::prof::GatherPhase::Walking;
        bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        code = "pending:walking";
        detail = "moving toward node " + std::to_string(go->GetEntry());
        return true;
    }

    g.phase = TortoiseBots::prof::GatherPhase::Casting;
    CastGatherOnNode(bot, go, facts.lockSkillId);
    code = "pending:casting";
    detail = "gathering node " + std::to_string(go->GetEntry());
    return true;
}

bool LootOpenNode(Player* bot, ObjectGuid const& guid)
{
    uint32_t itemId = 0;
    std::string itemName;
    uint32_t gained = 0;
    return LootOpenNodeWithStats(bot, guid, itemId, itemName, gained);
}

bool GatherNearestInSight(Player* bot, float maxDistYd, std::string& code, std::string& detail)
{
    return StartGatherNearest(bot, maxDistYd, 0, code, detail);
}

bool StartGatherNearest(Player* bot, float maxDistYd, uint32_t orderSeq, std::string& stateOut, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !bot->GetSession())
    {
        stateOut = "not-in-world";
        detail = "bot is not in world";
        return false;
    }

    TortoiseBots::prof::GatherNodeFacts facts;
    std::string skipCode;
    GameObject* bestGo = FindBestGatherNodeInSight(bot, maxDistYd, facts, skipCode);
    if (!bestGo)
    {
        stateOut = skipCode;
        detail = "no gatherable node in sight (" + skipCode + ")";
        return false;
    }

    BotProfessionErrands& entry = Errands()[bot->GetGUID()];
    entry.craft = CraftErrandState{};
    GatherErrandState& g = entry.gather;
    g = GatherErrandState{};
    g.active = true;
    g.orderSeq = orderSeq;
    g.nodeGuid = bestGo->GetGUID();
    g.nodeEntry = bestGo->GetEntry();
    g.lockSkillId = facts.lockSkillId;
    g.beforeSkill = facts.lockSkillId ? bot->GetSkillValue(facts.lockSkillId) : 0;

    if (!bestGo->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
    {
        g.phase = TortoiseBots::prof::GatherPhase::Walking;
        bot->GetMotionMaster()->MovePoint(0, bestGo->GetPositionX(), bestGo->GetPositionY(), bestGo->GetPositionZ());
        stateOut = "pending:walking";
        detail = "moving toward node " + std::to_string(bestGo->GetEntry());
    }
    else
    {
        g.phase = TortoiseBots::prof::GatherPhase::Casting;
        CastGatherOnNode(bot, bestGo, facts.lockSkillId);
        stateOut = "pending:casting";
        detail = "gathering node " + std::to_string(bestGo->GetEntry());
    }

    if (orderSeq != 0)
        entry.outcome = { orderSeq, stateOut, false };

    return true;
}

bool Craft(Player* bot, uint32_t spellId, uint32_t count, std::string& code, std::string& detail)
{
    return StartCraft(bot, spellId, count, 0, code, detail);
}

bool StartCraft(Player* bot, uint32_t spellId, uint32_t count, uint32_t orderSeq, std::string& stateOut, std::string& detail)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !bot->GetSession())
    {
        stateOut = "not-in-world";
        detail = "bot is not in world";
        return false;
    }

    uint32_t skillId = 0;
    uint32_t craftedItemId = 0;
    std::string craftedItemName;
    if (!ValidateAndPrepareCraft(bot, spellId, skillId, craftedItemId, craftedItemName, stateOut, detail))
        return false;

    uint32_t const runs = std::max<uint32_t>(1, count);
    BotProfessionErrands& entry = Errands()[bot->GetGUID()];
    entry.gather = GatherErrandState{};
    CraftErrandState& c = entry.craft;
    c = CraftErrandState{};
    c.active = true;
    c.orderSeq = orderSeq;
    c.spellId = spellId;
    c.skillId = skillId;
    c.beforeSkill = skillId ? bot->GetSkillValue(skillId) : 0;
    c.craftedItemId = craftedItemId;
    c.craftedItemName = craftedItemName;
    c.lastItemCount = craftedItemId ? bot->GetItemCount(craftedItemId, false) : 0;
    c.targetRuns = runs;
    c.phase = TortoiseBots::prof::CraftPhase::Casting;

    CastCraftSpell(bot, spellId);
    stateOut = "pending:casting";
    detail = "crafting spell " + std::to_string(spellId);
    if (orderSeq != 0)
        entry.outcome = { orderSeq, stateOut, false };
    return true;
}

void TickErrands(Player* bot, uint32_t diffMs)
{
    if (!bot)
        return;

    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end())
        return;

    BotProfessionErrands& entry = it->second;

    if (entry.gather.active)
    {
        GatherErrandState& g = entry.gather;
        g.phaseElapsedMs += diffMs;

        Map* map = bot->FindMap();
        GameObject* go = (map && g.nodeGuid.IsGameObject()) ? map->GetGameObject(g.nodeGuid) : nullptr;

        TortoiseBots::prof::GatherErrandFacts f;
        f.phase = g.phase;
        f.inWorld = bot->IsInWorld() && map != nullptr && bot->GetSession() != nullptr;
        f.alive = bot->IsAlive();
        f.inCombat = bot->IsInCombat();
        f.nodeExists = (go != nullptr && go->isSpawned());
        f.nodeReady = (go != nullptr && go->isSpawned() && go->getLootState() == GO_READY && !go->HasFlag(GO_FLAG_IN_USE));
        f.inInteractRange = (go != nullptr && go->IsWithinDistInMap(bot, INTERACTION_DISTANCE));
        f.isMoving = bot->isMoving();
        f.isCasting = (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr);
        f.lootAvailable = (bot->GetLootByWorldObjectGUID(g.nodeGuid) != nullptr);
        f.lootAttempted = g.lootAttempted;
        f.itemsGained = g.itemsGained;
        f.phaseElapsedMs = g.phaseElapsedMs;

        TortoiseBots::prof::GatherErrandStep const step = TortoiseBots::prof::StepGatherErrand(f);
        if (step.nextPhase != g.phase)
        {
            g.phase = step.nextPhase;
            g.phaseElapsedMs = 0;
        }

        auto finishOk = [&]()
        {
            uint16_t const afterSkill = g.lockSkillId ? bot->GetSkillValue(g.lockSkillId) : 0;
            TC_LOG_INFO("tortoisebots", "[W7] gather bot={} node={} skill={} before={} after={} item={} count={}",
                bot->GetName(), g.nodeEntry, g.lockSkillId, g.beforeSkill, afterSkill,
                g.gainedItemName.empty() ? std::to_string(g.gainedItemId) : g.gainedItemName, g.itemsGained);
            LogSkillUpIfChanged(bot, g.lockSkillId, g.beforeSkill);
            if (g.orderSeq != 0)
                entry.outcome = { g.orderSeq, "ok", true };
            g.active = false;
        };

        auto finishFail = [&](char const* failCode)
        {
            TC_LOG_INFO("tortoisebots", "[W7] gather-fail bot={} node={} reason={}",
                bot->GetName(), g.nodeEntry, failCode ? failCode : "unknown");
            if (g.orderSeq != 0)
                entry.outcome = { g.orderSeq, std::string("fail:") + (failCode ? failCode : "unknown"), true };
            g.active = false;
        };

        switch (step.action)
        {
            case TortoiseBots::prof::GatherErrandAction::MoveToNode:
                if (go)
                    bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
                if (g.orderSeq != 0)
                    entry.outcome = { g.orderSeq, step.stateLabel, false };
                break;

            case TortoiseBots::prof::GatherErrandAction::CastGather:
                if (go)
                    CastGatherOnNode(bot, go, g.lockSkillId);
                if (g.orderSeq != 0)
                    entry.outcome = { g.orderSeq, step.stateLabel, false };
                break;

            case TortoiseBots::prof::GatherErrandAction::Wait:
                if (g.orderSeq != 0)
                    entry.outcome = { g.orderSeq, step.stateLabel, false };
                break;

            case TortoiseBots::prof::GatherErrandAction::LootNode:
            {
                g.lootAttempted = true;
                uint32_t itemId = 0;
                std::string itemName;
                uint32_t gained = 0;
                if (LootOpenNodeWithStats(bot, g.nodeGuid, itemId, itemName, gained) && gained > 0)
                {
                    g.itemsGained += gained;
                    if (g.gainedItemId == 0)
                    {
                        g.gainedItemId = itemId;
                        g.gainedItemName = itemName;
                    }
                    finishOk();
                }
                else
                {
                    finishFail("no-loot");
                }
                break;
            }

            case TortoiseBots::prof::GatherErrandAction::FinishOk:
                finishOk();
                break;

            case TortoiseBots::prof::GatherErrandAction::FinishFail:
                finishFail(step.failCode);
                break;

            case TortoiseBots::prof::GatherErrandAction::None:
            default:
                break;
        }
        return;
    }

    if (entry.craft.active)
    {
        CraftErrandState& c = entry.craft;
        c.phaseElapsedMs += diffMs;

        uint32_t const currentCount = c.craftedItemId ? bot->GetItemCount(c.craftedItemId, false) : 0;
        uint32_t const gainedThisRun = (c.craftedItemId && currentCount > c.lastItemCount) ? (currentCount - c.lastItemCount) : 0;

        TortoiseBots::prof::CraftErrandFacts f;
        f.phase = c.phase;
        f.inWorld = bot->IsInWorld() && bot->FindMap() != nullptr && bot->GetSession() != nullptr;
        f.alive = bot->IsAlive();
        f.isCasting = (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr);
        f.tracksOutputItem = (c.craftedItemId != 0);
        f.itemsGainedThisRun = gainedThisRun;
        f.completedRuns = c.completedRuns;
        f.targetRuns = c.targetRuns;
        f.phaseElapsedMs = c.phaseElapsedMs;

        TortoiseBots::prof::CraftErrandStep const step = TortoiseBots::prof::StepCraftErrand(f);
        switch (step.action)
        {
            case TortoiseBots::prof::CraftErrandAction::Wait:
                if (c.orderSeq != 0)
                    entry.outcome = { c.orderSeq, step.stateLabel, false };
                break;

            case TortoiseBots::prof::CraftErrandAction::CastNext:
            {
                c.completedRuns += 1;
                c.totalGained += gainedThisRun;
                c.lastItemCount = currentCount;
                c.phaseElapsedMs = 0;

                std::string checkCode, checkDetail;
                uint32_t dummySkill = 0, dummyItem = 0;
                std::string dummyName;
                if (!ValidateAndPrepareCraft(bot, c.spellId, dummySkill, dummyItem, dummyName, checkCode, checkDetail))
                {
                    if (c.orderSeq != 0)
                        entry.outcome = { c.orderSeq, "fail:" + checkCode, true };
                    c.active = false;
                    break;
                }
                CastCraftSpell(bot, c.spellId);
                if (c.orderSeq != 0)
                    entry.outcome = { c.orderSeq, "pending:casting", false };
                break;
            }

            case TortoiseBots::prof::CraftErrandAction::FinishOk:
            {
                c.completedRuns += 1;
                c.totalGained += gainedThisRun;
                uint16_t const afterSkill = c.skillId ? bot->GetSkillValue(c.skillId) : 0;
                LogW7("craft", bot, c.spellId, c.beforeSkill, afterSkill,
                    "count=" + std::to_string(c.completedRuns) +
                    " item=" + (c.craftedItemName.empty() ? std::to_string(c.craftedItemId) : c.craftedItemName) +
                    " gained=" + std::to_string(c.totalGained));
                LogSkillUpIfChanged(bot, c.skillId, c.beforeSkill);
                if (c.orderSeq != 0)
                    entry.outcome = { c.orderSeq, "ok", true };
                c.active = false;
                break;
            }

            case TortoiseBots::prof::CraftErrandAction::FinishFail:
            {
                LogW7("craft-fail", bot, c.spellId, c.beforeSkill, c.beforeSkill,
                    std::string("reason=") + (step.failCode ? step.failCode : "unknown"));
                if (c.orderSeq != 0)
                    entry.outcome = { c.orderSeq, std::string("fail:") + (step.failCode ? step.failCode : "unknown"), true };
                c.active = false;
                break;
            }

            case TortoiseBots::prof::CraftErrandAction::None:
            default:
                break;
        }
    }
}

bool PollOrderErrand(Player const* bot, uint32_t orderSeq, std::string& outResult)
{
    if (!bot || orderSeq == 0)
        return false;
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end())
        return false;
    ErrandOutcome const& o = it->second.outcome;
    if (o.orderSeq != orderSeq || o.result.empty())
        return false;
    outResult = o.result;
    return true;
}

void CancelOrderErrand(Player const* bot)
{
    if (!bot)
        return;
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end())
        return;
    it->second.gather.active = false;
    it->second.craft.active = false;
    it->second.outcome = ErrandOutcome{};
}

bool HasActiveErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end())
        return false;
    return it->second.gather.active || it->second.craft.active;
}

std::string ActiveErrandPhase(Player const* bot)
{
    if (!bot)
        return {};
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end())
        return {};
    if (it->second.gather.active)
    {
        switch (it->second.gather.phase)
        {
            case TortoiseBots::prof::GatherPhase::Walking: return "walking";
            case TortoiseBots::prof::GatherPhase::Casting: return "casting";
            case TortoiseBots::prof::GatherPhase::Looting: return "looting";
            default: return "pending";
        }
    }
    if (it->second.craft.active)
        return "casting";
    return {};
}

namespace
{
TortoiseBots::prof::FreeGatherCooldownTracker& FreeGatherTimers()
{
    static TortoiseBots::prof::FreeGatherCooldownTracker tracker;
    return tracker;
}
} // namespace

bool AdvanceFreeGatherTimer(uint64_t botGuidCounter, uint32_t diffMs)
{
    return FreeGatherTimers().Advance(botGuidCounter, diffMs, 5000);
}

void ClearFreeGatherTimer(uint64_t botGuidCounter)
{
    FreeGatherTimers().Clear(botGuidCounter);
}

void ClearBotErrands(ObjectGuid const& botGuid)
{
    Errands().erase(botGuid);
    FreeGatherTimers().Clear(botGuid.GetCounter());
}

void TickFreeGather(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->IsInCombat() || bot->isMoving() || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        return;
    if (HasActiveErrand(bot))
        return;
    std::string code, detail;
    StartGatherNearest(bot, 150.0f, 0, code, detail);
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
        TortoiseBots::prof::GatherNodeFacts const facts = BuildGoNodeFacts(bot, go, maxDistYd, false);
        if (facts.lockSkillId != TortoiseBots::prof::SkillMining && facts.lockSkillId != TortoiseBots::prof::SkillHerbalism)
            continue;
        if (TortoiseBots::prof::EvaluateGatherNode(facts).allow)
            return !HasDangerNearby(bot);
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
