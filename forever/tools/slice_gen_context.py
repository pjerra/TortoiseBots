#!/usr/bin/env python3
"""Generates forever/src/slice/SliceAiObjectContext.cpp (FOREVER-SLICE1-SPEC section 4, package R2; slice 2 keys added in R3).

    python3 forever/tools/slice_gen_context.py            # rewrite the file
    python3 forever/tools/slice_gen_context.py --check    # exit 1 when the file on disk differs from what would be written
    python3 forever/tools/slice_gen_context.py --report   # also list registered classes whose .cpp is not in SLICE2.txt

The key lists are SLICE_KEYS below (the spec's lists, corrected against the code). For each key the creator text is
copied byte for byte from the full build's context header, inside a context class of the same shape (same
shared/siblings flags). The headers of the classes the creators build are found by scanning ai/playerbot for the class
definition, so the slice file includes only what it registers. slice_registry_check.py proves the copy is faithful.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402

ROOT = chk.ROOT
OUT = chk.SLICE_CTX

# The spec's lists (FOREVER-SLICE1-SPEC section 4). Differences from the spec are recorded in
# forever/docs/SLICE1-R2-NOTES.md; at the time of writing there are none.
# Slice 2 (R3, decision D6): SLICE2_KEYS below are the 69 keys of forever/docs/SLICE2-KEYS.md, added to the slice-1 lists.
SLICE_KEYS = {
    "strategy": "dps assist, close, follow, guard, no aoe, auto cc",
    "action": "melee, reach melee, follow, stop follow, dps assist, select new target, set combat state, "
              "set non combat state, set dead state, interrupt current spell, formation station, guard chat shortcut, guard, stance",
    "trigger": "combat start, combat end, death, enemy out of melee, invalid target, in pve, in pvp, in raid fight, "
               "often, very often, random, critical health, almost full health, low health, target critical health, "
               "light rage available, medium rage available, high rage available, melee light aoe, melee medium aoe, "
               "melee high aoe, melee very high aoe, ranged light aoe, ranged medium aoe, not dps target active, "
               "update follow, stop follow, master target active, out of free move range, formation station",
    "value": "action useful, add hazard, and, aoe count, aoe heal, aoe position, attack target, "
             "attacker of non-tank target, attackers, attackers count, attackers targeting me, automated attack target, "
             "bag space, balance, behind, can free move, cc target, closest attacker targeting me, collision, combat, "
             "combat start time, combo, corpse run, current cc target, current target, dead, death count, distance, "
             "dps aoe target, dps target, duel target, durability, durability inventory, enemy healer target, energy, "
             "estimated group dps, explicit attack target, facing, focus heal targets, follow target, formation, "
             "formation position, free move center, free move range, future travel destinations, group, group and, group members, group or, "
             "group ready, has aggro, has area debuff, has attackers, has mana, has strategy, has totem, hazards, "
             "health, ignore rpg target, incoming damage, inside target, interrupt target, invalid target, "
             "inventory items, item count, item for spell, last area trigger, last long move, last movement, "
             "last spell cast, last spell cast time, last taxi, leader travel target, lowest durability, main tank, "
             "mana, mana save level, manual bool, manual follow target, manual int, manual saved int, manual string, "
             "manual time, master target, mounted, move style, moving, my attacker count, my threat, nearest adds, "
             "nearest area triggers no los, nearest damaging traps, nearest dynamic objects no los, nearest friendly players, nearest game objects, "
             "nearest game objects no los, nearest npcs, nearest stealthed units, neglect threat, new player nearby, "
             "old target, party member to protect, party member to remove roots, party member without aura, "
             "party member without my aura, party tank without aura, pet target, pos, position, possible adds, "
             "possible attack targets, possible targets, possible targets no los, possible triggers, pull target, rage, "
             "random bot update, range, rpg target, rti, rti cc, rti cc target, rti target, run cc family, self target, "
             "skip spells list, snare target, spell cast useful, spell id, stance, stay time, stored hazards, swimming, "
             "talent spec, tank target, tank threat, tank to remove roots, threat, travel target, trigger active, "
             "trinkets on use, wait for attack time",
}
SLICE2_KEYS = {
    "strategy": "ranged, food, loot, cast time, default, heal interrupt",
    "action": "reach spell, reach party member to heal, flee, shoot, drink, food, use, loot, open loot, move to loot, "
              "add all loot, store loot",
    "trigger": "enemy out of spell, enemy too close for spell, enemy five yards, medium mana, no mana, high mana, "
               "medium health, healer should attack, healer should wand, melee medium aoe and healer should attack, "
               "medium threat, target of attacker, party member critical health, party member low health, "
               "party member medium health, party member almost full health, party member to heal out of spell range, "
               "group heal setting, medium group heal setting, resume drink, resume eat, loot available, can loot, "
               "far from loot target, timer, loot response, heal target full health",
    "value": "party member to heal, party member to dispel, buff targets, patchwerk soaker to shield, drink duration, "
             "eat duration, drink unfinished, rest to full, should drink, should eat, spell ready, custom position, "
             "nearest npcs no los, loot target, available loot, has available loot, can loot, always loot list, "
             "skip loot list, should loot object, stack space for item, force item usage, nearest corpses, "
             "loot strategy, should get money, inventory item ids, incoming heals",
}
# Slice 3 (porting worker, FOREVER-SLICE3-SPEC section 3, listed with creator file:line in forever/docs/SLICE3-KEYS.md): the tank, the pull, the stay
# and wait strategies and the order actions. Keys the spec deliberately leaves out: `chat` (the three pull rows are in PULL_KEYS).
SLICE3_KEYS = {
    "strategy": "tank assist, threat, stay, free, passive, tank face, wait for attack",
    "action": "tank assist, stay, return to stay position, pull my target, end pull, pull start, pull action, pull end, reach pull, "
              "release pull hold, return to pull position, wait for attack keep safe distance, tank face away, set facing, "
              "successor white cap noop, threat reset white stop noop, attack my target, attack rti target, follow chat shortcut, "
              "stay chat shortcut, follow target",
    "trigger": "tank assist, lose aggro, has aggro, party member attacked, protect party member, tank face needed, high threat, "
               "return to stay position, pull start, pull end, pull hold expired, pull anchor done, return to pull position, "
               "wait for attack safe distance, successor white cap, threat reset white stop",
    "value": "master position",
}
# Slice 4 (package P4, FOREVER-SLICE4-SPEC section 3, listed with creator file:line in forever/docs/SLICE4-P-NOTES.md section 4): death and resurrection. Left out on
# purpose (each one does less): repop, spirit healer, should spirit healer, find corpse, revive from corpse, corpse near, dead (trigger),
# falling far, move long stuck, bg reset objective force, corpse run, resurrect request (the host calls the action), area trigger, reach area trigger,
# within area trigger, use meeting stone, summon.
SLICE4_KEYS = {
    "strategy": "dead",
    "action": "auto release, accept resurrect, release, reach party member to resurrect, self resurrect",
    "trigger": "party member dead, resurrect",
    "value": "party member to resurrect",
}
# `behind` (the rogue kit's strategy, SetBehindCombatStrategy) and the three keys it fires (`set behind`, `rear flank`, `rear flank needed`) are NOT in the spec's table
# (its closure read only the mage and rogue folders, not the kit): H8 adds them because the spec's rogue kit names `behind` (section 2) and section 5 relies on it.
# Slice 8 (package H8, FOREVER-SLICE-MAGE-ROGUE-SPEC section 3 "Registry keys", forever/docs/SLICE8-K-NOTES.md "Needs from H8"; listed with creator file:line in
# forever/docs/SLICE8-H-NOTES.md): the keys the compiled mage and rogue contexts and their kits name that the slice did not register yet. The give-item values
# (`party member without item/food/water`) are not in the spec's table but `give food`/`give water` read them (K8 notes). Left unregistered on purpose (each one does
# less): apply oil, throw grenade, oil of immolation, thistle tea (item casts); enemy flagcarrier near, player has flag, enemy player target, has enemy player targets
# (PvP); grind target (the stealth trigger then reads `dps target` only); no blink instance; hemorrhage back/front (no context defines them).
SLICE8_KEYS = {
    "strategy": "hold position, behind",
    "action": "give food, give water, cancel channel, set behind, rear flank, reach cc",
    "trigger": "give food, give water, low mana, enemy ten yards, enemy is close, behind target, not behind target, high energy available, "
               "no energy available, has aggro in raid group, rear flank needed",
    "value": "party member without item, party member without food, party member without water",
}
# Slice 10 (package H10, FOREVER-SLICE10-HYBRIDS-SPEC section 3 "Registry keys", forever/docs/SLICE10-K-NOTES.md section 4; listed with creator file:line in
# forever/docs/SLICE10-H-NOTES.md): the keys the compiled druid, paladin and shaman contexts and their kits name that the slice did not register yet. `low mana` and
# `enemy is close` are not in the spec's table but the restoration shaman (`low mana`) and the holy paladin (`enemy is close`) kits fire them (K10 notes: also on
# slice 8's list, listed once). Left unregistered on purpose (each one does less): travel target traveling, enemy player
# target, has enemy player targets, grind target; enemy flagcarrier near, player has flag, new pet, has (greater) blessing of salvation, resist aura swap over, not
# sensing undead, kick, aquatic form; apply oil/stone, oil of immolation, throw grenade, mana/stoneshield potion, remove (greater) blessing of salvation, set pet stance,
# sense undead, switch to melee/ranged, cancel channel, powershift, reach party member for totem (no totem order). Cat, balance, enhancement, elemental and the Turtle
# shaman tank kit are out of the slice (D2), so behind target / not behind target (the feral dps) stay out too. `restore spec aura` (the spec's table lists it) is NOT registered:
# its class RestoreSpecAuraAction is defined in strategy/actions/DungeonActions.cpp (1,036 lines of raid and dungeon actions, not in the list: the option-ON link failed with
# `undefined vtable for ai::RestoreSpecAuraAction`), and its only trigger `resist aura swap over` is on the unregistered list, so the action could never fire.
SLICE10_KEYS = {
    "strategy": "",
    "action": "move out of enemy contact",
    "trigger": "enemy too close for melee, not facing target, rooted, party member rooted, tank rooted, enough mana, healer low mana, medium aoe heal, "
               "ranged medium aoe and healer should attack, target of attacker close, loose adds near, low mana, enemy is close",
    "value": "has own totem, have any own totem, possible attack targets count, revive targets, boost targets, healer low mana",
}
# Slice 11 (package H11, FOREVER-SLICE11-PETS-SPEC sections 3 and 10, listed with creator file:line in forever/docs/SLICE11-NOTES.md section 5): the generic pet keys the hunter and
# warlock kits fire, and the hunter's dead-zone and range keys. Left unregistered on purpose (each one makes the bot do less, spec section 3): the chat `tame` action and
# trigger (TameAction.cpp stays out), `flee with pet`, `boss tactics seek pet`, `thaddius *`, the `choice pet` / `choice sting` / `choice aspect` rows
# (so `.bot choice` stays off, as S10 D5), `apply oil`, `apply stone`, `equip upgrades`, `dispel enrage` (Tranquilizing Shot is level 60), `player has flag`, `nearest stealthed unit`,
# `warlock spread attacker my aura` (the raid DoT spread), `aoe fear`. `pet target` is registered since slice 1.
SLICE11_KEYS = {
    "strategy": "",
    "action": "initialize pet, toggle pet spell, set pet stance, pet attack, pet, cancel channel, switch to melee, switch to ranged",
    "trigger": "no pet, has pet, new pet, pet attack, pet, enemy ten yards, enemy fifteen yards, low mana, target of attacker close, has aggro in raid group",
    "value": "pet dead, pet happy, active spell",
}
# World-life slice W1 (FOREVER-W1-W6A-SPEC section 1.3, forever/docs/W1-NOTES.md): the grind target value and its strategy, the
# actions and triggers of the open-world dead kit, and the graveyard values. Left out of the free kit on purpose (each one does less, spec
# section 1.3): `rpg`, `tfish`, `maintenance`, `attack tagged`, `start duel`, `group`, `guild` (the collision keys are in
# COLLISION_KEYS, which a player can add, still not part of the free kit); the travel keys are TRAVEL_KEYS below and `travel`
# is in the free kit, `rpg` is added by the factory under AutoDoQuests (LIFE_* keys); `need for quest` (false), `enemy flag
# carrier`; the battleground reset action and the `falling far` / `val::gt32::*` triggers (the host's ghost rescue covers
# the first; the stuck triggers are STUCK_KEYS).
W1_KEYS = {
    "strategy": "grind",
    "action": "attack anything, avoid elite, idle wander, find corpse, revive from corpse, spirit healer",
    "trigger": "no target, dangerous elite near, dead, corpse near, val",
    "value": "grind target, can fight elite, dangerous elite, graveyard, best graveyard, should spirit healer, can move around",
}
# World slice W0-A (FOREVER-W0-W4-SPEC.md, round W0-A): the pool composes and creates characters host-side (host/pool), so it registers
# no context key. The block stands so the later W0 rounds and W4 add theirs next to it instead of starting a second list.
WL0_KEYS = {
    "strategy": "",
    "action": "",
    "trigger": "",
    "value": "",
}


def _join(*ds):
    # One entry per key and kind: a key two slice lists both name (slice 8's and slice 10's `low mana`, `enemy is close`) is
    # registered once, in the order of the first list that names it.
    out = {}
    for k in SLICE_KEYS:
        seen = []
        for d in ds:
            for x in d[k].split(","):
                x = x.strip()
                if x and x not in seen:
                    seen.append(x)
        out[k] = seen
    return out
# Slice 9 (package L9, FOREVER-SLICE9-SPEC section 6, listed with creator file:line in forever/docs/SLICE9-L-NOTES.md): item loot and group loot rolls. The
# `roll` strategy is the one whose `very often` trigger fires `auto loot roll`; `loot start roll` / `loot vote` are the pass-through names of the
# `default` strategy (WorldPacketHandlerStrategy::supported), fed by the poll of TbCompat_S9.h. Piece C feeds `loot roll won` from the PollRollWon
# host poll (forever/src/host/TbRollWon.*): it remembers the bot's open rolls and raises the event once when a roll disappears with the bot as
# winner, so `log roll won` logs the win and the already-registered `equip upgrades` action equips it when it is an upgrade. Still left out on
# purpose (each one does less): `delayed roll`, `equip upgrades` (fed by the maintenance strategy, W6_KEYS), `item push result` (fed by the same
# strategy's bag-generation poll); the item usage value's own dependencies (mount list, craft spells, should craft spell, free money for): their
# absence reads as the empty value, so no mount, craft or purchase is planned.
SLICE9_KEYS = {
    "strategy": "roll",
    "action": "loot start roll, loot vote, loot roll, auto loot roll, roll, release loot, log roll won",
    "trigger": "loot start roll, loot vote, loot roll, loot roll won",
    "value": "active rolls, seen need rolls, item usage, skip go loot list",
}
# W6a (package E6): auto-equip upgrades for every bot (`equipment audit` 300 s + `item push result` bag-generation poll); commanded bots self-equip iff ClaimedBotSelfEquip.
# Pool gear top-up rides the same `equipment audit` row (PoolGearTopUpAction.cpp, AiPlayerbot.PoolGearTopUpOnLevel): its gear pass is
# the host's W6b gear index, so only a masterless pool bot with the knob on does anything.
# `use random recipe` and `use random quest item` are R3A_KEYS; `open random item` and `auto complete quest` stay out (see
# R3A_KEYS and the note above W2_KEYS).
# `disenchant random item` / `enchant random item` stay out on purpose: TbHostProfessionUpkeep already does both for the bots the
# design gives the upkeep to (free bots, gated by TortoiseBots.World.Disenchant / .EnchantGear), while the slice gives `maintenance`
# to every bot, so the legacy rows would also dismantle a commanded player's loot and spend its reagents outside those switches (TB
# adds maintenance only to a bot without a live master; their isUseful has no master gate). The enchant row would also need
# `has reagents for` (CraftValues.cpp, not on the list) before it could score a spell at all.
W6_KEYS = {
    "strategy": "maintenance",
    "action": "equip upgrades, pool gear top up",
    "trigger": "equipment audit, item push result",
    "value": "",
}
# Two more keys the compiled strategies fire stay out for a proved reason, not by oversight.
# `auto complete quest` (AutoCompleteQuestAction.cpp): its completing list holds only the placeholder quest 0, so it can act only
# through the `quest` cheat, and no bot can hold that cheat here (CheatAction::GetCheatMask is the slice stub that answers 0; the
# per-bot SetCheat lives in the uncompiled CheatAction.cpp). The quest objective credits it calls (the flat ReqItemId/
# ReqCreatureOrGOId arrays, CastedCreatureOrGO, KilledMonster(CreatureInfo*)) are not ForeverCore APIs either; the debug quest
# command's Forever stub says the same. A creator registered for it would do nothing on this build.
# `check mail` (CheckMailAction.cpp): its guild branch takes mail straight into the bags with no mailbox, but every compiled mail
# take and send goes through the core's session handlers, which require an interactable mailbox (WorldSession::CanOpenMailBox,
# MailHandler.cpp:34). It would also take the disenchant and lockbox service mail the host chains own (TbHostGuildOrderDisenchant's
# IsDisenchantServiceMail, TbHostThievery's IsLockboxServiceMail) and delete returned mail behind the core's back. Mail reading and
# delivery stay with `mail` at a mailbox (W2) and the rpg/travel mail rows.
# W2 (D): vendor, repair, trainer, mail read (actions + budget values).
W2_KEYS = {
    "strategy": "",
    "action": "sell, buy, repair, trainer, mail",
    "trigger": "",
    "value": "free money for, total money needed for, money needed for, repair cost, max repair cost, min repair cost, has all money for",
}
# W3 (package E): quests (accept, turn-in with reward choice, drop, share; quest strategies and actions).
W3_KEYS = {
    "strategy": "quest, accept all quests",
    "action": "talk to quest giver, accept quest, accept all quests, accept quest share, drop, clean quest log, share, auto share quest, quest reward",
    "trigger": "quest share",
    "value": "quest reward",
}
# S1-R2 (chat commands in, FOREVER-SMALL-SLICES-SPEC section 3): the `chat` strategy and the keys of the commands the spec's
# table registers. Every other trigger the strategy names stays unregistered on purpose (each one does less): the quest, item, trade
# and guild commands belong to their own packages (`accept`, `q`, `e`, `t`, `s`, `b`, `u`, the guild orders), `cast`/`castnc` name the
# 1,186-line CastCustomSpellAction.cpp, `jump` is out by D5, and the rest (`pet`, `talk`, `grind`, `wander`, `portal`, `summon`, `who`,
# `where`, `stance`, ...) are the kits this slice does not have. Creator file:line of every key here: forever/docs/S1-R2-NOTES.md.
CH_KEYS = {
    "strategy": "chat",
    "action": "tank attack chat shortcut, flee chat shortcut, max dps chat shortcut, "
              "stats, help, spells, spell, los, rti, range, save mana, tell attackers, tell possible attack targets, co, nc, formation",
    "trigger": "follow, stay, attack, tank attack, flee, max dps, "
               "stats, help, spells, spell, los, rti, range, save mana, attackers, possible attack targets, co, nc, formation",
    "value": "",
}
# R4 of PLAYER-COMMANDS-SPEC ("chat grammar A", forever/docs/PC-R1-NOTES.md has the round table): the chat words whose ACTION is
# already registered (or whose class is already compiled), so the bot answers the master's word without a new .cpp. The chat
# strategy's pass-through fires an action of the word's own name, so a word also registers that action when the full build has one;
# where the strategy names another action (`e` -> equip, `u` -> use, `s` -> sell, `b` -> buy, `bb` -> buy back, `free` -> free chat
# shortcut, `pull` -> pull my target, `attack rti` -> attack rti target, `accept` -> accept quest, `revive` -> spirit healer,
# `follow target` -> follow target) only the trigger is new. Left out on purpose: `pullback` (`pullback my target` is not
# registered; `.tb order pullback` exists), `de react all` (strategy edits of other states: with the behaviour toggles later),
# `guard` / `wander` / `grind` / `runaway` / `warning` (those strategies are not registered), the value setters (`boost target`,
# `buff target`, `revive target`, `wait for attack time`, `self res`), `portal` (round 7) and `jump` (D5). Registered keys: forever/docs/PC-R4-NOTES.md.
PC_CHAT_KEYS = {
    "strategy": "",
    "action": "buy back, corpse run, equip, free chat shortcut, position",
    "trigger": "quest reward, drop, roll, share, loot, add all loot, release, repair, u, use, s, b, trainer, attack rti, pull, "
               "end pull, accept, follow target, revive, mail, ah, ah bid, ah cancel, "
               "corpse run, e, equip, bb, free, position",
    "value": "",
}
# R5 of PLAYER-COMMANDS-SPEC ("chat grammar B"): the chat words that need ONE small legacy action file each, now on the build list
# (forever/SLICE-W4.txt): `who` (WhoAction.cpp), `c` / `items` / `inventory` / `inv` (`item count`, TellItemCountAction.cpp), `ue`
# (`unequip`, UnequipAction.cpp), `quests` (ListQuestsActions.cpp), `leave` (LeaveGroupAction.cpp) and `rep` / `reputation`
# (TellReputationAction.cpp). Every trigger copies its ChatTriggerContext.h creator, every action its ChatActionContext.h creator.
# `ll` and `ss` (LootStrategyAction.cpp, SkipSpellsListAction.cpp) are not in this round. Who may use a word is the legacy whisper
# gate's table (PlayerbotAI::AuthTierForCommand), unchanged. Notes: forever/docs/PC-R5-NOTES.md.
PC_CHAT_B_KEYS = {
    "strategy": "",
    "action": "who, item count, unequip, quests, leave, reputation",
    "trigger": "who, c, items, inventory, inv, ue, quests, leave, rep, reputation",
    # The values those actions read and the slice did not register (R5 fix, live batch 4): `focus travel target` is read by the
    # PlayerTravelInfo constructor (TravelMgr.cpp:72) that ListQuestsAction builds on every call (a missing value read as a type the
    # stand-in lacked crashed the worldserver on `quests`), `experience` and `honor` by LeaveGroupAction.cpp (MEM_AI_VALUE derefs them).
    "value": "focus travel target, experience, honor",
}
# W8 (auction house): ah sell/list, ah buy, ah cancel.
W8_KEYS = {
    "strategy": "",
    "action": "ah, ah bid, ah cancel",
    "trigger": "",
    "value": "",
}
# Reactions: flee, kite, runaway, avoid aoe, racials, mark rti, focus, spread distance, fear/stun/threat triggers, target values.
REACTIONS_KEYS = {
    "strategy": "flee, flee from adds, kite, runaway, react, avoid aoe, racials, mark rti, focus, focus heal targets, focus rti targets",
    "action": "avoid aoe, flee from area, runaway, mark rti, spread distance, "
              "war stomp, berserking, blood fury, cannibalize, escape artist, shadowmeld, stoneform, "
              "perception, will of the forsaken, mana tap, arcane torrent",
    "trigger": "feared, stunned, fear sleep sap, low tank threat, some threat, no threat, "
               "panic, outnumbered, critical health no master, has nearest adds, aoe threat nearby, has area debuff, no rti target, "
               "berserking, blood fury, generic boost, cannibalize, will of the forsaken, stoneform, shadowmeld, "
               "mana tap, arcane torrent, war stomp, perception",
    "value": "least hp target, attacker without aura, attacker without my aura, spread distance",
}
# Combat items (issue #124): potions, healthstones, bandages, trinkets, oils, stones, consumables, soulstone target.
COMBAT_ITEMS_KEYS = {
    "strategy": "potions, consumables",
    "action": "whipper root tuber, healthstone, healing potion, mana potion, dark rune, use bandage, anti-venom, "
              "use consumable, use trinket, apply stone, apply oil, buff",
    "trigger": "has poison debuff, use trinket",
    "value": "party member to soulstone",
}
CLASS_EXTRAS_KEYS = {
    "strategy": "",
    "action": "tame, use lightwell, reach party member for totem",
    "trigger": "dispel enrage",
    "value": "warlock spread attacker my aura, warlock spread attacker, warlock spread attacker manual",
}
# Piece c-store (TBF #133): the persisted "manual saved string" AI value. TB keeps every class choice (totem/blessing/choice keys) as a
# qualified "manual saved string" value persisted by sPlayerbotDbStore into characters.ai_playerbot_db_store; the slice left it
# unregistered (slices 8/10/11 notes above), so no choice reader could resolve it. Registered alone it changes nothing the bot does:
# the triggers and strategies that read it (choice rows, ordered totem/blessing rows) stay unregistered until the #133 pieces land.
STORE_KEYS = {
    "strategy": "",
    "action": "",
    "trigger": "",
    "value": "manual saved string",
}
BEHAVIOR_KEYS = {
    "strategy": "no aoe, auto cc, conserve mana, mimic consumables, spread, no blink instance",
    "action": "",
    "trigger": "",
    "value": "",
}
STRAT_STORE_KEYS = {
    "strategy": "",
    "action": "de, react, all, reset ai, reset strats, reset values, reset ai soft",
    "trigger": "de, react, all, reset ai, reset strats, reset values",
    "value": "",
}
# Piece P135-C (TBF #135): custom strategy (`custom::<name>`) and `cs` edit chat action/trigger.
PRESET_KEYS = {
    "strategy": "custom",
    "action": "cs",
    "trigger": "cs",
    "value": "",
}
# Rows the compiled kits fire but the slice left unregistered: the AoE dps row, the salvation removal row, the
# check-values pre-warm, the target books the reach and give-up paths write, the corpse-run timers and three
# behaviour toggles that stay off until a player enables them.
S209_KEYS = {
    "strategy": "dps aoe, "
                "map, map full, "
                "attack tagged, preheal, silent",
    "action": "dps aoe, "
              "remove blessing of salvation, remove greater blessing of salvation, "
              "check values",
    "trigger": "not dps aoe target active, "
               "has blessing of salvation, has greater blessing of salvation",
    "value": "all targets, "
             "keepout set aside, runners let go, unreachable entries, unreachable targets, "
             "current position, time since last change",
}
# The pull rows slice 3 left out. A tank's kit keeps "pull" on the combat engine, so the one row that can start a
# fight without an order is the non-combat `should pull`: it fires only in a dungeon, on a tank, with a live
# grouped party where nobody fights and the healer has mana (PullTriggers.cpp), and it pulls a reachable target.
# "pull back" is the return leg the ordered pulls switch on with ChangeStrategy("+pull back", ALL).
PULL_KEYS = {
    "strategy": "pull back",
    "action": "pull nearest target",
    "trigger": "should pull",
    "value": "",
}
# The collision row: the bot unstacked itself from a real player in TB. The full factory adds the strategy to every
# random and real-player bot (AiFactory.cpp:1546, :1588, :1631); the slice kit does not, so the strategy stays a
# player add (.bot nc collision) and the keys below only make that add work. The action is useful only when the
# bot overlaps a unit and a non-bot player is within reach (MovementActions.cpp:5492).
COLLISION_KEYS = {
    "strategy": "collision",
    "action": "move out of collision",
    "trigger": "collision",
    "value": "nearest non bot players",
}
# The sit strategy a player adds to a stayed bot: SitTrigger fires once the stay has lasted SitDelay, SitAction puts
# the bot in the sit stand state. Nothing adds it to a kit, in TB either.
SIT_KEYS = {
    "strategy": "sit",
    "action": "sit",
    "trigger": "sit",
    "value": "",
}
# Combat/movement port: the chat `jump` word, the melee kit's `jump::chase` row and the `follow jump` marker that makes
# a following bot take a jump shortcut, now that JumpAction drives the core's jump spline instead of the 1.12 jump
# packets (earlier slices left `jump` out because the jump did nothing).
JUMP_KEYS = {
    "strategy": "follow jump",
    "action": "jump",
    "trigger": "jump",
    "value": "",
}
# TBF #142 (diagnostics): the whisper words `debug`/`cdebug` (the DebugAction families that survive on
# ForeverCore plus the stubs in forever/src/slice/SliceDebugStubs.cpp), `log` (LogLevelAction), `set value`
# (SetValueAction) and the log-level value they read. The debug strategies are registered too, so bare
# `debug` can turn the bot's action-line feedback on (`-debug` turns it off). Kept apart from CH_KEYS so the
# chat round's own test keeps its exact list.
DEBUG_KEYS = {
    "strategy": "debug, debug action, debug move, debug rpg, debug spell, debug travel, debug threat, debug mount, "
                "debug grind, debug loot, debug log, debug logname, debug stuck, debug xp, debug equip",
    "action": "debug, cdebug, log, set value",
    "trigger": "debug, cdebug, log, set value",
    "value": "log level",
}
# Riding (#152, forever/docs/MOUNT-RIDE-NOTES.md): the mount state action, the mount list and the mount
# speed values, and the travel-target state the action reads to mount on a long walk. Left unregistered on
# purpose (each one does less): `enemy player target` and `has enemy player targets` (EnemyPlayerValue.cpp is
# not in the slice, so the dismount decision sees the dps target only), `possible rpg targets`
# (PossibleRpgTargetsValue.cpp is not in the slice, so the speculative safe-place mount never fires), and the
# purchase values `mount skilltype`, `can train mount`, `can buy mount`, `available mount vendors`,
# `full mount list` (the buy/train package registers them).
MOUNT_KEYS = {
    "strategy": "mount",
    "action": "check mount state",
    "trigger": "check mount state",
    "value": "mount list, current mount speed, max mount speed, travel target traveling, travel target working",
}
# Travel chooser and walker: the travel strategy rows, the actions that pick a destination and walk
# to it, and the values they read. `travel` joins the free kit (ai/playerbot/policy/world/FreeKitPolicy.h), so a
# free bot picks a destination and walks there; the rpg keys that would sell on arrival belong to #147. Left out
# on purpose: `has focus travel target` (only the rpg-quest row reads it, and that row cannot fire without
# `rpg quest`), `travel target traveling`/`travel target working` (registered with the mount keys).
TRAVEL_KEYS = {
    "strategy": "travel, travel once, explore",
    "action": "travel, choose travel target, choose group travel target, refresh travel target, request travel target, "
              "request named travel target, request quest travel target, reset travel target, move to travel target",
    "trigger": "",
    "value": "travel target active, travel target ready, no active travel destinations, need travel purpose, "
             "should travel named, should leave outgrown zone, vendor trip needed, not",
}
# Stuck rescue: the 5-minute move reset and the 15-minute hearthstone -> repop -> teleport chain, the combat-stuck
# give-up and the four triggers that fire them (MaintenanceStrategy, ClassStrategy, CombatStrategy, DeadStrategy). `repop` is
# the DeadStrategy action for the same trigger; `travel target active` and `no active travel destinations` are the travel-target
# keep/retire state UnstuckAction reads and writes (WP-2's TRAVEL_KEYS also name them; _join registers each once). Values:
# `distance moved since` and `time since last change` (S209) time the triggers, `money` is their earning test, `home bind` is
# the rescue's own location. Left unregistered: `falling far` (the host's ghost rescue covers it), `self resurrect`, `spirit
# healer`, `find corpse`, `revive from corpse` (their strategies' other rows do less on this core).
STUCK_KEYS = {
    "strategy": "",
    "action": "unstuck, reset, hearthstone, repop",
    "trigger": "move stuck, move long stuck, combat stuck, combat long stuck",
    "value": "distance moved since, money, home bind, travel target active, no active travel destinations",
}
# Free-bot life (TBF #147, packages A/B/C + this wiring package): the rpg brain that picks an NPC and runs its verbs,
# the quest values the travel quest rows and the rpg quest verbs read, and the town verbs (smart destroy, the 50-yard
# nearby service, the vendor/repair/mail values). `rpg` is not in the free kit array: the factory adds it on top when
# AiPlayerbot.AutoDoQuests is on, as the full build does. Left unregistered on purpose (each row then does less; a
# missing trigger reads as null in RpgAction's next-action search): `rpg camp`, `rpg heal`, `rpg gossip talk`,
# `rpg repeat quest`, `rpg taxi`, `rpg discover`, `rpg home bind`, `rpg buy petition`, `rpg queue bg`, `rpg trade useful`,
# `rpg enchant`, `rpg duel`, `rpg spell`, `rpg craft`, `rpg item`, `rpg guild`, `castnc`, `random jump`, `destroy all gray`
# (no build has a creator for it) and the shared-context values (`vendor map`, `item vendor list`, `entry quest relation`,
# `quest guidp map`, `quest givers`, `trainable spell map`), which have no LOCAL mapping below.
LIFE_RPG_KEYS = {
    "strategy": "rpg, rpg quest, rpg vendor, rpg explore, rpg maintenance",
    "action": "rpg, choose rpg target, move to rpg target, service nearby npc, rpg stay, rpg work, rpg emote, "
              "rpg cancel, rpg use, rpg start quest, rpg end quest, rpg sell, rpg buy, rpg repair, rpg train",
    "trigger": "no rpg target, far from rpg target, rpg, rpg wander, rpg start quest, rpg end quest, rpg sell, "
               "rpg buy, rpg repair, rpg train, rpg use, rpg quest use",
    "value": "possible rpg targets, next rpg action, can fight equal",
}
# The stock quest values (package B): dialog status for a giver/taker, the log slots, the objective rows and the two
# item-on-quest answers. The `active quest givers/takers/objectives` spawn-index values stay unregistered (TB registers
# them nowhere either) and the shared quest maps cannot be generated.
# `following party` is the group answer every quest travel gate ANDs in before its own verb (TravelMgr.cpp,
# QuestRelationTravelDestination::IsActive and QuestObjectiveTravelDestination::IsActive read
# "group or" of "following party,can accept/turn in quest npc::<entry>" and "following party,need quest objective::<q>,<o>").
# The value answers through the bot's own party, so a free bot with no master is its own authority (PartyFollowPolicy.h);
# the key itself must exist, or the gate reads the inert false and every quest destination is inactive - no bot walks to
# a giver, a taker or an objective, and the quest search parks with "rejected[inactive=...]".
LIFE_QUEST_KEYS = {
    "strategy": "",
    "action": "",
    "trigger": "",
    "value": "following party, free quest log slots, has rewardable finished quest, dialog status, dialog status quest, "
             "can accept quest npc, can accept quest low level npc, need quest reward, can turn in quest npc, "
             "can repeat quest npc, need quest objective, can use item on, need for quest, has nearby quest taker",
}
# Town verbs (package C): the bag-pressure destroy action, the quest-log-near-full trigger whose upkeep rows are inert
# for host bots (IsRandomBot is false), and the vendor/repair/mail/nearby-service values.
LIFE_TOWN_KEYS = {
    "strategy": "",
    "action": "smart destroy item",
    "trigger": "quest log nearly full",
    "value": "can sell, can buy, can repair, should repair, should sell, can get mail, vendor has useful item, "
             "nearby service target, should service nearby npc, nearby service fail parks",
}
# TBF #156 (guild participation): the owner's `speak` chat word. Its action is the legacy SayAction, whose `/g` branch
# calls PlayerbotAI::SayToGuild, now backed by the real guild speak right (forever/src/compat/TbCompat_P.h). The trigger
# is the ordinary chat command trigger every chat word uses. SayAction.cpp is on the build list (forever/SLICE-W4.txt).
GUILD_KEYS = {
    "strategy": "",
    "action": "speak",
    "trigger": "speak",
    "value": "",
}
# TBF #158 (social): greeting. `new player nearby` -> `greet`; the strategy's own emote and mount-animation rows are the
# separate EMOTE_KEYS group below. `already seen players` is the encounter memory GreetAction reads and writes.
# GreetAction.cpp and EmoteStrategy.cpp are on the build list (forever/SLICE-W4.txt).
SOCIAL_KEYS = {
    "strategy": "emote",
    "action": "greet",
    "trigger": "new player nearby",
    "value": "already seen players",
}
# Round 3 of the unregistered-key audit: the item-use rows the maintenance strategy and the class AoE kits name. The
# maintenance rows are gated to masterless bots inside the actions, as TB's factory only adds the strategy without a real
# player master. `open random item` stays out: opening a bag item is unported on Forever (OpenItemCompat), so it could not work.
R3A_KEYS = {
    "strategy": "",
    "action": "use random recipe, use random quest item, throw grenade, oil of immolation",
    "trigger": "",
    "value": "",
}
# TBF #209 round 3 (emote): the two rows of the emote strategy that need no packet. A masterless bot's `random` row plays
# one emote at a real player within AiPlayerbot.TooCloseDistance (EmoteAction::GetTarget skips bots, so an audience of bots
# means no emote) and `last emote` keeps TB's 5-25 second cooldown between them; a `random` row plays the mount-special
# animation of a mounted, still bot. A commanded bot keeps TB's quiet gate (it gets neither the emote nor the talk row).
# Left out on purpose: `seldom` -> `talk` (idle chatter) and
# `receive text emote` / `receive emote` -> `emote`. The two receive triggers exist in the full build, but the Forever
# slice registers no legacy packet handler table (RegisterPacketHandlersCompat) and polls only the three loot and quest
# events (PollPacketEventCompat), so no emote packet ever reaches a trigger and the rows could not fire. `talk target` is
# the book the compiled TalkAction writes; registered so that action never reads a missing ObjectGuid, even though no row
# creates it.
EMOTE_KEYS = {
    "strategy": "",
    "action": "emote, mount anim",
    "trigger": "",
    "value": "last emote, talk target",
}
# Duels: TB's duel strategy (strategy/generic/DuelStrategy.cpp, strategy/generic/DuelStrategy.h): the bot takes
# its duel opponent as the target (`attack duel opponent` reads `duel target`, registered since slice 1) and the class kit
# rotates on it. `duel requested` is registered so the row exists, but the Forever bot session receives no duel packet
# (UnportedFeatures.h), so it never fires; the host answers the core's Player::duel state instead and `accept duel` is
# therefore not registered. No value is new: DuelTargetValue.cpp is already on the build list.
DUEL_KEYS = {
    "strategy": "duel",
    "action": "attack duel opponent",
    "trigger": "no attackers, duel requested",
    "value": "",
}
# Open-world PvP seek (TBF #163 P3): TB's `pvp` strategy for every bot and `world pvp` for the free bots the host gives the
# seek role (forever/src/host/TbHostWorldPvp.cpp). `enemy player near` fires `attack enemy player`, which reads the three
# enemy-player values; outside a battleground EnemyPlayersValue only scans the proximity list for a bot holding `world pvp`
# and only takes a PvP-flagged player (WorldPvpSeekChecks). No value is new: EnemyPlayerValue.cpp is on the build list.
WPVP_KEYS = {
    "strategy": "pvp, world pvp",
    "action": "attack enemy player",
    "trigger": "enemy player near",
    "value": "enemy player target, enemy player targets, has enemy player targets",
}
# Round BG2 (BATTLEGROUNDS-SPEC.md D9, forever/docs/BG2-NOTES.md): the keys of the PvP fight kit that round WPVP did not already register.
# `battleground` is the strategy PlayerbotAI::UpdateAIGroupMaster looks for in the non-combat engine of a bot inside a match, and the
# flag carrier values are what BG3's objective play will read (`pvp`, `attack enemy player`, `enemy player near` and the enemy player
# values came first with the open-world PvP seek port). Left unregistered on purpose: the triggers and actions of the objective play
# (BG3) and "attack enemy flag carrier" (the carrier is the first rule of the target order).
BG2_KEYS = {
    "strategy": "battleground",
    "action": "",
    "trigger": "",
    "value": "enemy flag carrier, team flag carrier",
}
BOSS5_KEYS = {
    "strategy": "dungeon, boss tactics",
    "action": "enable boss tactics strategy, disable boss tactics strategy, boss tactics interrupt, boss tactics kill first, boss tactics step behind, boss tactics avoid cast, boss tactics stand",
    "trigger": "enter boss tactics, leave boss tactics, boss tactics interrupt, boss tactics kill first, boss tactics front, boss tactics avoid cast, boss tactics stand",
    "value": "",
}
KEYS = _join(SLICE_KEYS, SLICE2_KEYS, SLICE3_KEYS, SLICE4_KEYS, SLICE8_KEYS, SLICE10_KEYS, SLICE11_KEYS, SLICE9_KEYS, W1_KEYS, W6_KEYS, W2_KEYS, W3_KEYS, W8_KEYS, CH_KEYS, PC_CHAT_KEYS, PC_CHAT_B_KEYS, WL0_KEYS, REACTIONS_KEYS, COMBAT_ITEMS_KEYS, CLASS_EXTRAS_KEYS, STORE_KEYS, BEHAVIOR_KEYS, STRAT_STORE_KEYS, PRESET_KEYS, S209_KEYS, PULL_KEYS, COLLISION_KEYS, SIT_KEYS, JUMP_KEYS, DEBUG_KEYS, MOUNT_KEYS, TRAVEL_KEYS, STUCK_KEYS, LIFE_RPG_KEYS, LIFE_QUEST_KEYS, LIFE_TOWN_KEYS, GUILD_KEYS, SOCIAL_KEYS, R3A_KEYS, EMOTE_KEYS, DUEL_KEYS, WPVP_KEYS, BG2_KEYS, BOSS5_KEYS)

# Local context class names and the full-build context class each one copies the shape of (order = registration order).
KIND_ORDER = ("strategy", "action", "trigger", "value")
BASE_T = {"strategy": "Strategy", "action": "Action", "trigger": "Trigger", "value": "UntypedValue"}
LOCAL = {"StrategyContext": "SliceStrategyContext", "MovementStrategyContext": "SliceMovementStrategyContext",
         "AssistStrategyContext": "SliceAssistStrategyContext", "QuestStrategyContext": "SliceQuestStrategyContext",
         "ActionContext": "SliceActionContext",
         "ChatActionContext": "SliceChatActionContext", "WorldPacketActionContext": "SliceWorldPacketActionContext",
         "TriggerContext": "SliceTriggerContext", "ChatTriggerContext": "SliceChatTriggerContext",
         "WorldPacketTriggerContext": "SliceWorldPacketTriggerContext",
         "ValueContext": "SliceValueContext"}


# Headers nothing includes (forward-ported from the donor module, never wired in): the class they define is the same name as a live one in
# another header, so the class finder must not see them. strategy/triggers/WaitForAttackTriggers.h repeats WaitForAttackSafeDistanceTrigger
# of RangeTriggers.h (grep over ai/: no #include of it; slice 3).
DONOR_ONLY_HEADERS = {"strategy/triggers/WaitForAttackTriggers.h"}


def find_class_headers():
    """class name -> list of headers (paths relative to ai/playerbot) that define it."""
    defs = {}
    pat = re.compile(r'^\s*(?:class|struct)\s+(\w+)\b(?![^{;]*;)[^;{]*\{|^\s*(?:class|struct)\s+(\w+)\s*(?:final\s*)?:[^;{]*$', re.M)
    base = os.path.join(ROOT, "ai", "playerbot")
    for d, _, names in os.walk(base):
        for n in names:
            if not n.endswith(".h"):
                continue
            p = os.path.join(d, n)
            if os.path.relpath(p, base).replace(os.sep, "/") in DONOR_ONLY_HEADERS:
                continue
            text = chk.strip_comments(chk.read(p))
            for m in re.finditer(r'^[ \t]*(?:class|struct)\s+(\w+)\b([^;{]*)(\{|$)', text, re.M):
                if m.group(3) == "{" or ":" in m.group(2):
                    defs.setdefault(m.group(1), set()).add(os.path.relpath(p, base))
            for m in re.finditer(r'^[ \t]*(?:BEGIN_TRIGGER|BUFF_ACTION(?:_U)?|HEAL_ACTION)\(\s*(\w+)\s*,', text, re.M):
                defs.setdefault(m.group(1), set()).add(os.path.relpath(p, base))
    return defs


def classes_of(expr):
    return re.findall(r'\bnew\s+(\w+)', expr)


def main(argv):
    full = chk.full_registry()
    entries = {k: [] for k in KIND_ORDER}
    for kind in KIND_ORDER:
        for key in KEYS[kind]:
            e = full[kind].get(key)
            if e is None:
                sys.exit("slice_gen_context: %s key '%s' is not a key of the full build" % (kind, key))
            entries[kind].append(e)

    defs = find_class_headers()
    headers, problems, used = set(), [], []
    for kind in KIND_ORDER:
        for e in entries[kind]:
            for c in classes_of(e["expr"]):
                used.append((c, kind, e["key"]))
                hs = defs.get(c, set())
                if not hs:
                    problems.append("no header defines class %s (%s '%s')" % (c, kind, e["key"]))
                elif len(hs) > 1:
                    problems.append("class %s is defined in several headers: %s" % (c, ", ".join(sorted(hs))))
                else:
                    headers.add(next(iter(hs)))
    if problems:
        print("\n".join(problems))
        sys.exit("slice_gen_context: cannot place every class")

    # group the entries by their full-build context class, keeping each class's flags
    groups = {}
    order = []
    for kind in KIND_ORDER:
        for e in entries[kind]:
            if e["cls"] not in groups:
                groups[e["cls"]] = (kind, e["flags"], [])
                order.append(e["cls"])
            groups[e["cls"]][2].append(e)
    for cls in order:
        if cls not in LOCAL:
            sys.exit("slice_gen_context: unknown full-build context class %s" % cls)
    # registration order of the full build (AiObjectContext.cpp:21-35): StrategyContext, Movement, Assist, ..., then the rest
    order.sort(key=lambda c: list(LOCAL).index(c))

    L = []
    w = L.append
    w("// GENERATED by forever/tools/slice_gen_context.py: do not edit by hand, change the key lists there and rerun it.")
    w("// forever/tools/slice_registry_check.py proves every entry below is a key of the full build with the same creator.")
    w("//")
    w("// Slice 1 (FOREVER-SLICE1-SPEC section 4): the AiObjectContext constructor of the slice build. The full build's")
    w("// constructor (strategy/AiObjectContextRegistry.cpp, strategy/AiObjectContext.cpp before the R1 split) adds ~1,800")
    w("// creators from eight context headers; the slice registers only the keys the warrior's follow / assist / melee")
    w("// kit uses. Each creator is the text of the full header's, inside a context class of the same shape. The warrior")
    w("// entries are not here: WarriorAiObjectContext (compiled as it is) adds them on top of this constructor.")
    w("#include \"compat/botpch_forever.h\"")
    w("#include \"playerbot/playerbot.h\"")
    w("#include \"SliceLog.h\"")
    w("#include \"playerbot/strategy/AiObjectContext.h\"")
    w("#include \"playerbot/strategy/NamedObjectContext.h\"")
    for h in sorted(headers):
        w("#include \"playerbot/%s\"" % h.replace(os.sep, "/"))
    w("")
    w("using namespace ai;")
    w("")
    w("namespace ai")
    w("{")
    w("namespace slice")
    w("{")
    for cls in order:
        kind, flags, es = groups[cls]
        t = BASE_T[kind]
        w("    // the registry of %s (%d of its keys)" % (cls, len(es)))
        w("    class %s : public NamedObjectContext<%s>" % (LOCAL[cls], t))
        w("    {")
        w("    public:")
        init = " : NamedObjectContext<%s>(%s, %s)" % (t, flags[0], flags[1]) if flags != ("false", "false") else ""
        w("        %s()%s" % (LOCAL[cls], init))
        w("        {")
        for e in es:
            w("            creators[\"%s\"] = %s;" % (e["key"], e["expr"]))
        w("        }")
        w("    };")
        w("")
    w("    // Counted when the constructor runs; SliceRegisteredCounts() reports them for the one-line boot log.")
    w("    constexpr unsigned kStrategies = %d, kActions = %d, kTriggers = %d, kValues = %d;"
      % tuple(len(entries[k]) for k in KIND_ORDER))
    w("}")
    w("}")
    w("")
    w("// The slice's constructor. Same signature as the full build's: only the registry differs.")
    w("AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)")
    w("{")
    for kind in KIND_ORDER:
        member = {"strategy": "strategyContexts", "action": "actionContexts", "trigger": "triggerContexts",
                  "value": "valueContexts"}[kind]
        for cls in order:
            if groups[cls][0] == kind:
                w("    %s.Add(new slice::%s());" % (member, LOCAL[cls]))
    w("")
    w("    static bool logged = false;")
    w("    if (!logged)")
    w("    {")
    w("        logged = true;")
    w("        slice::LogInfo(\"slice: registered \" + std::to_string(slice::kStrategies) + \"/\" + std::to_string(slice::kActions) + \"/\"")
    w("            + std::to_string(slice::kTriggers) + \"/\" + std::to_string(slice::kValues) + \" (strategies/actions/triggers/values)\");")
    w("    }")
    w("}")
    text = "\n".join(L) + "\n"

    if "--report" in argv:
        inslice = set(l.strip() for l in chk.read(chk.SLICE_LIST).splitlines() if l.strip() and not l.startswith("#"))
        cpp_defs = {}
        for d, _, names in os.walk(os.path.join(ROOT, "ai")):
            for n in names:
                if n.endswith(".cpp"):
                    p = os.path.join(d, n)
                    cpp_defs[os.path.relpath(p, ROOT)] = chk.strip_comments(chk.read(p))
        print("registered classes with out-of-line members in a .cpp that is NOT in forever/SLICE1.txt:")
        seen, nout = set(), 0
        for c, kind, key in used:
            if c in seen:
                continue
            seen.add(c)
            where = [f for f, t in cpp_defs.items() if re.search(r'\b%s::~?\w+\s*\(' % re.escape(c), t)]
            miss = [f for f in where if f not in inslice]
            if where and not any(f in inslice for f in where):
                print("  %s (%s '%s') defined in %s" % (c, kind, key, ", ".join(where)))
                nout += 1
        print("  (%d classes checked, %d with no defining .cpp in the slice list)" % (len(seen), nout))
    if "--check" in argv:
        cur = chk.read(OUT) if os.path.exists(OUT) else ""
        if cur != text:
            print("SliceAiObjectContext.cpp is out of date: rerun forever/tools/slice_gen_context.py")
            return 1
        print("SliceAiObjectContext.cpp is up to date")
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s: %s" % (os.path.relpath(OUT, ROOT),
                            ", ".join("%d %s" % (len(entries[k]), {"strategy": "strategies"}.get(k, k + "s")) for k in KIND_ORDER)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
