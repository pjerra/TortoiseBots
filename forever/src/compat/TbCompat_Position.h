// cmangos's WorldLocation on ForeverCore (slice 1, round R3). Included from forever-compat-shim.h, last.
//
// The bot brain treats a position as a plain value: `WorldLocation` is a struct with PUBLIC `mapId, x, y, z, o`
// (Turtle: src/game/SharedDefines.h:1901-1939), `WorldPosition` derives from it and reads those five members 225
// times (ai/playerbot/WorldPosition.h), and 25 more files copy, compare and return it. ForeverCore's WorldLocation is a
// `Position` subclass whose members are private-ish accessors (`m_mapId`, `GetPositionX()`, the orientation is private
// and normalised on every write; Position.h:29-221), so a field read cannot compile against it.
//
// Design (D2 of the R3 brief): the core's class is NOT renamed. This file defines our own struct with the old field
// names, `TbLegacyWorldLocation`, converts to and from the core's class, and ends by making the TOKEN `WorldLocation` mean
// that struct for every legacy source file. The macro is the last thing the shim defines, after every core header the bot
// brain uses (botpch_forever.h includes them all first), so no core header is ever read under it. A legacy file that
// includes a core header botpch_forever.h does not already pull in would read that header under the macro; the check for
// this is forever/tools/check_worldlocation_scope.py (documented in SLICE1-R3-NOTES.md).
//
// What the struct matches (Turtle SharedDefines.h:1901-1939), what it does differently, and why:
//   same      mapId/x/y/z/o public, all default 0, the constructor takes (mapId, x, y, z, o) with every argument
//             defaulted and is `explicit`, copy construction, CopyFrom, IsEmpty, Clear.
//   same      the orientation is stored as given (Turtle does not normalise either). The core's WorldLocation does
//             normalise to [0, 2*pi) when a value is handed to it, which is what conversion TO the core does.
//   differs   a default `TbLegacyWorldLocation` is mapId 0; the core's default is MAPID_INVALID (0xFFFFFFFF). The legacy
//             code compares against `WorldLocation()` (an all-zero value: `IsNullLocation`, `IsEmpty`), so the Turtle
//             default is the one that keeps the meaning. Converting a core location keeps its map id as it is, including
//             MAPID_INVALID.
#pragma once

#include "Position.h"   // the core's Position / WorldLocation (already in front of every legacy file)

struct TbLegacyWorldLocation
{
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;

    explicit TbLegacyWorldLocation(uint32 _mapid = 0, float _x = 0, float _y = 0, float _z = 0, float _o = 0)
        : mapId(_mapid), x(_x), y(_y), z(_z), o(_o) { }
    TbLegacyWorldLocation(TbLegacyWorldLocation const&) = default;
    TbLegacyWorldLocation& operator=(TbLegacyWorldLocation const&) = default;

    // From the core: a core WorldLocation (a map id and a Position) or a WorldObject (which is one).
    TbLegacyWorldLocation(::WorldLocation const& core)
        : mapId(core.GetMapId()), x(core.GetPositionX()), y(core.GetPositionY()), z(core.GetPositionZ()),
          o(core.GetOrientation()) { }

    // To the core, for the few places a bot hands a location to a core function.
    operator ::WorldLocation() const { return ::WorldLocation(mapId, x, y, z, o); }

    void CopyFrom(const TbLegacyWorldLocation other)
    {
        mapId = other.mapId;
        x = other.x;
        y = other.y;
        z = other.z;
        o = other.o;
    }

    bool IsEmpty() const { return mapId == 0 && x == 0 && y == 0 && z == 0 && o == 0; }

    void Clear()
    {
        mapId = 0;
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        o = 0.0f;
    }
};

// A point of a legacy WorldLocation as a core Position (x, y, z, o), for the core functions that take one.
inline Position TbCorePosition(TbLegacyWorldLocation const& loc) { return Position(loc.x, loc.y, loc.z, loc.o); }

// From here on the token means the struct above. Nothing below this line in the shim may name the core's class.
#define WorldLocation TbLegacyWorldLocation
