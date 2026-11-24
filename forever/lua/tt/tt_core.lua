--[==[
ForeverCore test toolkit, core part (TEST SERVERS ONLY; runs inside the ForeverLua module, see TortoiseBots/forever-lua).

The `.tt <sub> [args] #<id>` GM command of the agent's test toolkit, ported from the Eluna toolkit to the ForeverLua
API. The protocol is unchanged: every answer is ONE system-chat line `TTJSON {json}` with v, cmd, ok, id (the `#<id>`
the caller sent), ts; commands that read twice (quiet, place) send `read` 1 and then `read` 2 with `final` true. The one
change is that guids are the opaque strings of ObjectGuid::ToString (e.g. "Creature-0-1-2-3-14-0000A1B2C3"), not 64-bit
hex numbers.

Rules: no game handle is kept across ticks (only guid strings, looked up again); every reply is built from plain
values; work per command is bounded (radius, list and removal caps).
This file is loaded first by the folder loader (init.lua, then names in sorted order); the other tt_*.lua files add
their sub-commands to TT.subs.
]==]

if rawget(_G, "TT") and TT.__loaded then
    return TT
end

TT = {}
TT.__loaded = true
TT.VERSION = 1
TT.subs = {}
TT.NULL = Json.null

TT.CFG = {
    min_gm_rank      = 3,     -- Player:security(); 3 = administrator
    max_radius       = 100,   -- radius cap of quiet / clearto / q (yd)
    max_point_dist   = 150,   -- a point must be this close (2D) to the caller
    max_list         = 40,    -- units/objects in one reply
    max_clear        = 60,    -- creatures removed by one clearto
    max_line         = 3500,  -- reply length cap (chars); lists are cut and "trunc" is set
    z_slack          = 100,   -- the vertical slack of the 3D search sphere behind the 2D radius filters
    place_anchor_max = 40,    -- place / floor at x y z: the caller must be within this (2D)
    place_anchor_dz  = 10,    -- ... and within this height of z
    place_floor_tol  = 3,     -- floor found vs the requested z
    place_max_drop   = 8,     -- caller z minus the floor under it (here mode)
    place_spread     = 3,     -- default ring radius of the members
    place_verify_ms  = 4000,  -- second reply after a place
    default_dz       = 8,     -- quiet/clearto vertical window (yd) when dz= is absent; 0 = none
    quiet_wait_max_s = 60,
    quiet_roam_r     = 80,    -- roamer check radius (the owner's quiet rule)
    quiet_roam_moved = 2.0,   -- moved more than this between two reads = roamer
}

local floor, sqrt = math.floor, math.sqrt

-- ---------------------------------------------------------------------------------------------------------------
-- small helpers
-- ---------------------------------------------------------------------------------------------------------------
function TT.round(v, digits)
    if type(v) ~= "number" or v ~= v or v == math.huge or v == -math.huge then
        return nil
    end
    local m = 10 ^ (digits or 2)
    return floor(v * m + 0.5) / m
end

function TT.dist2(ax, ay, bx, by)
    local dx, dy = ax - bx, ay - by
    return sqrt(dx * dx + dy * dy)
end

function TT.dist3(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return sqrt(dx * dx + dy * dy + dz * dz)
end

function TT.num(s)
    local n = tonumber(s)
    if n and n == n and n > -1e7 and n < 1e7 then return n end
    return nil
end

-- "1,2,3" -> {1, 2, 3} (at most 16)
function TT.idList(s)
    local out = {}
    if not s then return out end
    for id in tostring(s):gmatch("%d+") do
        if #out >= 16 then break end
        out[#out + 1] = tonumber(id)
    end
    return out
end

function TT.clampR(r, default)
    r = r or default
    if r < 1 then r = 1 end
    if r > TT.CFG.max_radius then r = TT.CFG.max_radius end
    return r
end

-- A position as a list of 3 rounded numbers, or nil when the handle is gone.
function TT.pos(h)
    local x, y, z = h:pos()
    if not x then return nil end
    return { TT.round(x), TT.round(y), TT.round(z) }
end

-- ---------------------------------------------------------------------------------------------------------------
-- request parsing: ctx.args are the whitespace-separated tokens (the #id is already taken out into ctx.id)
-- ---------------------------------------------------------------------------------------------------------------
function TT.parse(ctx)
    local req = { pos = {}, kv = {}, flags = {}, id = ctx.id }
    for _, tok in ipairs(ctx.args) do
        local k, v = tok:match("^([%w_]+)=(.*)$")
        if k then
            req.kv[k:lower()] = v
        else
            req.pos[#req.pos + 1] = tok
        end
    end
    for _, p in ipairs(req.pos) do
        req.flags[p:lower()] = true
    end
    return req
end

-- The first three numbers of req.pos from index i.
function TT.point(req, i)
    local x, y, z = TT.num(req.pos[i]), TT.num(req.pos[i + 1]), TT.num(req.pos[i + 2])
    if x and y and z then return x, y, z end
    return nil
end

function TT.flagOn(req, name)
    local v = req.kv[name]
    return req.flags[name] or (v ~= nil and v ~= "" and v ~= "0" and v:lower() ~= "false")
end

-- ---------------------------------------------------------------------------------------------------------------
-- replies
-- ---------------------------------------------------------------------------------------------------------------
local LIST_KEYS = { "units", "gos", "members", "near", "removed", "skipped", "roamers", "los" }

-- The line, with the longest list cut until it fits.
function TT.line(t)
    local line = "TTJSON " .. Json.encode(t)
    local guard = 0
    while #line > TT.CFG.max_line and guard < 200 do
        guard = guard + 1
        local bestKey, bestLen
        for _, k in ipairs(LIST_KEYS) do
            local l = t[k]
            if type(l) == "table" and #l > 0 and (not bestLen or #l > bestLen) then
                bestKey, bestLen = k, #l
            end
        end
        if not bestKey then break end
        table.remove(t[bestKey])
        t.trunc = true
        line = "TTJSON " .. Json.encode(t)
    end
    return line
end

-- A reply during the command (ctx:reply) or later (a player handle's system message).
function TT.send(sink, t)
    t.v = TT.VERSION
    t.ts = World.now()
    local line = TT.line(t)
    if sink.reply then
        sink:reply(line)
    elseif sink.message then
        sink:message(line)
    end
    return line
end

function TT.fail(sink, req, code, msg, extra)
    local t = extra or {}
    t.cmd = req.cmd
    t.id = req.id
    t.ok = false
    t.err = code
    t.msg = msg
    TT.send(sink, t)
end

function TT.ok(sink, req, t)
    t.cmd = req.cmd
    t.id = req.id
    if t.ok == nil then t.ok = true end
    TT.send(sink, t)
end

-- ---------------------------------------------------------------------------------------------------------------
-- world helpers
-- ---------------------------------------------------------------------------------------------------------------
function TT.mapInfo(map)
    return { map = map:id(), inst = map:instance(), dungeon = map:isDungeon() and true or false }
end

-- Creatures of `map` within r (2D) of the point and within dz of its height (dz 0 = any height): a list of
-- { u = handle, d = 2D distance, dz = height difference }, nearest first. The map call is a 3D sphere, wide enough for
-- the vertical slack; the 2D filter is done here.
function TT.creaturesAt(map, x, y, z, r, opts)
    opts = opts or {}
    local R = sqrt(r * r + TT.CFG.z_slack * TT.CFG.z_slack)
    if R > 1000 then R = 1000 end
    local q = { alive = not opts.dead, dead = opts.dead or opts.withDead }
    if opts.entry then q.entry = opts.entry end
    local out = {}
    for _, c in ipairs(map:creatures(x, y, z, R, q) or {}) do
        local cx, cy, cz = c:pos()
        if cx then
            local d = TT.dist2(x, y, cx, cy)
            if d <= r then out[#out + 1] = { u = c, d = d, dz = cz - z, z = cz } end
        end
    end
    table.sort(out, function(a, b) return a.d < b.d end)
    return out
end

-- The group of a player handle: list of member handles (nil group: just the player).
function TT.groupMembers(player, ofName)
    local p = player
    if ofName then
        local o = World.player(ofName)
        if o then p = o end
    end
    local list = p:group()
    if not list then return { p } end
    return list
end

-- The caller (or the leader named by of=) judges hostility: the first living non-GM member of the group on the
-- caller's map, else the caller itself.
function TT.reference(player, ofName)
    local mapId, inst = player:mapId(), player:instanceId()
    for _, m in ipairs(TT.groupMembers(player, ofName)) do
        if m:mapId() == mapId and m:instanceId() == inst and m:alive() and (m:security() or 0) < TT.CFG.min_gm_rank then
            return m, true
        end
    end
    return player, false
end

-- ---------------------------------------------------------------------------------------------------------------
-- unit snapshot
-- ---------------------------------------------------------------------------------------------------------------
-- auraIds: spell ids to look up (the module has no aura listing); each found aura reports its caster.
function TT.unitSnap(u, ox, oy, oz, auraIds)
    local x, y, z = u:pos()
    if not x then return nil end
    local hp, maxhp = u:health(), u:maxHealth()
    local s = {
        g = u:guid(),
        e = u:entry(),
        n = u:name(),
        p = { TT.round(x), TT.round(y), TT.round(z) },
        hp = TT.round(u:healthPct(), 1),
        hpv = { hp, maxhp },
        cb = u:inCombat() and true or false,
        dead = (not u:alive()) and true or false,
    }
    if ox then
        s.d = TT.round(TT.dist2(ox, oy, x, y), 1)
        s.dz = TT.round(z - oz, 1)
    end
    local v = u:victim()
    if v then s.v = v:guid() end
    local cid, ctarget = u:casting()
    if cid then s.cs = { cid, ctarget or TT.NULL } end
    local chid = u:channel()
    if chid then s.ch = { chid, TT.NULL } end
    if u:kind() == "creature" then
        s.db = u:spawnId()
        s.rk = u:rank()
        local hx, hy, hz = u:home()
        if hx then s.home = { TT.round(hx), TT.round(hy), TT.round(hz) } end
    end
    if auraIds and #auraIds > 0 then
        local list = {}
        for i = 1, #auraIds do
            local a = u:aura(auraIds[i])
            if a then
                list[#list + 1] = { id = auraIds[i], caster = a.caster, stacks = a.stacks, ms = a.remainingMs }
            end
        end
        s.au = list
    end
    return s
end

-- ---------------------------------------------------------------------------------------------------------------
-- dispatch
-- ---------------------------------------------------------------------------------------------------------------
local HELP = "tt: q floor|units|unit|los|gos|group, quiet, clearto, place, reset, selftest; quiet/clearto take dz=<yd> " ..
    "(default 8, 0=any) and skip=<entry,..>; clearto never touches friendly NPCs unless all=1 (of=<lead> picks the " ..
    "group); `#id` echoes; guids are ObjectGuid strings"

local function dispatch(ctx)
    local req = TT.parse(ctx)
    local sub = (req.pos[1] or "help"):lower()
    if sub == "help" then
        TT.ok(ctx, { cmd = "help", id = req.id }, { help = HELP })
        return
    end
    local player = ctx.player
    -- console: only help; everything else needs a caller in the world
    if not player then
        TT.fail(ctx, { cmd = sub, id = req.id }, "needs_player", "run it from a GM character")
        return
    end
    if not player:valid() then
        TT.fail(ctx, { cmd = sub, id = req.id }, "not_in_world", "")
        return
    end
    table.remove(req.pos, 1)
    if sub == "q" then
        local what = (req.pos[1] or ""):lower()
        req.cmd = "q." .. what
        local h = TT.subs["q." .. what]
        if not h then
            TT.fail(ctx, req, "usage", "q floor|units|unit|los|gos|group")
            return
        end
        h(ctx, player, req)
        return
    end
    req.cmd = sub
    local h = TT.subs[sub]
    if not h then
        TT.fail(ctx, req, "usage", HELP)
        return
    end
    h(ctx, player, req)
end

TT.dispatch = dispatch
-- Registered for every security level: a caller below min_gm_rank gets the toolkit's own `not_allowed` reply.
local registered, why = Cmd.register("tt", function(ctx)
    if ctx.security < TT.CFG.min_gm_rank then
        TT.send(ctx, { cmd = "tt", id = ctx.id, ok = false, err = "not_allowed",
            msg = "GM rank " .. TT.CFG.min_gm_rank .. "+ only" })
        return
    end
    local ok, err = pcall(dispatch, ctx)
    if not ok then
        TT.send(ctx, { cmd = "tt", id = ctx.id, ok = false, err = "lua_error", msg = tostring(err):sub(1, 300) })
    end
end, 0)
if not registered then
    World.log("warn", "tt toolkit: Cmd.register('tt') was refused (" .. tostring(why) ..
        "): add tt to ForeverLua.CommandRoots and restart")
end

return TT
