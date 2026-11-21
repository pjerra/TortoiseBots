// ForeverCore side of the both-core helpers of round S1-R2 (chat commands in). Turtle side: ai/cmangos-compat-ch.h (the code the bot always
// ran). Found through ai/playerbot/TbCompatCH.h. Notes: forever/docs/S1-R2-NOTES.md.
//
// Three of the legacy files this round compiles name 1.12 numbers and fields the pinned core does not have. Each helper answers with the
// core's own counterpart, never a guess; where the core has no table (the help topics) the answer is the smaller, truthful one.
#pragma once

#include "DB2Structure.h"
#include "Group.h"
#include "Item.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "RestMgr.h"

#include "playerbot/strategy/generic/ChatCommandHandlerStrategy.h"

#include <list>
#include <set>
#include <string>

class Group;

// The group's raid icon. ForeverCore's Group::SetTargetIcon takes the guid of whoever changed the mark (Group.h:364: the packet's ChangedBy,
// so a client can skip its own echo, Group.cpp:820-837); 1.12's took only the slot and the unit. A bot has no client, so it names itself.
inline void SetTargetIconCompat(Group* group, int index, ObjectGuid unit, ObjectGuid changedBy)
{
    group->SetTargetIcon(uint8(index), unit, changedBy);
}

// The experience a rested bot still has. T update field PLAYER_REST_STATE_EXPERIENCE, F the rest manager's own number (RestMgr.h:72).
inline uint32 RestXPCompat(Player const* player) { return uint32(player->GetRestMgr().GetRestBonus(REST_TYPE_XP)); }

// What repairing one item costs, as the core itself computes it (Item.cpp:1438-1470: lost durability, the subclass multiplier, the quality
// modifier and the item's repair multiplier). 1.12's estimate (what the bot made before the move) is the Turtle side.
inline uint32 ItemRepairCostCompat(Item const* item)
{
    return item ? uint32(item->CalculateDurabilityRepairCost(1.0f)) : 0;
}

// The skill levels a profession spell is learned at: min_value was the rank it turns learnable (yellow), max_value the rank it turns trivial
// (grey). ForeverCore names them MinSkillLineRank and TrivialSkillLineRankHigh (DB2Structure.h:3715-3733; the core's own skill-up math reads
// them as gray/green/yellow at Player.cpp:5996-5999); SkillLineAbilitySkillCompat/SpellCompat (TbCompat_S9.h) answer the line and the spell.
inline uint32 SkillLineAbilityMinCompat(SkillLineAbilityEntry const* ability) { return uint32(ability->MinSkillLineRank); }
inline uint32 SkillLineAbilityMaxCompat(SkillLineAbilityEntry const* ability) { return uint32(ability->TrivialSkillLineRankHigh); }

// The commands a player may whisper at a bot, for `help`'s "Whisper any of:" line. The full ChatActionContext header cannot compile on this
// core (its guild actions name 1.12 guild opcodes and Guild member slots), so the list is the chat strategy's own supported commands: the
// commands this build's chat engine answers. Turtle keeps the wider context list (ai/cmangos-compat-ch.h).
inline std::set<std::string> ChatCommandKeys(PlayerbotAI* ai)
{
    ai::ChatCommandHandlerStrategy strategy(ai);
    std::list<std::string> const& commands = strategy.GetSupported();
    return std::set<std::string>(commands.begin(), commands.end());
}
