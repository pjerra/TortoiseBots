#pragma once

#include <algorithm>
#include <array>
#include <type_traits>
#include <cstdint>
#include <vector>

// Which links of the travel node graph count as a route (W4 review fixes, decisions 1 and 2 of the W4 A1+A2 round,
// plus W5T: flight and transport legs are routes too, flown and ridden through the W5 host seams).
//
// The graph loads every link the node store holds (walk, area trigger, transport, flight path, teleport spell, static portal), but
// on ForeverCore the A* (TravelNodePath::getCost) only takes some of them: decision D4 allows
// no travel teleport but the host's logged rescue, so the teleport-spell and static-portal links are refused. A link
// the A* refuses is not a route, and nothing that asks "can I get there" may count it:
//  * TravelNode::getNodeMap (hasRouteTo, getRouteSize) skips it, so a goal that only a refused link reaches is unreachable;
//  * TravelMgr::LoadMapTransfers adds no map transfer for it, so MapTransDistance does not call a cross-continent goal
//    "finite" because of a link nobody can take (the bot would re-pick that destination on every pick).
// A flight link is usable only for a bot that knows both ends and can pay the fare, a transport link is boarded through
// tbhost::ride: those per-bot checks live in getCost (TravelFlightLinkUsableCompat), this gate only names the types the
// A* may take at all. Turtle answers every type usable, as it always did.
// `type` is TravelNodePathType as its number.
namespace ai
{
    namespace travel_graph
    {
        constexpr std::uint8_t kNone = 0;
        constexpr std::uint8_t kWalk = 1;
        constexpr std::uint8_t kAreaTrigger = 2;
        constexpr std::uint8_t kTransport = 3;
        constexpr std::uint8_t kFlightPath = 4;
        constexpr std::uint8_t kTeleportSpell = 5;
        constexpr std::uint8_t kStaticPortal = 6;

        // ForeverCore: walks, area triggers, transport legs and flight paths (the core teleports the bot when it crosses
        // the trigger itself; flights and rides go through the W5 host seams, never instant flight or teleport).
        // An allowlist: none (0) and any out-of-range database value are not routes either.
        inline bool LinkUsableOnForever(std::uint8_t type)
        {
            return type == kWalk || type == kAreaTrigger || type == kTransport || type == kFlightPath;
        }

        // A link type the graph knows (walk .. static portal). The loader refuses the rest (0 and out of range) on ForeverCore.
        inline bool LinkTypeKnown(std::uint8_t type)
        {
            return type >= kWalk && type <= kStaticPortal;
        }

        // A link becomes a map transfer (a cross-map shortcut of MapTransDistance) only when it joins two maps and may be used.
        inline bool MapTransferWanted(bool usable, std::uint32_t fromMap, std::uint32_t toMap)
        {
            return usable && fromMap != toMap;
        }

        // A walk link stays on one map: the A* costs it as `distance` yards of walking, so a "walk" from Azeroth to Kalimdor
        // would leave the bot standing at the node. Every path point of a walk link must be on the link's map too.
        inline bool WalkLinkMapsConsistent(std::uint8_t type, std::uint32_t fromMap, std::uint32_t toMap)
        {
            return type != kWalk || fromMap == toMap;
        }

        inline bool WalkPathPointOnLinkMap(std::uint8_t type, std::uint32_t linkMap, std::uint32_t pointMap)
        {
            return type != kWalk || linkMap == pointMap;
        }

        // The node walk behind TravelNode::getNodeMap: breadth-first from `start` over the links `passes` accepts, in discovery
        // order, exactly the old loop (open list, close list, the important-only filter, the ignore list, the same-map filter).
        // `Node` is a pointer-like handle; `linksOf(node)` lists (neighbour, link) pairs; `mapOf` and `important` read the node and `usable` the link.
        template <class Node, class LinksOf, class MapOf, class Important, class Usable>
        std::vector<Node> CollectReachable(Node start, bool importantOnly, std::vector<Node> const& ignoreNodes, bool mapOnly,
            LinksOf linksOf, MapOf mapOf, Important important, Usable usable)
        {
            std::vector<Node> openList;
            std::vector<Node> closeList;
            openList.push_back(start);

            std::size_t i = 0;
            while (i < openList.size())
            {
                Node current = openList[i];
                ++i;

                if (!importantOnly || important(current))
                    closeList.push_back(current);

                for (auto const& link : linksOf(current))
                {
                    Node next = link.first;

                    if (!usable(link.second))
                        continue;

                    if (mapOnly && mapOf(next) != mapOf(start))
                        continue;

                    if (std::find(openList.begin(), openList.end(), next) != openList.end())
                        continue;

                    if (!ignoreNodes.empty() && std::find(ignoreNodes.begin(), ignoreNodes.end(), next) != ignoreNodes.end())
                        continue;

                    openList.push_back(next);
                }
            }

            return closeList;
        }

        // The three creature levels a link records (TravelNodePath::maxLevelCreature): [0] creatures hostile to both factions,
        // [1] friendly to Alliance only, [2] friendly to Horde only. Keeps the highest level seen per slot; a level of 0 (not
        // rated, see CreatureLevelPolicy.h) changes nothing.
        template <class Slots>
        void RecordCreatureLevel(Slots& maxLevel, std::uint32_t level, bool allianceFriend, bool hordeFriend)
        {
            using Slot = typename std::decay<decltype(maxLevel[0])>::type;

            if (maxLevel[0] < level && !allianceFriend && !hordeFriend)
                maxLevel[0] = static_cast<Slot>(level);
            if (maxLevel[1] < level && allianceFriend && !hordeFriend)
                maxLevel[1] = static_cast<Slot>(level);
            if (maxLevel[2] < level && !allianceFriend && hordeFriend)
                maxLevel[2] = static_cast<Slot>(level);
        }
    }
}
