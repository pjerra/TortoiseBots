
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/ThreatCapPolicy.h"
#include "playerbot/BotActionLog.h"
#include "playerbot/ServerFacade.h"
#include "Spells/SpellMgr.h"
#include "Threat/ThreatManager.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "../../../../runtime/RemoteCommanderPolicy.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <unordered_map>

using namespace ai;

namespace
{
// The white damage the threat multiplier exempts: the melee auto-attack, the wand's Shoot and the hunter's Auto Shot.
// The successor white stop has to see them (AiPlayerbot.DungeonRunThreatSuccessorStrict, ThreatCapPolicy.h).
bool AutoAttackAction(Action* action)
{
    return dynamic_cast<MeleeAction*>(action) || dynamic_cast<CastShootAction*>(action) ||
        action->getName() == "auto shot";
}

// One row per bot and second while an interrupt is vetoed with AiPlayerbot.InterruptIgnoresThreat on: the bot's
// threat, the tank's, the interrupt's flat threat and the pull line it was measured against, so a live run shows why
// a Kick or Counterspell still waited (the engine's own `MULT threat x0.000` line carries no numbers).
constexpr uint32 kInterruptVetoLogGapMs = 1000;

std::mutex g_interruptVetoLogMutex;
std::unordered_map<uint64, uint32> g_interruptVetoLog;

void LogInterruptVeto(PlayerbotAI* ai, Player* bot, CastInterruptSpellAction const* action, float botThreat,
                      float tankThreat, float flatThreat, bool ranged)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_interruptVetoLogMutex);
        uint32& last = g_interruptVetoLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kInterruptVetoLogGapMs)
            return;
        last = nowMs;
        if (g_interruptVetoLog.size() > 4096)
        {
            for (auto it = g_interruptVetoLog.begin(); it != g_interruptVetoLog.end();)
                it = nowMs - it->second > kInterruptVetoLogGapMs * 600 ? g_interruptVetoLog.erase(it) : std::next(it);
        }
    }
    char info[192];
    snprintf(info, sizeof(info), "ThreatCap interrupt spell=%s pct=%.0f flat=%.0f line=%.2f bot=%.0f tank=%.0f",
             action->GetSpellName().c_str(), ThreatCapPercentOf(botThreat + flatThreat, tankThreat), flatThreat,
             AggroPullFactor(ranged), botThreat, tankThreat);
    ai::botdiag::BotActionLog::Write(ai, "THREAT", "%s", info);
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, "ThreatCapInterrupt", info, "");
}

// One effect's average points for this caster (ThreatCapAverageEffectPoints: WorldObject::CalculateSpellDamage without
// the roll, the level clamped to the spell's base/max level as the core does), the caster's all-effects spell mods and
// the level-damage attribute.
float AverageEffectPoints(Player* bot, SpellEntry const* info, uint32 effect)
{
    int32 level = int32(bot->GetLevel());
    if (level > int32(info->maxLevel) && info->maxLevel > 0)
        level = int32(info->maxLevel);
    else if (level < int32(info->baseLevel))
        level = int32(info->baseLevel);
    level -= int32(info->spellLevel);
    float points = ThreatCapAverageEffectPoints(info->EffectBasePoints[effect], int(info->EffectBaseDice[effect]),
                                                info->EffectDieSides[effect], info->EffectRealPointsPerLevel[effect],
                                                info->EffectDicePerLevel[effect], level);
    bot->ApplySpellMod(info->Id, SPELLMOD_ALL_EFFECTS, points);
    if ((info->Attributes & SPELL_ATTR_LEVEL_DAMAGE_CALCULATION) && info->spellLevel)
        points *= 0.25f * std::exp(bot->GetLevel() * (70 - int32(info->spellLevel)) / 1000.0f);
    return points;
}

// The average direct damage of one cast of `info` on `target`: every SPELL_EFFECT_SCHOOL_DAMAGE effect's average
// points plus spell power and damage modifiers (SpellDamageBonusDone, SPELL_DIRECT_DAMAGE), as Spell::EffectSchoolDMG.
float AverageDirectDamage(Player* bot, Unit* target, SpellEntry const* info)
{
    float damage = 0.0f;
    for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
    {
        if (info->Effect[effect] != SPELL_EFFECT_SCHOOL_DAMAGE)
            continue;
        float const points = AverageEffectPoints(bot, info, effect);
        if (points < 1.0f)
            continue;
        damage += float(bot->SpellDamageBonusDone(target, info, SpellEffectIndex(effect), uint32(points),
                                                  SPELL_DIRECT_DAMAGE));
    }
    return damage;
}

// The caster's threat mods on `info` as two numbers for ThreatCapCastThreat: the SPELLMOD_THREAT percent (read by
// applying the mods to 100; a flat SPELLMOD_THREAT mod reads as part of the percent) and the caster's total threat
// modifier for the spell's school (ApplyTotalThreatModifier on 1).
void CasterThreatMods(Player* bot, SpellEntry const* info, float& modPct, float& totalMod)
{
    float probe = 100.0f;
    bot->ApplySpellMod(info->Id, SPELLMOD_THREAT, probe);
    modPct = probe - 100.0f;
    totalMod = bot->ApplyTotalThreatModifier(1.0f, info->GetSpellSchoolMask());
}

// The threat one cast of `spellId` adds on `target` when it lands, as an average (AiPlayerbot.ThreatCapCountsCastThreat).
// - Direct damage: AverageDirectDamage.
// - A CHANNELED spell's whole channel: each periodic damage or leech effect (Mind Flay, Drain Life) as its average
//   points through SpellDamageBonusDone(DOT), times the ticks (duration / amplitude, ThreatCapChannelDamage); a
//   periodic trigger effect (Arcane Missiles) as the triggered spell's average direct damage times the ticks, with the
//   triggered spell's own threat multiplier. A DoT that is not channeled counts only its direct part: its threat
//   comes a tick at a time and every later cast is checked again.
// - Threat: ThreatCapCastThreat, as the core adds it when the hit lands: the damage times the spell threat table's
//   multiplier (Unit::DealDamage, sSpellMgr.GetSpellThreatMultiplier), plus the table's flat threat and AP bonus
//   (Spell::HandleThreatSpells), each through the caster's SPELLMOD_THREAT and total threat modifier
//   (ThreatCalcHelper::CalcThreat).
// Left out, so the estimate stays an average and on the low side: crits, the target's damage-taken modifiers and a
// non-channeled DoT's ticks. 0 for a spell the core gives no harmful threat (SPELL_ATTR_EX4_NO_HARMFUL_THREAT). The
// mod reads pass no Spell, so no spell mod charge (Clearcasting, Presence of Mind) is used up (Player::DropModCharge).
float ExpectedCastThreat(Player* bot, Unit* target, uint32 spellId)
{
    SpellEntry const* info = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
    if (!info || !target || info->HasAttribute(SPELL_ATTR_EX4_NO_HARMFUL_THREAT))
        return 0.0f;
    bool const channeled = info->IsChanneledSpell();
    int32 const durationMs = info->GetDuration();
    float damage = AverageDirectDamage(bot, target, info);
    float triggeredThreat = 0.0f;
    float modPct = 0.0f;
    float totalMod = 1.0f;
    CasterThreatMods(bot, info, modPct, totalMod);
    if (channeled)
    {
        for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
        {
            if (info->Effect[effect] != SPELL_EFFECT_APPLY_AURA)
                continue;
            uint32 const aura = info->EffectApplyAuraName[effect];
            int32 const amplitudeMs = int32(info->EffectAmplitude[effect]);
            if (aura == SPELL_AURA_PERIODIC_DAMAGE || aura == SPELL_AURA_PERIODIC_LEECH)
            {
                float const points = AverageEffectPoints(bot, info, effect);
                if (points < 1.0f)
                    continue;
                float const perTick = float(bot->SpellDamageBonusDone(target, info, SpellEffectIndex(effect),
                                                                      uint32(points), DOT));
                damage += ThreatCapChannelDamage(channeled, perTick, durationMs, amplitudeMs);
            }
            else if (aura == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
            {
                SpellEntry const* triggered = sServerFacade.LookupSpellInfo(info->EffectTriggerSpell[effect]);
                if (!triggered || triggered->HasAttribute(SPELL_ATTR_EX4_NO_HARMFUL_THREAT))
                    continue;
                float const perTick = AverageDirectDamage(bot, target, triggered);
                float const channel = ThreatCapChannelDamage(channeled, perTick, durationMs, amplitudeMs);
                float triggeredPct = 0.0f;
                float triggeredTotal = 1.0f;
                CasterThreatMods(bot, triggered, triggeredPct, triggeredTotal);
                triggeredThreat += ThreatCapCastThreat(channel, sSpellMgr.GetSpellThreatMultiplier(triggered), 0.0f,
                                                       0.0f, 0.0f, triggeredPct, triggeredTotal);
            }
        }
    }
    float flatThreat = 0.0f;
    float apBonus = 0.0f;
    if (SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(info->Id))
    {
        flatThreat = float(threatEntry->threat);
        apBonus = threatEntry->ap_bonus;
    }
    float const attackPower = apBonus != 0.0f ? bot->GetTotalAttackPowerValue(info->GetWeaponAttackType()) : 0.0f;
    return ThreatCapCastThreat(damage, sSpellMgr.GetSpellThreatMultiplier(info), flatThreat, apBonus, attackPower,
                               modPct, totalMod) + triggeredThreat;
}

// Whether the bot can crit with `info` on `target`, the core's Unit::IsSpellCrit chance in short: none for a spell that
// cannot crit or has no damage class, the spell crit percent of the school for a magic spell (none for a physical one),
// the weapon crit chance for a melee or ranged attack spell.
bool CastCanCrit(Player* bot, Unit* target, SpellEntry const* info)
{
    if (info->AttributesEx2 & SPELL_ATTR_EX2_CANT_CRIT)
        return false;
    switch (info->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MAGIC:
        {
            SpellSchoolMask const schoolMask = info->GetSpellSchoolMask();
            if (schoolMask & SPELL_SCHOOL_MASK_NORMAL)
                return false;
            return bot->GetSpellCritPercent(GetFirstSchoolInMask(schoolMask)) > 0.0f;
        }
        case SPELL_DAMAGE_CLASS_MELEE:
        case SPELL_DAMAGE_CLASS_RANGED:
            return bot->GetUnitCriticalChance(info->GetWeaponAttackType(), target) > 0.0f;
        default:
            return false;
    }
}

// A crit's damage as a multiple of the normal hit: the core's SpellCriticalDamageBonus on 1000 (x1.5 for spells, x2
// for melee and ranged attack spells, plus crit-damage talents and modifiers). No Spell is passed, so no mod charge is
// used up.
float CastCritMultiplier(Player* bot, Unit* target, SpellEntry const* info)
{
    return float(bot->SpellCriticalDamageBonus(info, 1000, target)) / 1000.0f;
}

// One row per bot and 2 s while a damage cast is held by AiPlayerbot.ThreatCapCountsCastThreat: the bot's threat, the
// cast's expected threat and the tank's, so a live run shows why a Fireball waited (the engine's own
// `MULT threat x0.000` line carries no numbers).
constexpr uint32 kCastVetoLogGapMs = 2000;

std::mutex g_castVetoLogMutex;
std::unordered_map<uint64, uint32> g_castVetoLog;

void LogCastVeto(PlayerbotAI* ai, Player* bot, CastSpellAction const* action, float botThreat, float castThreat,
                 float tankThreat)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_castVetoLogMutex);
        uint32& last = g_castVetoLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kCastVetoLogGapMs)
            return;
        last = nowMs;
        if (g_castVetoLog.size() > 4096)
        {
            for (auto it = g_castVetoLog.begin(); it != g_castVetoLog.end();)
                it = nowMs - it->second > kCastVetoLogGapMs * 600 ? g_castVetoLog.erase(it) : std::next(it);
        }
    }
    char info[192];
    snprintf(info, sizeof(info), "ThreatCap cast spell=%s pct=%.0f cast=%.0f bot=%.0f tank=%.0f",
             action->GetSpellName().c_str(), (botThreat + castThreat) * 100.0f / tankThreat, castThreat, botThreat,
             tankThreat);
    ai::botdiag::BotActionLog::Write(ai, "THREAT", "%s", info);
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, "ThreatCapCast", info, "");
}
}

float ThreatMultiplier::GetValue(Action* action)
{
    // Per-fight suppression (mod-playerbots "neglect threat"): a boss
    // fight strategy that owns positioning/targeting sets this while
    // engaged so threat gating does not veto its scripted target choice.
    if (AI_VALUE(bool, "neglect threat"))
        return 1.0f;

    if (action == NULL)
        return 1.0f;

    // AiPlayerbot.DungeonRunThreatSuccessorStrict: on a boss whose `threat_successor` row carries `strict` (a plain
    // successor row clears the latch and keeps the old exemption) the auto-attack, the wand's Shoot and
    // Auto Shot do not start again while this bot's white stop holds (92 % stop, 85 % resume; tanks never; a 0 reference
    // never stops them). The row decides before the percentage is read; a target without it clears the latch. Auto Shot
    // is SINGLE-threat, so the 80 % veto below still gates its restart. Key off: the old exemption below.
    if (sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict && AutoAttackAction(action))
    {
        Unit* const target = AI_VALUE(Unit*, "current target");
        if (!StrictSuccessorTarget(sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict, StrictThreatSuccessorRow(target)))
            ai->SuccessorWhiteCap().Update(false, false, 0);
        else if (ai->SuccessorWhiteCap().Update(true, PlayerbotAI::IsTank(bot),
                     AI_VALUE2(uint8, "threat", "current target"),
                     AI_VALUE2(float, "tank threat", "current target") == 0.0f))
            return 0.0f;
    }

    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
        return 1.0f;

    // Burn the raid's timed Fankriss Spawn before its enrage. Single-target
    // only; boss damage and unrelated AoE keep their threat gates.
    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_SINGLE &&
        FankrissBurnSpawn(ai, action->GetTarget()))
        return 1.0f;

    // Heals never back off. CastHealingSpellAction carries the AoE threat
    // type, and the threat value reads 100 for any mob in combat that the
    // tank has not touched yet, so while the tank was mounted, idle or on
    // another mob the veto held every heal and shield and the healer
    // watched the party die at full mana. The back-off is for damage that
    // pulls a mob off the tank; a heal that keeps the mob's victim alive is
    // never the worse choice. mod-playerbots does not give its healers this
    // multiplier by default, and its FocusMultiplier exempts heals the same
    // way.
    // A party cure or dispel joins the heals behind AiPlayerbot.DungeonRunCureIgnoresThreat: it is a SINGLE-threat
    // CastSpellAction, so a healer whose own threat on its target sat at the high line had every cure vetoed and
    // Jin'do's Hex stayed on the tank for its full 5 s (ThreatCapPolicy.h ThreatCapExempt).
    // An interrupt joins the heals behind AiPlayerbot.InterruptIgnoresThreat: Kick and the other pure interrupts are
    // SINGLE-threat spell actions, so a rogue at the high line had every Kick vetoed and stood idle at full energy
    // while the caster finished. It is measured on the interrupt's own target (the "interrupt target" pick, which can
    // be another mob than the current target) against the core's pull line: the bot's threat plus the interrupt's flat
    // threat (spell threat table) under 110 % of the tank's when the mob can reach the bot in melee, 130 % otherwise
    // (ThreatCapPolicy.h ThreatCapInterruptExempt). Earth Shock and Death Coil rows stay gated.
    bool const heal = dynamic_cast<CastHealingSpellAction*>(action) != nullptr;
    bool const partyCure = dynamic_cast<CurePartyMemberAction*>(action) != nullptr;
    CastInterruptSpellAction const* interruptAction = dynamic_cast<CastInterruptSpellAction*>(action);
    bool const interrupt = interruptAction != nullptr && interruptAction->IgnoresThreatCap();
    bool const interruptKey = interrupt && sPlayerbotAIConfig.interruptIgnoresThreat;
    float interruptBotThreat = 0.0f;
    float interruptTankThreat = 0.0f;
    float interruptFlatThreat = 0.0f;
    bool interruptRanged = false;
    if (interruptKey)
    {
        Unit* const interruptTarget = action->GetTarget();
        interruptBotThreat = ThreatValue::GetThreat(bot, interruptTarget);
        interruptTankThreat = ThreatValue::GetTankThreat(ai, interruptTarget);
        if (SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(interruptAction->GetSpellID()))
            interruptFlatThreat = threatEntry->threat;
        // The core's own test for the 110 % rule: can the mob reach this attacker with a melee swing.
        interruptRanged = !interruptTarget || !interruptTarget->CanReachWithMeleeAutoAttack(bot);
    }
    if (ThreatCapExempt(heal, partyCure, sPlayerbotAIConfig.dungeonRunCureIgnoresThreat) ||
        ThreatCapInterruptExempt(interrupt, sPlayerbotAIConfig.interruptIgnoresThreat, interruptBotThreat,
                                 interruptTankThreat, interruptFlatThreat, interruptRanged))
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return 1.0f;

    // AiPlayerbot.ThreatCapCountsCastThreat: the high-threat veto below reads only the threat the bot has now, so a
    // slow nuke started under its 80 % line (a Fireball at 79 %) landed as one big hit past the core's pull line. A
    // single-target damage spell from a non-tank at a mob whose victim is a tank is also held while the bot's threat
    // plus the cast's expected threat reaches the victim's threat, capped by the top tank's (ThreatCapPolicy.h
    // ThreatCapCastReference, ThreatCapCastOvershoots). Measured on the action's own target. Interrupt actions (Kick,
    // Pummel, Shield Bash, Earth Shock...) are left to their own rules: stopping the cast is the point. The key is read
    // first: off, nothing more is computed.
    if (sPlayerbotAIConfig.threatCapCountsCastThreat &&
        action->GetThreatType() == ActionThreatType::ACTION_THREAT_SINGLE && !PlayerbotAI::IsTank(bot))
    {
        CastSpellAction const* castAction = dynamic_cast<CastSpellAction*>(action);
        if (castAction && !dynamic_cast<CastInterruptSpellAction*>(action))
        {
            Unit* const castTarget = action->GetTarget();
            Unit* const castVictim = castTarget ? castTarget->GetVictim() : nullptr;
            Player* const castVictimPlayer = castVictim ? castVictim->ToPlayer() : nullptr;
            if (castTarget && !sServerFacade.IsFriendlyTo(castTarget, bot) && castVictimPlayer &&
                castVictimPlayer != bot && PlayerbotAI::IsTank(castVictimPlayer))
            {
                // The current target's numbers are the cached values the high-threat trigger reads too.
                bool const onCurrent = castTarget == AI_VALUE(Unit*, "current target");
                float const castTankThreat = onCurrent ? AI_VALUE2(float, "tank threat", "current target")
                                                       : ThreatValue::GetTankThreat(ai, castTarget);
                float const castBotThreat = onCurrent ? AI_VALUE2(float, "my threat", "current target")
                                                      : ThreatValue::GetThreat(bot, castTarget);
                float const castLine = ThreatCapCastReference(
                    sServerFacade.GetThreatManager(castTarget).getThreat(castVictimPlayer), castTankThreat);
                float const castThreat = ExpectedCastThreat(bot, castTarget, castAction->GetSpellID());
                // The crit rule (b) measures a crit against the core's real pull line: 110 % while the mob can reach
                // the bot with a melee swing (the core's own test), 130 % otherwise.
                SpellEntry const* castInfo = castThreat > 0.0f ? sServerFacade.LookupSpellInfo(castAction->GetSpellID())
                                                               : nullptr;
                bool const castCanCrit = castInfo && CastCanCrit(bot, castTarget, castInfo);
                float const castCritMult = castCanCrit ? CastCritMultiplier(bot, castTarget, castInfo) : 1.0f;
                float const castPullLine = ThreatCapCastPullLine(castTarget->CanReachWithMeleeAutoAttack(bot));
                if (ThreatCapCastOvershoots(sPlayerbotAIConfig.threatCapCountsCastThreat, castBotThreat, castThreat,
                                            castLine, castCritMult, castPullLine, castCanCrit))
                {
                    LogCastVeto(ai, bot, castAction, castBotThreat, castThreat, castLine);
                    return 0.0f;
                }
            }
        }
    }

    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE)
    {
        uint8 threat = AI_VALUE2(uint8, "threat", "aoe");
        if (threat >= 50)
            return 0.0f;
    }


    if (ai->HasStrategy("debug threat", BotState::BOT_STATE_COMBAT))
    {
        if (ai->GetMaster())
        {
            if (AI_VALUE2(bool, "trigger active", "high threat"))
            {
                ai->GetBot()->SendPlaySpellVisual(6372);
            }
            else if (AI_VALUE2(bool, "trigger active", "medium threat"))
            {
                ai->GetBot()->SendPlaySpellVisual(5036);
            }
        }
    }

    if (AI_VALUE2(bool, "trigger active", "high threat"))
    {
        // In a commanded party a mob already hitting the healer or a damage
        // bot is off the tank: backing off it leaves it there
        // (RemoteCommander::CommandedBackoffYields). On a taunt-immune target
        // the back-off is never yielded: the tank cannot take it back, so the
        // damage has to stop for the tanks' threat to pass the member.
        Unit* target = AI_VALUE(Unit*, "current target");
        Unit* victim = target ? target->GetVictim() : nullptr;
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        bool const hitsMember = victimPlayer && bot->GetGroup() && bot->GetGroup()->IsMember(victimPlayer->GetObjectGuid());
        if (hitsMember && TortoiseBots::RemoteCommander::CommandedBackoffYields(
                TortoiseBots::BotCommands::IsInCommandedGroup(bot), true, PlayerbotAI::IsTank(victimPlayer),
                TargetTauntImmune(target)))
            return 1.0f;
        if (interruptKey)
            LogInterruptVeto(ai, bot, interruptAction, interruptBotThreat, interruptTankThreat, interruptFlatThreat,
                             interruptRanged);
        return 0.0f;
    }

    return 1.0f;
}

// The successor white stop (AiPlayerbot.DungeonRunThreatSuccessorStrict): the trigger stops the swing and the auto-repeat
// itself in the trigger pass and is never active, so its handler is never queued. The handler is a no-op at
// ACTION_PASSTROUGH only so the node's first relevance is 100: Engine::ProcessTriggers(minimal) checks just the nodes at
// 100 or more, and an empty node (-1) was skipped on every minimal tick, letting a swing run on past the line. Key off:
// the trigger returns at its first line.
void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode("successor white cap",
        NextAction::array(0, new NextAction("successor white cap noop", ACTION_PASSTROUGH), NULL)));
}

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
