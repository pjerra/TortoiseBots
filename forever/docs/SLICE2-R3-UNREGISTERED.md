# Slice 2: keys the slice sources name and the slice does not register (R3, decision D6)

Generated from `python3 forever/tools/slice_registry_check.py` (part (b)) at the final commit of the R3 package; the reason column is assigned by name and by the file that names the key (the `default` strategy's packet keys are the bulk). The engine skips an unregistered strategy, trigger or action, and the missing-key guard (`SliceMissingKeyGuard.h`) logs it once as `slice: missing ...`; an unregistered value gets an inert stand-in of its type. The keys that appeared as `slice: missing` lines in the boots are listed with their groups in SLICE2-R3-NOTES.md section 9.5.

| Kind | Key | Reason |
|---|---|---|
| action | accept invitation | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | accept summon | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | accept trade | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | add gathering loot | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| action | add loot | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | apply oil | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | apply stone | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | area trigger | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | attack anything | feature outside the slice, not on the follow, heal, drink or money-loot path |
| action | attack enemy player | PvP and battleground (outside the slice) |
| action | auto learn spell | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | auto loot roll | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| action | auto talents | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | avoid aoe | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | be near | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| action | berserker rage fear | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| action | bop immune boss victim | boss and raid tactics (unported row `boss and raid tactics`) |
| action | check mail | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | check mount state | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | check values | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | destroy all gray | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| action | dismiss pet | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| action | dps aoe | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | equip upgrades | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | faction | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | finish ready check | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | fish | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | flee from area | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | flee with pet | pets (outside the slice) |
| action | free action potion | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | go | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | goblin sapper | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | guild accept | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | healing potion | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | healthstone | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | hearthstone | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | honor gain | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | immune boss victim escape | boss and raid tactics (unported row `boss and raid tactics`) |
| action | jump | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | log roll won | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | loot roll | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| action | mana potion | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | move out of collision | feature outside the slice, not on the follow, heal, drink or money-loot path |
| action | move random | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | move to fish | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | oil of immolation | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | petition sign | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | pool gear top up | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | pool money top up | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | pull action | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | pull end | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | pull my target | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | pull nearest target | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | pull start | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | query item usage | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | raid spread | boss and raid tactics (unported row `boss and raid tactics`) |
| action | reach area trigger | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | reach pull | feature outside the slice, not on the follow, heal, drink or money-loot path |
| action | ready check | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | ready reply | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | rear flank | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | release | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | release pull hold | boss and raid tactics (unported row `boss and raid tactics`) |
| action | remember taxi | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | remove blessing of salvation | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | remove greater blessing of salvation | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | reset ai soft | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | return to pull position | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | reveal gathering item | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| action | revive from corpse | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | set behind | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | set pet stance | pets (outside the slice) |
| action | smart destroy item | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| action | stay line | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| action | stoneshield potion | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | talk to quest giver | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | tank assist | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | tank face away | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| action | taxi | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | tell cannot equip | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | tell not enough money | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | tell not enough reputation | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | throw grenade | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | trainer | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | uninvite | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | unlock traded item | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | unstuck | debug, chat, talents, gear and avoidance features (outside the slice) |
| action | use fishing bobber | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | use lightwell | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| action | use meeting stone | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| action | use trinket | feature outside the slice, not on the follow, heal, drink or money-loot path |
| action | wait for attack keep safe distance | feature outside the slice, not on the follow, heal, drink or money-loot path |
| action | world buff | travel, mounts, fishing, quests and open-world features (outside the slice) |
| action | xp gain | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| strategy | aoe fear | boss and raid tactics (unported row `boss and raid tactics`) |
| strategy | attack tagged | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | auto cc | feature outside the slice, not on the follow, heal, drink or money-loot path |
| strategy | battleground | PvP and battleground (outside the slice) |
| strategy | behind | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | boss tactics | boss and raid tactics (unported row `boss and raid tactics`) |
| strategy | debug | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug action | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug log | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug logname | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug loot | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug move | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug rpg | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug spell | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | debug threat | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | dps feral | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| strategy | duel | PvP and battleground (outside the slice) |
| strategy | emote | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | focus heal targets | feature outside the slice, not on the follow, heal, drink or money-loot path |
| strategy | focus rti targets | feature outside the slice, not on the follow, heal, drink or money-loot path |
| strategy | follow chase | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| strategy | follow jump | travel, mounts, fishing, quests and open-world features (outside the slice) |
| strategy | guard | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | hold position | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | passive | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | patchwerk main | boss and raid tactics (unported row `boss and raid tactics`) |
| strategy | patchwerk soak | boss and raid tactics (unported row `boss and raid tactics`) |
| strategy | preheal | feature outside the slice, not on the follow, heal, drink or money-loot path |
| strategy | pull back | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | pvp | PvP and battleground (outside the slice) |
| strategy | silent | debug, chat, talents, gear and avoidance features (outside the slice) |
| strategy | solnius | boss and raid tactics (unported row `boss and raid tactics`) |
| strategy | stay | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | stealthed | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| strategy | tank assist | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | tank face | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | tank feral | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| strategy | tank shaman | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| strategy | threat | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | travel once | travel, mounts, fishing, quests and open-world features (outside the slice) |
| strategy | wait for attack | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| strategy | wander | travel, mounts, fishing, quests and open-world features (outside the slice) |
| strategy | world pvp | PvP and battleground (outside the slice) |
| trigger | activate taxi | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | aoe threat nearby | debug, chat, talents, gear and avoidance features (outside the slice) |
| trigger | area trigger | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | at war | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | cannot equip | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | check mount state | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | collision | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | combat long stuck | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | combat stuck | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | equipment audit | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | fear sleep sap | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | feared | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | force rebuff pending | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | gossip hello | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | group invite | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | group set leader | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | guild accept | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | has area debuff | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | has blessing of salvation | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| trigger | has greater blessing of salvation | consumable and item use other than food and drink (unported row `item use except food and drink`) |
| trigger | heal target full health | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | honorgain | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | immune boss victim | boss and raid tactics (unported row `boss and raid tactics`) |
| trigger | immune boss victim protect | boss and raid tactics (unported row `boss and raid tactics`) |
| trigger | item push result | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | levelup | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | loose adds near | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | loot roll | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| trigger | loot roll won | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | lose aggro | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | mounted | travel, mounts, fishing, quests and open-world features (outside the slice) |
| trigger | need world buff | travel, mounts, fishing, quests and open-world features (outside the slice) |
| trigger | new pet | pets (outside the slice) |
| trigger | not behind target | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | not dps aoe target active | debug, chat, talents, gear and avoidance features (outside the slice) |
| trigger | not enough money | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | not enough reputation | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | party member attacked | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | party member dead | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | petition offer | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | possible ads | named in the source but a key of no registry (dead name in the legacy code; nothing to register) |
| trigger | protect party member | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | pull anchor done | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| trigger | pull end | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| trigger | pull hold expired | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| trigger | pull start | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| trigger | ready check finished | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | rear flank needed | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | release spirit | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | resurrect | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | return to pull position | tank, pull and positioning kit of other roles/classes (the slice has one healer and one melee dps) |
| trigger | revive from corpse | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | should pull | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | spread needed | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | stunned | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | summon request | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | tank face needed | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | taxi done | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | trade status | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | trade status extended | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | uninvite | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | uninvite guid | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | use game object | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | val | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | wait for attack safe distance | feature outside the slice, not on the follow, heal, drink or money-loot path |
| trigger | within area trigger | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| trigger | xpgain | packet feature of the `default` strategy outside this slice (decision D6: invitations, trade, taxi, ready check, petitions, area triggers, xp/honor, summon, resurrection, mail, trainer, quest and item packets) |
| value | already seen players | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | can move around | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | current mount speed | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | dangerous elite | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | enemy flag carrier | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | enemy player target | PvP and battleground (outside the slice) |
| value | free money for | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | free quest log slots | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | future travel destinations | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | grind target | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | guild order | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | has enemy player targets | PvP and battleground (outside the slice) |
| value | has reagents for | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | incoming heals | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | item usage | loot rolls, gathering, destroying and item usage (decision D2: money only) |
| value | keepout set aside | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | money needed for | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | nearest non bot players | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | outfit list | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | party member without food | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | party member without item | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | party member without water | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | patchwerk opening window | boss and raid tactics (unported row `boss and raid tactics`) |
| value | raid tank needs healing | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | runners let go | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | skip go loot list | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | travel target active | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | travel target traveling | travel, mounts, fishing, quests and open-world features (outside the slice) |
| value | unreachable entries | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
| value | unreachable targets | value read only by a key above or by a feature outside the slice (the missing-value stand-in answers its type's empty value) |
