# Features that do not work on ForeverCore yet

The closed list of bot-brain features that are not ported to ForeverCore. The list itself is the table in
`ai/playerbot/UnportedFeatures.h`: one row per feature. This page lists the same rows. On ForeverCore the helpers
(`forever/src/compat/TbCompat_Unported.h`) call `tbcompat::Unported(Feature::X)`, which counts the call, logs the first one per
name (`TortoiseBots: not ported yet: <name>`) and returns a failure value: "the action did not happen", never an invented value.
On Turtle (`ai/cmangos-compat-unported.h`) every helper is the original call. A slice that ports a feature deletes its row,
its enum entry and its helpers.

`.tb unported` on a running server prints which features the bots actually reached and how often. The player-facing view
of what is missing is [Known limitations](../../docs/play/known-limitations.md).

## Still answered "not ported yet" (49)

| Feature | What it is |
|---|---|
| packet handler tables | the legacy opcode-to-handler tables (1.12 opcode names); ForeverCore's bot session neither sends nor receives these packets, so nothing is registered |
| item random properties | the random property / suffix of an item (1.12 ItemRandomProperties.dbc); ForeverCore uses item bonus lists |
| bank and keyring slots | the 1.12 keyring slots and bank item slots; ForeverCore has neither (keys live in bags, the bank is bank bags) |
| loot objects | the loot of a creature or game object as a public member (1.12 Creature::loot); ForeverCore keeps loot per looting player |
| equip slot of an item prototype | Player::FindEquipSlot on a prototype; ForeverCore asks about an Item (W6b gear generation uses GearSetPolicy::InventoryTypeFitsSlot) |
| companions | spells with the summon critter effect (1.12 SPELL_EFFECT_SUMMON_CRITTER); ForeverCore has no such effect |
| incoming: taxi | activate taxi and taxi node packets |
| incoming: group and party | group invite, uninvite, set leader, party command, ready check, summon request and duel request packets |
| incoming: trade and item errors | trade status, buy errors, item push, inventory change failure, master use item packets |
| incoming: battleground status | the battleground status packet |
| incoming: guild and petition | guild invite and petition signature packets |
| incoming: experience and honor | level up, xp gain and honor gain packets |
| incoming: emotes | text emote and emote packets |
| incoming: gossip, game object and area trigger | gossip hello, game object use and area trigger packets |
| world update time average | sWorld.GetAverageDiff(): the average world update time, a host value (R4M request 5); ForeverCore keeps no such average |
| talents | talent points and trees (1.12 TalentTab.dbc, Player::GetFreeTalentPoints, GetBonusTalentCount); ForeverCore has no equivalent accessors in the pre-included set |
| guid from spawn data | the loaded unit or game object of a spawn record, and a game object's spawn guid (1.12 layout); ForeverCore generates a loaded object's low guid per map, so a spawn-record guid is not a live lookup key |
| wire form of a GuidPosition | writing a GuidPosition into a packet buffer (1.12 layout) |
| battleground queue | battleground template id from a queue type id (BattleGroundMgr::BGTemplateId); ForeverCore's queue type id is a struct and the mapping is not ported |
| quest link: waits for D3 | extracting a quest id from a chat link (1.12 ChatHandler helper ExtractKeyFromLink); its link branch waits for D3, the spell and item halves are real since S1-R1 |
| game object use | using a game object by the use packet (lightwell, party portal, ritual objects); ForeverCore uses typed packets and a different interact check |
| item and totem casts | casting an item spell with no owned Item, the item cheat's hand-built Spell (a poison on a weapon, a potion the bot does not carry) and the lock check that reads Spell internals; an owned item's on-use spell casts through the core's Player::CastItemUseSpell |
| skills | skill lines ForeverCore does not name (thrown) and skill writes (SetSkill with the 1.12 argument list) |
| taxi | flight masters and taxi paths (Player::GetTaxi, TaxiPathEntry/TaxiNodesEntry fields, ActivateTaxiPathTo, CleanupFlagsOnTaxiPathFinished); ForeverCore's taxi model is different |
| transports | boarding, leaving and riding transports (MoveOnTransport, MoveOffTransport, UseTransport, FollowOnTransport, AddPassenger, RemovePassenger, transport movement info); ForeverCore's passenger model and spline transform differ |
| navmesh and path internals | the navmesh-island unstick probe over MMapFactory / PathInfo::FindWalkPoly, and the 1.12 path type and nav flag bits (PATHFIND_*, NAV_*); ForeverCore's MMapManager and path flags differ |
| cinematic and camera | cinematic state (watching_cinematic_entry, CinematicEnd); ForeverCore keeps a cinematic manager object |
| camp, rest bonus, water breathing | the camp kit skill (SKILL_SURVIVAL2), rest bonus (SetRestBonus), the water breathing interval |
| movement heartbeat | Player::SendHeartBeat, the client heartbeat a real-player-controlled bot sends; a headless host bot never needs it. The movement generator interrupt is real: the near-teleport ack calls the core's MotionMaster::InterruptOnTeleport |
| chat channels, addon bridge, chat tags | addon messages (SendAddonMessage, BuildChatPacket with a chat tag) and chat channels (ChannelMgr by team); ForeverCore chat is typed packets |
| guild | guild rank rights and member slots (Guild::GetRanksSize, GetMemberSlot, HasRankRight); ForeverCore has Guild::Member and rank ids |
| spawn id of a creature | the spawn record of a creature or game object found by the guid counter (T GetCreatureDataPair(guid.GetCounter()), GetGODataPair); ForeverCore's guid counter is not a spawn id (findings item 20) |
| creature template level | CreatureInfo::level_max (T); ForeverCore templates scale levels per difficulty |
| pools and game events | the pool and game event of a creature or game object found by its guid counter (PoolMgr::IsPartOfAPool, GameEventMgr::GetGameEventId); ForeverCore keys both by spawn id |
| spell data (icon ids) | SpellEntry::SpellIconID on the mirror spell entry (paladin heal classification, icon comparison); ForeverCore keeps IconFileDataId, not the 1.12 icon index. The item charges are real: ItemSpellChargesCompat maps the legacy slot index to the item's ItemEffectEntry |
| boss and raid tactics | boss-specific hold, taunt, heal, position and spawn rules (BossTacticsActions, Fankriss, Patchwerk, Viscidus, Solnius, meet-and-heal holds, boss stands, boss tactics and dungeon plan tables); the link stand-ins answer: no boss rule applies (R6B) |
| random item tables | RandomItemMgr food picks, reagent and vendor tables; gear generation is handled by W6b TbGearGen + GearSetPolicy |
| bot texts and help | PlayerbotTextMgr and PlayerbotHelpMgr (ai_playerbot_texts, help topics); the text lookups answer the text name itself (R6B) |
| talent spec tables | TalentSpec and ClassSpecs, the premade talent specs and their checks; no spec is read (R6B) |
| pool and hire services | the random-bot pool tables (RandomBotFacade caches, RandomBotAccountRegistry, AhBotSellerAccount), HireLifecycle, ClaimLifecycle, BotGearService (pinned cap), AltBotService (autonomous alts), the activity leases and the guild order store (TortoiseBots::GuildOrders); answers: not a pool bot, not hired, not claimed, no pinned cap, not autonomous, no order (R6B) |
| bot factory (ammo, pets) | PlayerbotFactory ammo and hunter pet set-up and pet training; nothing is created (R6B) |
| chat filter chain | CompositeChatFilter, the filters that rewrite a command line before the bot reads it; the line is used as it comes (R6B) |
| camp spots table | CampMgr, the table of camp spots (ai_playerbot_camp) and the active camp list; empty (R6B) |
| other legacy services | small out-of-slice helpers the slice links against: mount spell test, cheat mask parsing, emote variants, grind instance filter, meeting stone spot search, fishing session end, area trigger run, db store load, cast cancel log (R6B) |
| loot: bot-only drop boost | the extra roll of green and blue drops for random (pool) bots (ApplyBotLootBonus); a host bot is never a random bot. Item loot itself (the window, the take through the autostore handler, quest and free-for-all copies) and the group-loot roll are real since slice 9 |
| loot tables: shared drop maps | the drop maps built from the loot templates through the shared value context (entry loot list, item drop list, loot chance); the shared context is not built on ForeverCore. The templates themselves (creature, game object, reference, item) are copied from the world database since slice 9 and read by the chest check |
| loot: game objects, pickpocketing and disenchant loot | looting a game object (herb or ore node, goober) through the bot brain's own open path, pickpocketing and disenchant loot, and the loot templates of those types; chests (slice 9), the host herb/ore errand (W7), a fishing bobber (host errand) and skinning a looted corpse are looted |
| item use on a game object, openable items and a cast with no bot session | using an item on a game object, opening a bag item that holds loot (CMSG_OPEN_ITEM), an item whose only effect the core will not cast on use (a LEGACY item), and a use from a session that is not a bot. An owned item used from the bags (potions, healthstones, mana consumables, bandages, scrolls, elixirs, flasks, food buffs, weapon stones, poison on a weapon) casts through the core's own Player::CastItemUseSpell (slice 2, I2; the item families on ForeverCore) |
| battleground release aura | the Waiting to Resurrect aura (spell 2584) a bot casts on itself when it releases inside a battleground (ReleaseSpiritAction); on ForeverCore the core puts it on a player itself when the spirit guide is chosen (Player::SetAreaSpiritHealer), and the host queues a dead bot at the nearest area spirit healer (BG1, forever/src/host/bg/TbBgQueue.cpp), so the helper stays a no-op and nothing is cast by the engine |

## In the table but no longer called (3)

These rows are still in `UnportedFeatures.h`, but no code calls them any more: the feature was ported or its code left
the build. They can be deleted from the table.

| Feature | What it was |
|---|---|
| item display id | ItemPrototype::DisplayInfoID, used to find consumables that look the same; the mirror prototype has no display id |
| travel and exploration | TravelMgr, the travel node map, paths, travel targets and destinations, area levels, quest travel table, taxi node zones; nothing is loaded and every query answers: no route, no target (R6B) |
| trace and observability services | WhyTraceService, RunTraceService and ObservabilityEmitter are switched off on ForeverCore, every hook a no-op (R6B); the combat monitor is ported (host/cmd/TbCmdCombat.cpp) |
