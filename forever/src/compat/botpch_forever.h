// The ForeverCore counterpart of ai/botpch.h.
//
// The bot brain (ai/, runtime/, commands/) is written against cmangos names and expects one header in front of every
// file: on the Turtle core that is ai/botpch.h (core headers, then ai/cmangos-compat-shim.h). On ForeverCore it is this
// file: the TrinityCore headers for the same things, then forever-compat-shim.h, which gives the old names a meaning
// here. The build puts it in front of every legacy source file (see the root CMakeLists.txt and port_census.py).
//
// Order matters: EVERY core header the bot brain uses is included here, before the shim, because the shim renames
// things with macros and a core header read after it would be renamed too.
#pragma once

#include "Common.h"
#include "Define.h"

#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "ByteBuffer.h"
#include "Cell.h"
#include "CellImpl.h"
#include "ChannelMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Corpse.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureData.h"
#include "CreatureGroups.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "DBCEnums.h"
#include "DatabaseEnv.h"
#include "DynamicObject.h"
#include "Formulas.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GridDefines.h"
#include "GridMap.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceScript.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "LFGMgr.h"
#include "Language.h"
#include "Log.h"
#include "LootMgr.h"
#include "Mail.h"
#include "Map.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "MovementGenerator.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Pet.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SocialMgr.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "Totem.h"
#include "Transport.h"
#include "Unit.h"
#include "Util.h"
#include "VMapFactory.h"
#include "WaypointManager.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

// STL headers (the same set ai/botpch.h gives)
#include <algorithm>
#include <functional>
#include <iomanip>
#include <memory>
#include <numeric>
#include <regex>
#include <stack>

#include "forever-compat-shim.h"

// Playerbot AI framework: the same tail as ai/botpch.h
#include "playerbot/PlayerbotAIAware.h"
#include "playerbot/BotState.h"
#include "runtime/PlayerbotAIStorage.h"
