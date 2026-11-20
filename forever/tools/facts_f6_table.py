#!/usr/bin/env python3
"""The field-coverage table of package F6 (forever/docs/RUN-S6-F-FIELDS.tsv).

    python3 forever/tools/facts_f6_table.py <facts_rfc fields.tsv> <classes.tsv> > forever/docs/RUN-S6-F-FIELDS.tsv

Inputs: the R0 report's field list (`facts_rfc_report.py --tsv`: field, decl, group, dynamic, static_reads, ...) and the class of each field
(A / yes / check / no / D, from RUN-S5-FACTS-RFC.md). One line per `Facts` field: what the F6 reader does with it.
Status: filled (read from the core), config (copied from aiplayerbot.conf), forced (the glue sets the value, whatever the conf says),
notknown (the core cannot tell: the default is the do-less value), skipped (not filled: the RFC run does not read it in a way F6 serves; default stands),
never (no read site in the policy).
"""
import csv, sys

F = "filled"; C = "config"; X = "forced"; N = "notknown"; S = "skipped"
GRP = "Group.h:341 GetMemberSlots"
T = {}
def put(names, status, src):
    for n in names.split():
        T[n] = (status, src)

# ---- A (read in the simulated run) ----
put("players", F, "slots of the lead's group that are not host bots (Group.h:341, ObjectAccessor::FindPlayer)")
put("raid", X, "raid.on stays false: a bot-led party is no raid binding (DungeonRunRaid not read)")
put("raidIcons raidIconsRead", F, "Group::GetTargetIcon Group.h:362, each mark a guid handle")
put("bossDeadNearSpot bossDeadNearLead bossCorpseGuid bossCorpseX bossCorpseY bossCorpseZ", F, "grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive")
put("bossOwnDead bossOwnDeadEntry", F, "spawn id of the S5 unit row (role boss) -> Map::GetCreatureBySpawnId Map.h:453; not loaded: Map::GetCreatureRespawnTime Map.h:541 in the future = dead. No unit row matched: not known (false)")
put("bossGateStacks bossGateMax bossGateSpell bossGateHoldS bossGatePriests", X, "no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty)")
put("seekSiphonCycle", X, "0: the seek-aura clock is Hakkar only (map 309)")
put("linkedDeath nearWipeStopCount nearWipeStopWindowMs fightForward lightRest packClearMarginYd packClearRepulls walkBackAfterWonFight retreatAt2Dead", C, "PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read")
put("walkBackRescue teleportReturnYd reviveHoldYd dryWalk rescueRead rezHold lootFfaOn", X, "forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader")
put("joinerCeiling aggroWalkRing pullStand standClearance standDiscipline standPullPreCc autoCc autoCcRisky totemFocus auraGatedTargets packFocusSparesTacticUnits respawnNest stragglerPull", X, "forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported")
put("engagedUnits engagedRead staleCombat anyInCombat leadInCombat", F, "Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets")
put("ownFightRead ownFightUnits", F, "one ObjectAccessor::GetUnit per remembered guid handle (Alive/Dead/Gone; a dropped handle stays Unknown)")
put("killFirst", F, "the engaged units whose entry the plan lists in kill_first (the grid read of a totem key is not ported)")
put("leadHasAttacker leadAttacker leadHit partyAttackers leadIsTank leadStaying leadStayKnown leadStayX leadStayY leadStayZ leadReaching reachAggro", F, "getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy")
put("minHealerManaPct minHealthPct minManaPct rezzer", F, "Unit::GetHealth/GetPower of the living bots; priest, paladin or shaman out of combat")
put("eating", F, "PlayerbotAI::HasSeatedMeal(food/drink) PlayerbotAI.h:1060")
put("total alive dead members deadNames maxMemberYd", F, "the lead's group slots: a slot with no player in the world counts dead and 1000 yd away")
put("leadAlive leadX leadY leadZ carrier team", F, "the lead Player (Player::IsAlive, position, GetName, GetTeam)")
put("leadOffFloor leadFloorZ", F, "Map height read (GetHeightCompat, Map.h:330) and the navmesh presence (WorldPosition::isMmapLoaded); no feet probe: leadFloorZ is the height read")
put("leadResting leadCasting leadMoving leadInWater leadMoveFailed leadMoveFailReason", F, "PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value")
put("pointLiquid", F, "terrain liquid status at the point (TbCompat LiquidStatusCompat over TerrainInfo::GetLiquidStatus); an unreadable terrain refuses the point")
put("anchorPathRead anchorReachable", F, "one PathGenerator query (PathGenerator.h:104), only on the tick after a gather asked")
put("directPathCrossesPack", N, "false: the test needs the live list of uncleared packs along the path")
put("approachIncomplete approachKnown approachRead approachX approachY approachZ seen leadPathGood leadIsland", F, "one PathGenerator query toward the step point (PathGenerator.h:104/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false")
put("units patrollers packResidue packNearestLeft packNearestLeftRead", F, "grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation")
put("hopUnits", F, "Trinity::AnyUnfriendlyUnitInObjectRangeCheck Grids/Notifiers/GridNotifiers.h:1190 + Cell::VisitAllObjects, IsWithinLOSInMap Object.h:364, at most 4 path queries")
put("leadShotYd", N, "0: the tank's ranged shot range is not read (pull stand is forced off)")
# ---- C yes ----
put("bossSearchRead bossSearchAlive bossSearchAliveEntry bossAliveX bossAliveY bossAliveZ bossSearchDead bossSearchUnloaded bossSearchOwnDead bossSearchCorpseGuid bossSearchCorpseX bossSearchCorpseY bossSearchCorpseZ", F, "the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278")
put("bossSearchNoSpawn bossSearchEventOff bossSearchEventId bossSearchEventName", N, "false/0: no spawn-less or game-event read (the RFC bosses are plain spawns with unit rows)")
put("shortcutRead shortcutFor shortcutOk forwardRead forwardFor forwardPathOk forwardLos forwardHostileNear forwardHopAlive", F, "PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted)")
# ---- C check ----
put("bossHpPct", F, "Unit::GetHealthPct of the living plan boss")
put("bossBerserk seekSiphonDosed seekSiphonUndosed", N, "false/0: Hakkar's enrage and siphon are map 309 only")
put("goodSpotCut leadStayWalkResting leadStayWalkFails stallDirectRead stallDirectClear", N, "default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported")
put("dpsAlive", F, "living bots that are neither tank nor healer")
# The class of everything else: skipped / never
def main():
    fields = {r["field"]: r for r in csv.DictReader(open(sys.argv[1]), delimiter="\t")}
    cls = {}
    for line in open(sys.argv[2]):
        f, decl, c, grp = line.rstrip("\n").split("\t")
        cls[f] = (decl, c, grp)
    w = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    w.writerow(["field", "decl", "class", "status", "static_reads", "source_or_reason"])
    for name in fields:
        decl, c, grp = cls[name]
        if name in T:
            status, src = T[name]
        elif c == "D":
            status, src = "never", "no read site in DungeonRunPolicy.h"
        else:
            status, src = S, "not read by an RFC run (" + grp + "): the Facts() default stands"
        w.writerow([name, decl, c, status, fields[name]["static_reads"], src])
main()
