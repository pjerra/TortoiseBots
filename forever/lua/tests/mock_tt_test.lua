-- Mock test of the ForeverLua toolkit (plain `lua5.4`, no server): fake World/Map/handles with the module's return
-- shapes, the real tt_*.lua files, and a small JSON decoder so every reply is checked as data, not as text fragments.
-- Run: lua5.4 forever/lua/tests/mock_tt_test.lua   (from the repository root)
--
-- The fakes mirror the module (TortoiseBots/forever-lua, test mode): a read on a gone handle answers nil, an action
-- answers false, "gone", Player:group() returns members and the leader guid, Map:los answers nil when it cannot be
-- judged, and a table encodes as an array only when its keys are exactly 1..n (an empty table is {}).
package.path = "forever/lua/tt/?.lua;" .. package.path

local fails, count = 0, 0
local function check(name, cond, detail)
    count = count + 1
    if not cond then
        fails = fails + 1
        print("FAIL " .. name .. (detail ~= nil and (": " .. tostring(detail)) or ""))
    end
end

-- ---- JSON (the engine's rules) ------------------------------------------------------------------------------------
local Json_null = {}
local function encode_string(s)
    s = s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    return '"' .. s .. '"'
end
local function encode_number(v)
    if math.type(v) == "integer" then return tostring(v) end
    if v ~= v or v == math.huge or v == -math.huge then error("NaN or infinity cannot be encoded") end
    return string.format("%.17g", v)
end
local encode
function encode(v)
    local t = type(v)
    if v == nil or v == Json_null then return "null" end
    if t == "boolean" then return v and "true" or "false" end
    if t == "number" then return encode_number(v) end
    if t == "string" then return encode_string(v) end
    if t == "table" then
        local n, maxIndex, onlyInts = 0, 0, true
        for k in pairs(v) do
            n = n + 1
            if math.type(k) == "integer" and k >= 1 then
                if k > maxIndex then maxIndex = k end
            else
                onlyInts = false
            end
        end
        if n > 0 and onlyInts and maxIndex == n then
            local p = {}
            for i = 1, n do p[i] = encode(v[i]) end
            return "[" .. table.concat(p, ",") .. "]"
        end
        local keys = {}
        for k in pairs(v) do
            if type(k) ~= "string" then error("object keys must be strings") end
            keys[#keys + 1] = k
        end
        table.sort(keys)
        local p = {}
        for _, k in ipairs(keys) do p[#p + 1] = encode_string(k) .. ":" .. encode(v[k]) end
        return "{" .. table.concat(p, ",") .. "}"
    end
    error("cannot encode a " .. t)
end
Json = { null = Json_null, encode = encode }

-- A recursive-descent decoder for what encode() writes (plus the engine's null).
local function decode(text)
    local i, n = 1, #text
    local function err(what) error(what .. " at " .. i, 0) end
    local function ws() while i <= n and text:sub(i, i):match("%s") do i = i + 1 end end
    local parse_value
    local function parse_string()
        i = i + 1
        local out = {}
        while true do
            local c = text:sub(i, i)
            if c == "" then err("unterminated string") end
            if c == '"' then i = i + 1 return table.concat(out) end
            if c == "\\" then
                local e = text:sub(i + 1, i + 1)
                i = i + 2
                if e == "n" then out[#out + 1] = "\n"
                elseif e == "t" then out[#out + 1] = "\t"
                elseif e == "r" then out[#out + 1] = "\r"
                elseif e == "u" then err("unicode escape")
                else out[#out + 1] = e end
            else
                out[#out + 1] = c
                i = i + 1
            end
        end
    end
    local function parse_number()
        local j = i
        while i <= n and text:sub(i, i):match("[%d%.eE%+%-]") do i = i + 1 end
        local v = tonumber(text:sub(j, i - 1))
        if not v then err("bad number") end
        return v
    end
    function parse_value()
        ws()
        local c = text:sub(i, i)
        if c == "{" then
            i = i + 1
            local t = {}
            ws()
            if text:sub(i, i) == "}" then i = i + 1 return t end
            while true do
                ws()
                local k = parse_string()
                ws()
                if text:sub(i, i) ~= ":" then err("expected :") end
                i = i + 1
                t[k] = parse_value()
                ws()
                local d = text:sub(i, i)
                i = i + 1
                if d == "}" then return t end
                if d ~= "," then err("expected , or }") end
            end
        elseif c == "[" then
            i = i + 1
            local t = {}
            ws()
            if text:sub(i, i) == "]" then i = i + 1 return t end
            while true do
                t[#t + 1] = parse_value()
                ws()
                local d = text:sub(i, i)
                i = i + 1
                if d == "]" then return t end
                if d ~= "," then err("expected , or ]") end
            end
        elseif c == '"' then
            return parse_string()
        elseif c == "t" then
            if text:sub(i, i + 3) ~= "true" then err("bad literal") end
            i = i + 4
            return true
        elseif c == "f" then
            if text:sub(i, i + 4) ~= "false" then err("bad literal") end
            i = i + 5
            return false
        elseif c == "n" then
            if text:sub(i, i + 3) ~= "null" then err("bad literal") end
            i = i + 4
            return Json_null
        else
            return parse_number()
        end
    end
    local v = parse_value()
    ws()
    if i <= n then err("trailing text") end
    return v
end

local function isnull(v) return v == nil or v == Json_null end

-- ---- engine API stand-ins -----------------------------------------------------------------------------------------
local handlers, timers, now = {}, {}, 1000
local cmdRegistrations = {}
Cmd = {
    register = function(root, fn, sec)
        cmdRegistrations[#cmdRegistrations + 1] = { root = root, fn = fn, sec = sec }
        handlers[root] = fn
        return true
    end,
}
Timer = { after = function(ms, fn) timers[#timers + 1] = { due = now + ms, fn = fn } end }
local logs = {}
local function advance(ms)
    now = now + ms
    local ran = true
    while ran do
        ran = false
        for i, t in ipairs(timers) do
            if t.due <= now then
                table.remove(timers, i)
                t.fn()
                ran = true
                break
            end
        end
    end
end

local objects, byGuid, byName = {}, {}, {}

-- ---- handles: the methods the toolkit calls, with the module's return shapes ---------------------------------------
local function mk(kind, guid, t)
    t = t or {}
    local o = {
        kind = kind, guid = guid,
        x = t.x or 0, y = t.y or 0, z = t.z or 0, o = t.o or 0,
        entry = t.entry or 0, name = t.name or guid,
        hp = t.hp or 100, maxhp = t.maxhp or 100,
        alive = t.alive ~= false, combat = t.combat or false, hostile = t.hostile ~= false,
        rank = t.rank or 0, boss = t.boss or false, pet = t.pet or false, owner = t.owner,
        sec = t.sec or 0, hasSession = t.hasSession ~= false,
        mapId = t.mapId or 0, inst = t.inst or 0,
        group = t.group, leader = t.leader,
        auras = t.auras, cast = t.cast, castTarget = t.castTarget, channelId = t.channelId, victim = t.victim,
        spawnId = t.spawnId or 0, home = t.home,
        goState = t.goState or 0, lootState = t.lootState or 0, spawned = t.spawned ~= false,
        gone = false, noMap = false, inbox = {}, teleported = nil, despawned = nil,
    }
    local H = {}
    function H:valid() return not o.gone end
    function H:guid() return o.guid end
    function H:kind() return o.kind end
    function H:mapId() return o.mapId end
    function H:instanceId() return o.inst end
    function H:entry() if o.gone then return nil end return o.entry end
    function H:name() if o.gone then return nil end return o.name end
    function H:alive() if o.gone then return nil end return o.alive end
    function H:pos() if o.gone then return nil end return o.x, o.y, o.z, o.o end
    function H:level() if o.gone then return nil end return 60 end
    function H:health() if o.gone then return nil end return o.hp end
    function H:maxHealth() if o.gone then return nil end return o.maxhp end
    function H:healthPct() if o.gone then return nil end return o.hp * 100 / o.maxhp end
    function H:inCombat() if o.gone then return nil end return o.combat end
    function H:victim() if o.gone then return nil end return o.victim end
    function H:isHostileTo() if o.gone then return nil end return o.hostile end
    function H:distanceTo() if o.gone then return nil end return 1 end
    function H:aura(id)
        if o.gone then return nil end
        return o.auras and o.auras[id] or nil
    end
    function H:casting()
        if o.gone then return nil end
        if not o.cast then return nil end
        return o.cast, o.castTarget
    end
    function H:channel()
        if o.gone then return nil end
        return o.channelId
    end
    function H:map()
        if o.gone or o.noMap then return nil end
        return objects.map
    end
    function H:isBot() if o.gone then return nil end return false end
    function H:security()
        if o.gone or not o.hasSession then return nil end
        return o.sec
    end
    function H:group()
        if o.gone or not o.group then return nil end
        local list = {}
        for _, m in ipairs(o.group) do
            if not m._o.gone then list[#list + 1] = m end
        end
        return list, o.leader
    end
    function H:message(text)
        if o.gone then return false, "gone" end
        o.inbox[#o.inbox + 1] = text
        return true
    end
    function H:teleport(m, x, y, z, ang)
        if o.gone then return false, "gone" end
        o.x, o.y, o.z, o.o, o.mapId = x, y, z, ang or o.o, m
        o.teleported = { x = x, y = y, z = z, o = ang or 0 }
        return true
    end
    function H:run(line)
        if o.gone then return false, "gone" end
        return true, { "ok: " .. line }
    end
    function H:home()
        if o.gone then return nil end
        local h = o.home or o
        return h.x, h.y, h.z, h.o
    end
    function H:rank() if o.gone then return nil end return o.rank end
    function H:isBoss() if o.gone then return nil end return o.boss end
    function H:isPet() if o.gone then return nil end return o.pet end
    function H:owner()
        if o.gone then return nil end
        if o.owner and not o.owner._o.gone then return o.owner end
        return nil
    end
    function H:kill()
        if o.gone then return false, "gone" end
        o.alive = false
        return true
    end
    function H:spawnId() if o.gone then return nil end return o.spawnId end
    function H:state() if o.gone then return nil end return o.goState end
    function H:lootState() if o.gone then return nil end return o.lootState end
    function H:spawned() if o.gone then return nil end return o.spawned end
    function H:despawn(ms)
        if o.gone then return false, "gone" end
        o.despawned = ms or 0
        if (ms or 0) == 0 then o.gone = true end
        return true
    end
    H._o = o
    objects[guid] = H
    byGuid[guid] = H
    if kind == "player" then byName[o.name] = H end
    return H
end

-- The map's world: creatures and game objects by zone, floors per point, the LoS answer per target.
local map = { floors = {}, tops = {}, noFloor = {}, losResult = {}, creativeRadius = 0 }
function map:id() return 0 end
function map:instance() return 0 end
function map:isDungeon() return false end
function map:valid() return true end
function map:name() return "Mock Map" end
function map:height(x, y, z)
    if not x or not y or not z then return nil end
    local key = string.format("%d,%d", math.floor(x + 0.5), math.floor(y + 0.5))
    if map.noFloor[key] then return nil end
    local f = map.floors[key] or 10.0
    local top = map.tops[key] or f
    if z >= 25 then return top end
    return f
end
function map:los(x, y, z, x2, y2, z2, unit)
    local key = unit and unit:guid()
    if key and map.losResult[key] == nil then return nil end   -- cannot be judged
    if key and map.losResult[key] == false then return false end
    return true
end
function map:find(guid) local h = byGuid[guid] if h and not h._o.gone then return h end return nil end
local function inRadius(o, x, y, z, r)
    local dx, dy, dz = o.x - x, o.y - y, o.z - z
    return dx * dx + dy * dy + dz * dz <= r * r
end
function map:creatures(x, y, z, r, q)
    q = q or {}
    local hasAlive = q.alive ~= nil
    local hasDead = q.dead ~= nil
    local alive, dead = true, false
    if hasAlive or hasDead then alive, dead = hasAlive and q.alive == true, hasDead and q.dead == true end
    local out = {}
    for _, h in pairs(byGuid) do
        local o = h._o
        if o.kind == "creature" and not o.gone and inRadius(o, x, y, z, r)
            and (not q.entry or q.entry == o.entry) and ((o.alive and alive) or (not o.alive and dead)) then
            out[#out + 1] = h
        end
    end
    table.sort(out, function(a, b) return a:guid() < b:guid() end)
    return out
end
function map:gameObjects(x, y, z, r, q)
    q = q or {}
    local out = {}
    for _, h in pairs(byGuid) do
        local o = h._o
        if o.kind == "gameobject" and not o.gone and inRadius(o, x, y, z, r)
            and (not q.entry or q.entry == o.entry) then
            out[#out + 1] = h
        end
    end
    table.sort(out, function(a, b) return a:guid() < b:guid() end)
    return out
end
function map:players() return {} end
function map:spawnCreature() return nil end
objects.map = map

World = {
    now = function() return 1234 end,
    clock = function() return now end,
    log = function(level, text) logs[#logs + 1] = { level = level, text = text } end,
    player = function(key)
        local h = byGuid[key] or byName[key]
        if h and h._o.kind == "player" and not h._o.gone then return h end
        return nil
    end,
}

-- ---- the world --------------------------------------------------------------------------------------------------
local gm = mk("player", "P-gm", { name = "Gm", sec = 3 })
local bot1 = mk("player", "P-b1", { name = "Bot1" })
local bot2 = mk("player", "P-b2", { name = "Bot2" })
local group = { gm, bot1, bot2 }
for _, m in ipairs(group) do m._o.group, m._o.leader = group, "P-gm" end
local function place_at(x, y, z)
    gm._o.x, gm._o.y, gm._o.z = x, y, z
    bot1._o.x, bot1._o.y, bot1._o.z = x + 1, y, z
    bot2._o.x, bot2._o.y, bot2._o.z = x - 1, y, z
end
place_at(0, 0, 10)

-- The test-mode folder loader loads the flat files sorted by name: tt_actions, tt_core, tt_query. Load them in that
-- order, so a file that only works when loaded first is caught here.
require("tt_actions")
require("tt_core")
require("tt_query")

for _, sub in ipairs({ "q.floor", "q.units", "q.unit", "q.los", "q.gos", "q.group", "quiet", "clearto", "place",
    "reset" }) do
    check("sub registered: " .. sub, type(TT.subs[sub]) == "function")
end
check("tt_query's floor helper is loaded (place depends on it)", type(TT.floorAt) == "function")

-- ---- run/reply helpers -------------------------------------------------------------------------------------------
local rawReplies = {}
local function run(args, player, security, id)
    local replies = {}
    local ctx = { player = player, security = security or 3, args = args, id = id or "tx1" }
    function ctx:reply(text) replies[#replies + 1] = text end
    handlers.tt(ctx)
    rawReplies = replies
    local out = {}
    for i, line in ipairs(replies) do
        check("reply " .. i .. " has the TTJSON prefix", line:sub(1, 7) == "TTJSON ", line)
        local ok, t = pcall(decode, line:sub(8))
        check("reply " .. i .. " is valid JSON", ok, ok and nil or t)
        if ok then
            check("reply " .. i .. " carries v and ts", t.v == 1 and t.ts == 1234, line)
            out[i] = t
        end
    end
    return out
end

local function inboxMessages(player)
    local out = {}
    for i, line in ipairs(player._o.inbox) do
        local ok, t = pcall(decode, line:sub(8))
        check("message " .. i .. " is valid JSON", ok and line:sub(1, 7) == "TTJSON ", line)
        out[i] = ok and t or {}
    end
    return out
end
local function lastInbox(player) local list = inboxMessages(player) return list[#list] end
local function last(r) return r[#r] end

-- ---- dispatch, context, errors -----------------------------------------------------------------------------------
check("tt_core registered one command root", #cmdRegistrations == 1 and cmdRegistrations[1].root == "tt"
    and cmdRegistrations[1].sec == 0)
check("no load-time warning with the root configured", #logs == 0, logs[1] and logs[1].text)

local r = run({ "help" }, nil, 255)
check("help from the console answers", #r == 1 and last(r).ok == true and last(r).cmd == "help"
    and last(r).help:find("q floor", 1, true) ~= nil, last(r) and last(r).help)

r = run({ "q", "units", "40" }, nil, 255)
check("console query: needs_player", last(r).err == "needs_player", last(r).err)

r = run({ "q", "units", "40" }, gm, 2)
check("below min rank: not_allowed", last(r).ok == false and last(r).err == "not_allowed", last(r).err)

r = run({ "q", "units", "40" }, gm, 3, "ta1x")
check("the #id is echoed", last(r).id == "ta1x", last(r).id)

gm._o.gone = true
r = run({ "help" }, gm)
check("a gone caller still gets help", last(r).cmd == "help", last(r).cmd)
r = run({ "q", "units" }, gm)
check("a gone caller: not_in_world", last(r).err == "not_in_world", last(r).err)
gm._o.gone = false

r = run({ "bogus" }, gm)
check("unknown sub: usage", last(r).err == "usage", last(r).err)
r = run({ "q", "bogus" }, gm)
check("unknown q sub: q usage", last(r).err == "usage" and last(r).msg:find("q floor", 1, true) ~= nil, last(r).msg)

gm._o.noMap = true
r = run({ "q", "floor" }, gm)
check("a lua error becomes lua_error, not a lost command", last(r).ok == false and last(r).err == "lua_error"
    and last(r).cmd == "tt", last(r).err)
gm._o.noMap = false

-- ---- q floor ----------------------------------------------------------------------------------------------------
map.floors["0,0"], map.tops["0,0"] = 10.0, 10.0
r = run({ "q", "floor" }, gm)
check("q floor: the caller's floor", last(r).ok == true and last(r).floor == 10 and last(r).multi == false
    and last(r).caller_above == 0, last(r).floor)
check("q floor: map info", last(r).map == 0 and last(r).inst == 0 and last(r).dungeon == false)

r = run({ "q", "floor", "0", "0", "12" }, gm)
check("q floor: dz_wanted reports floor - z", last(r).dz_wanted == -2, last(r).dz_wanted)

map.tops["0,0"] = 15.0
r = run({ "q", "floor" }, gm)
check("q floor: two levels -> multi", last(r).multi == true and last(r).top == 15 and last(r).floor == 10)
map.tops["0,0"] = 10.0

r = run({ "q", "floor", "200", "0" }, gm)
check("q floor: a far point is refused", last(r).err == "anchor_far", last(r).err)

map.noFloor["0,0"] = true
r = run({ "q", "floor" }, gm)
check("q floor: no floor answers null, not an error", last(r).ok == true and isnull(last(r).floor), last(r).floor)
map.noFloor["0,0"] = nil

-- ---- q units / q unit -------------------------------------------------------------------------------------------
place_at(0, 0, 10)
local petOwner = bot1
mk("creature", "C-1", { x = 10, y = 0, z = 10, entry = 100, name = "Hostile Near" })
mk("creature", "C-2", { x = 30, y = 0, z = 10, entry = 101, name = "Hostile Far" })
mk("creature", "C-3", { x = 12, y = 0, z = 10, entry = 102, name = "Friendly", hostile = false })
mk("creature", "C-4", { x = 14, y = 0, z = 40, entry = 103, name = "Above" })
mk("creature", "C-5", { x = 15, y = 0, z = 10, entry = 104, name = "Boss", rank = 3, boss = true })
mk("creature", "C-6", { x = 16, y = 0, z = 10, entry = 105, name = "Skipme" })
mk("creature", "C-7", { x = 17, y = 0, z = 10, entry = 106, name = "Pet", pet = true, owner = petOwner })
mk("creature", "C-8", { x = 18, y = 0, z = 10, entry = 107, name = "Rare", rank = 4 })
mk("creature", "C-9", { x = 19, y = 0, z = 10, entry = 108, name = "Corpse", alive = false })
mk("creature", "C-10", { x = 20, y = 0, z = 10, entry = 109, name = "Caster", hp = 50,
    auras = { [12345] = { caster = "Player-1-00000000-0000A1B2", stacks = 2, remainingMs = 4000 } },
    cast = 555, castTarget = "P-b1", channelId = 777, victim = bot1 })

r = run({ "q", "units", "40" }, gm)
check("q units: ok", last(r).ok == true and last(r).cmd == "q.units")
local names = {}
for _, u in ipairs(last(r).units or {}) do names[u.n] = u end
check("q units: the hostile near is listed", names["Hostile Near"] ~= nil)
check("q units: the far one at 30 yd is inside r=40", names["Hostile Far"] ~= nil)
check("q units: the friendly is filtered out", names["Friendly"] == nil)
check("q units: a unit 30 yd above is inside the 2D radius", names["Above"] ~= nil)
check("q units: guids are opaque strings", names["Hostile Near"] and names["Hostile Near"].g == "C-1")
check("q units: n counts the filtered set", last(r).n == 8, last(r).n)
check("q units: the dead one is not listed", names["Corpse"] == nil)
check("q units: the pet is by default hostile to the bot too", names["Pet"] ~= nil)
check("q units: a unit's snapshot has entry and pos", names["Hostile Near"].e == 100
    and names["Hostile Near"].p[1] == 10 and names["Hostile Near"].hpv[2] == 100)
check("q units: rank and db id on a creature", names["Boss"].rk == 3 and names["Boss"].db == 0)
check("q units: home on a creature", names["Hostile Near"].home ~= nil)

local cast = names["Caster"]
check("q units: victim, cast, channel", cast.v == "P-b1" and cast.cs[1] == 555 and cast.cs[2] == "P-b1"
    and cast.ch[1] == 777 and isnull(cast.ch[2]), cast.cs and cast.cs[2])

r = run({ "q", "units", "40", "all" }, gm)
check("q units all: the friendly is listed", (function()
    for _, u in ipairs(last(r).units or {}) do if u.n == "Friendly" then return true end end
    return false
end)())
check("q units all: filter says all", last(r).filter == "all")

r = run({ "q", "units", "40", "friendly" }, gm)
check("q units friendly: only the friendly", last(r).n == 1 and last(r).units[1].n == "Friendly", last(r).n)

r = run({ "q", "units", "40", "dead" }, gm)
check("q units dead: only the corpse", last(r).n == 1 and last(r).units[1].n == "Corpse", last(r).n)
check("q units dead: the corpse reads dead", last(r).units[1].dead == true)

r = run({ "q", "units", "40", "e=100" }, gm)
check("q units e=: the entry filter", last(r).n == 1 and last(r).units[1].e == 100, last(r).n)

r = run({ "q", "units", "40", "au=12345,999" }, gm)
local withAura
for _, u in ipairs(last(r).units or {}) do if u.n == "Caster" then withAura = u end end
check("q units au=: the found aura", withAura and withAura.au[1].id == 12345 and withAura.au[1].stacks == 2
    and withAura.au[1].caster == "Player-1-00000000-0000A1B2" and withAura.au[1].ms == 4000)
check("q units au=: the missing aura is not invented", withAura and #withAura.au == 1)

r = run({ "q", "units", "500" }, gm)
check("q units: the radius is clamped to max_radius", last(r).r == 100, last(r).r)

r = run({ "q", "units", "40", "200", "0", "10" }, gm)
check("q units: a far point is refused", last(r).err == "point_far", last(r).err)

r = run({ "q", "unit", "C-1" }, gm)
check("q unit: the guid is echoed as asked", last(r).asked == "C-1" and last(r).unit.g == "C-1", last(r).asked)
r = run({ "q", "unit", "C-404" }, gm)
check("q unit: unknown guid", last(r).err == "not_found", last(r).err)
r = run({ "q", "unit" }, gm)
check("q unit: without a guid", last(r).err == "not_found", last(r).err)

mk("gameobject", "G-1", { x = 1000, y = 0, z = 10, entry = 200, name = "Chest", goState = 0,
    lootState = 1, spawned = true, spawnId = 88 })
r = run({ "q", "unit", "G-1" }, gm)
check("q unit: a game object is not a unit", last(r).err == "not_a_unit", last(r).err)
r = run({ "q", "unit", "P-b1" }, gm)
check("q unit: a player snapshot", last(r).unit.n == "Bot1" and last(r).unit.e == 0, last(r).unit and last(r).unit.n)

-- ---- q los ------------------------------------------------------------------------------------------------------
map.losResult["C-1"], map.losResult["C-2"] = true, false
r = run({ "q", "los", "0", "0", "10", "C-1", "C-2", "C-404", "G-1" }, gm)
local los = last(r).los or {}
check("q los: four entries", last(r).n == 4 and #los == 4, last(r).n)
check("q los: in sight", los[1].g == "C-1" and los[1]["in"] == true and los[1].d == 10)
check("q los: out of sight", los[2].g == "C-2" and los[2]["in"] == false)
check("q los: a missing guid has its own err", los[3].err == "not_found", los[3] and los[3].err)
check("q los: a game object is not_a_unit", los[4].err == "not_a_unit", los[4] and los[4].err)
check("q los: the at point", last(r).at[1] == 0 and last(r).at[3] == 10)

r = run({ "q", "los", "0", "0", "10", "C-4" }, gm)
check("q los: a read the engine cannot judge is los_failed", last(r).los[1].err == "los_failed",
    last(r).los[1] and last(r).los[1].err)

r = run({ "q", "los", "0", "0", "10" }, gm)
check("q los: usage without a guid", last(r).err == "usage", last(r).err)
r = run({ "q", "los", "0", "0", "10", "C-1", "C-1", "C-1", "C-1", "C-1", "C-1", "C-1", "C-1", "C-1" }, gm)
check("q los: at most 8 guids", last(r).err == "too_many", last(r).err)
r = run({ "q", "los", "300", "0", "10", "C-1" }, gm)
check("q los: a far point is refused", last(r).err == "point_far", last(r).err)

-- ---- q gos ------------------------------------------------------------------------------------------------------
place_at(1000, 0, 10)
r = run({ "q", "gos", "30" }, gm)
check("q gos: the chest is listed", last(r).gos[1].g == "G-1" and last(r).gos[1].e == 200, last(r).gos[1] and last(r).gos[1].g)
check("q gos: state, loot state, spawned, spawn id, name", last(r).gos[1].st == 0 and last(r).gos[1].ls == 1
    and last(r).gos[1].sp == true and last(r).gos[1].db == 88 and last(r).gos[1].n == "Chest")
check("q gos: n counts the objects", last(r).n == 1, last(r).n)
r = run({ "q", "gos", "30", "e=999" }, gm)
check("q gos e=: filters by entry", last(r).n == 0, last(r).n)

-- ---- q group ----------------------------------------------------------------------------------------------------
place_at(0, 0, 10)
r = run({ "q", "group" }, gm)
check("q group: three members", #last(r).members == 3, #last(r).members)
local members = {}
for _, m in ipairs(last(r).members) do members[m.n] = m end
check("q group: positions and distance", members["Bot1"] and members["Bot1"].d == 1 and members["Bot1"].map == 0)
check("q group: the GM flag", members["Gm"].gm == true and members["Bot1"].gm == false)
check("q group: height above the floor", members["Gm"].above_floor == 0, members["Gm"].above_floor and members["Gm"].above_floor)
bot2._o.mapId = 1
r = run({ "q", "group" }, gm)
check("q group: a member on another map has no distance", (function()
    for _, m in ipairs(last(r).members) do if m.n == "Bot2" then return m.d == nil and m.map == 1 end end
    return false
end)())
bot2._o.mapId = 0
bot2._o.gone = true
r = run({ "q", "group" }, gm)
check("q group: a gone member is not listed", #last(r).members == 2, #last(r).members)
bot2._o.gone = false
r = run({ "q", "group", "of=Bot1" }, gm)
check("q group of=: the named player's group", #last(r).members == 3, #last(r).members)

-- ---- quiet ------------------------------------------------------------------------------------------------------
place_at(600, 0, 10)
map.floors["600,0"] = 10.0
local roamA = mk("creature", "Q-A", { x = 640, y = 0, z = 10, entry = 300, name = "RoamA" })

r = run({ "quiet", "20", "w=5", "dz=8" }, gm)
check("quiet read 1: read and final", last(r).read == 1 and last(r).final == false, last(r) and last(r).read)
check("quiet read 1: a hostile at 40 yd is outside r=20", last(r).quiet_now == true and last(r).n_near == 0)
check("quiet read 1: it is inside the roamer radius", last(r).n_scope == 1, last(r).n_scope)
advance(5000)
local q2 = lastInbox(gm)
check("quiet read 2 reaches the caller", q2 ~= nil and q2.read == 2 and q2.final == true and q2.wait_s == 5)
check("quiet read 2: nobody moved -> quiet", q2.quiet == true and #q2.roamers == 0, q2.quiet)
check("quiet read 2: n_near", q2.n_near == 0)

roamA._o.x = 645          -- moved 5 yd between the reads
r = run({ "quiet", "20", "w=5" }, gm)
roamA._o.x = 650
advance(5000)
q2 = lastInbox(gm)
check("quiet read 2: a moved hostile is a roamer", #q2.roamers == 1 and q2.roamers[1].why == "moved"
    and q2.roamers[1].g == "Q-A" and q2.quiet == false, q2.roamers[1] and q2.roamers[1].why)

roamA._o.gone = true
local roamB = mk("creature", "Q-B", { x = 610, y = 0, z = 10, entry = 301, name = "RoamB" })
roamB._o.gone = true
r = run({ "quiet", "20", "w=5" }, gm)
roamB._o.gone = false     -- arrived between the reads
advance(5000)
q2 = lastInbox(gm)
check("quiet read 2: a hostile that arrived is a roamer", #q2.roamers == 1 and q2.roamers[1].why == "arrived"
    and q2.roamers[1].g == "Q-B", q2.roamers[1] and q2.roamers[1].why)
roamB._o.gone = true

-- dz: a hostile 20 yd above is outside the default 8 yd window, inside dz=0
local high = mk("creature", "Q-H", { x = 600, y = 0, z = 30, entry = 302, name = "High" })
r = run({ "quiet", "20", "w=0", "dz=8" }, gm)
check("quiet dz=8: a unit 20 yd above does not count", last(r).quiet == true and last(r).n_near == 0)
r = run({ "quiet", "20", "w=0", "dz=0" }, gm)
check("quiet dz=0: no vertical limit", last(r).quiet == false and last(r).n_near == 1, last(r).n_near)
r = run({ "quiet", "20", "w=0", "skip=302" }, gm)
check("quiet skip=: the skipped entry never counts", last(r).quiet == true, last(r).n_near)
r = run({ "quiet", "20", "w=0" }, gm)
check("quiet w=0: one final reply, quiet", last(r).final == true and last(r).quiet == true, last(r).quiet)
check("quiet: the dz window is echoed", last(r).dz == 8 and last(r).skip[1] == nil)
high._o.gone = true

-- the caller leaves before read 2: no message, no error
local before = #gm._o.inbox
r = run({ "quiet", "20", "w=5" }, gm)
gm._o.gone = true
advance(5000)
gm._o.gone = false
check("quiet: a caller who left gets no read 2", #gm._o.inbox == before, #gm._o.inbox - before)

-- of= picks the judging group
r = run({ "quiet", "20", "w=0", "of=Bot1" }, gm)
check("quiet of=: still answers", last(r).ok == true)

-- ---- clearto ----------------------------------------------------------------------------------------------------
place_at(200, 0, 10)
local hostiles = {}
for i = 1, 3 do
    hostiles[i] = mk("creature", "K-" .. i, { x = 200 + i, y = 0, z = 10, entry = 400 + i, name = "Hostile " .. i })
end
local friend = mk("creature", "K-F", { x = 210, y = 0, z = 10, entry = 410, name = "Friend", hostile = false })
local aBoss = mk("creature", "K-B", { x = 211, y = 0, z = 10, entry = 411, name = "Boss", rank = 3, boss = true })
local aRare = mk("creature", "K-R", { x = 212, y = 0, z = 10, entry = 412, name = "Rare", rank = 4 })
local aPet = mk("creature", "K-P", { x = 213, y = 0, z = 10, entry = 413, name = "Pet", pet = true, owner = bot1 })
local skippable = mk("creature", "K-S", { x = 214, y = 0, z = 10, entry = 414, name = "Skip" })
local highK = mk("creature", "K-H", { x = 215, y = 0, z = 30, entry = 415, name = "High" })

r = run({ "clearto", "0", "200", "0", "10", "30", "skip=414" }, gm)
check("clearto: ok and mode", last(r).ok == true and last(r).mode == "despawn", last(r).mode)
check("clearto: three hostiles despawned", last(r).n == 3 and hostiles[1]._o.gone and hostiles[2]._o.gone
    and hostiles[3]._o.gone, last(r).n)
check("clearto: the friendly is untouched", friend._o.gone == false)
check("clearto: a boss is skipped", aBoss._o.gone == false)
check("clearto: a rare elite (rank 4) is skipped like a boss", aRare._o.gone == false)
check("clearto: an owned pet is skipped", aPet._o.gone == false)
check("clearto: the skip entry is skipped", skippable._o.gone == false)
check("clearto: a unit 20 yd above is outside dz=8", highK._o.gone == false)
local why = {}
for _, s in ipairs(last(r).skipped or {}) do why[s.e] = s.why end
check("clearto: why=skip", why[414] == "skip", why[414])
check("clearto: why=dz", why[415] == "dz", why[415])
check("clearto: why=friendly", why[410] == "friendly", why[410])
check("clearto: why=boss for rank 3 and 4", why[411] == "boss" and why[412] == "boss")
check("clearto: why=owned", why[413] == "owned", why[413])

r = run({ "clearto", "0", "200", "0", "10", "30", "boss", "dz=0" }, gm)
check("clearto boss: a boss is removed with `boss`", aBoss._o.gone == true and aRare._o.gone == true)
check("clearto dz=0: the high unit is removed", highK._o.gone == true)

r = run({ "clearto", "0", "200", "0", "10", "30", "kill", "all=1" }, gm)
check("clearto all=1: the friendly is removed", friend._o.gone or friend._o.alive == false)
check("clearto all=1: mode kill", last(r).mode == "kill" and last(r).all == true)

r = run({ "clearto", "1", "200", "0", "10", "30" }, gm)
check("clearto: wrong map", last(r).err == "wrong_map", last(r).err)
r = run({ "clearto", "0", "200", "0", "10" }, gm)
check("clearto: usage without a radius", last(r).err == "usage", last(r).err)
r = run({ "clearto", "0", "900", "0", "10", "30" }, gm)
check("clearto: a far point is refused", last(r).err == "point_far", last(r).err)

-- no_reference: only GMs in the group
bot1._o.sec, bot2._o.sec = 3, 3
local hostile4 = mk("creature", "K-4", { x = 201, y = 0, z = 10, entry = 404, name = "Hostile 4" })
r = run({ "clearto", "0", "200", "0", "10", "10" }, gm)
check("clearto: without a non-GM member it refuses", last(r).err == "no_reference" and hostile4._o.gone == false,
    last(r).err)
bot1._o.sec, bot2._o.sec = 0, 0
r = run({ "clearto", "0", "200", "0", "10", "10", "of=Bot1" }, gm)
check("clearto of=: the named group judges friendliness", last(r).ok == true and hostile4._o.gone == true, last(r).err)

-- the cap: 65 hostiles, at most 60 removed
place_at(300, 0, 10)
local capped = {}
for i = 1, 65 do
    capped[i] = mk("creature", "X-" .. i, { x = 300 + (i % 10), y = 0 + math.floor(i / 10), z = 10,
        entry = 500 + i, name = "x" .. i })
end
r = run({ "clearto", "0", "300", "0", "10", "30" }, gm)
check("clearto: the removal cap is 60", last(r).n == 60, last(r).n)
local still = 0
for _, h in ipairs(capped) do if not h._o.gone then still = still + 1 end end
check("clearto: exactly 60 of 65 despawned", still == 5, still)

-- ---- place ------------------------------------------------------------------------------------------------------
place_at(400, 0, 10)
map.floors["400,0"] = 10.0
r = run({ "place", "here" }, gm)
check("place read 1", last(r).read == 1 and last(r).final == false and last(r).floor == 10)
check("place read 1: the caller is skipped by default", (function()
    for _, s in ipairs(last(r).skipped or {}) do if s.why == "caller" then return true end end
    return false
end)())
check("place: both bots moved", bot1._o.teleported ~= nil and bot2._o.teleported ~= nil)
advance(4000)
q2 = lastInbox(gm)
check("place read 2 final", q2 ~= nil and q2.read == 2 and q2.final == true and #q2.members == 2)
check("place read 2: everyone landed", q2.placed == true, q2.placed)
check("place read 2: the ring", math.abs(q2.members[1].d) <= 5 and q2.members[1].p ~= nil)

r = run({ "place", "here", "spread=0", "me" }, gm)
advance(4000)
q2 = lastInbox(gm)
check("place me: the caller moves too", q2 ~= nil and #q2.members == 3 and q2.placed == true, #q2.members)
check("place spread=0: everyone on the centre", gm._o.teleported and math.abs(gm._o.teleported.x - 400) < 0.001)

bot1._o.combat = true
r = run({ "place", "here" }, gm)
check("place: a fighting member refuses", last(r).err == "in_combat" and last(r).fighting[1] == "Bot1", last(r).err)
r = run({ "place", "here", "force" }, gm)
check("place force: moves anyway", last(r).read == 1, last(r).err)
advance(4000)
bot1._o.combat = false

-- a mover gone before read 2
r = run({ "place", "here" }, gm)
bot2._o.gone = true
advance(4000)
q2 = lastInbox(gm)
check("place read 2: a gone member is reported, placed false", q2.placed == false and (function()
    for _, m in ipairs(q2.members) do if m.why == "gone" then return true end end
    return false
end)())
bot2._o.gone = false

-- a dead member and a member on another map are skipped
bot2._o.alive = false
bot1._o.mapId = 2
r = run({ "place", "here" }, gm)
check("place: a dead member is skipped", (function()
    for _, s in ipairs(last(r).skipped or {}) do if s.why == "dead" then return true end end
    return false
end)())
check("place: a member on another map is skipped", (function()
    for _, s in ipairs(last(r).skipped or {}) do if s.why == "other_map" then return true end end
    return false
end)())
check("place: nobody left to move", last(r).err == "no_members", last(r).err)
advance(4000)
bot1._o.mapId, bot2._o.alive = 0, true

-- no_members: the caller alone, me not given
local savedGroup = gm._o.group
gm._o.group = nil
r = run({ "place", "here" }, gm)
check("place: a solo caller has nobody to move", last(r).err == "no_members", last(r).err)
gm._o.group = savedGroup

-- anchor and floor refusals
place_at(400, 0, 10)
r = run({ "place", "400", "0", "30" }, gm)
check("place: anchor_dz when the caller is far below z", last(r).err == "anchor_dz", last(r).err)
map.floors["405,0"] = 4.0
r = run({ "place", "405", "0", "10" }, gm)
check("place: floor_mismatch", last(r).err == "floor_mismatch", last(r).err)
map.floors["406,0"] = 12.7
r = run({ "place", "406", "0", "10" }, gm)
check("place: caller_under_floor", last(r).err == "caller_under_floor", last(r).err)
map.noFloor["407,0"] = true
r = run({ "place", "407", "0", "10" }, gm)
check("place: no_floor", last(r).err == "no_floor", last(r).err)
map.noFloor["407,0"] = nil

place_at(400, 0, 30)
map.floors["400,0"] = 10.0
r = run({ "place", "here" }, gm)
check("place here: a caller high above the floor refuses", last(r).err == "caller_high", last(r).err)
place_at(400, 0, 10)

r = run({ "place" }, gm)
check("place without args means here", last(r).read == 1 and last(r).floor == 10)
advance(4000)

-- ---- reset ------------------------------------------------------------------------------------------------------
r = run({ "reset", "533" }, gm)
check("reset: the unbind runs", last(r).ok == true and last(r).handled == true and last(r).unbound[1] == "Gm")
check("reset: the lines are passed back", last(r).lines[1] == "ok: instance unbind 533", last(r).lines and last(r).lines[1])
r = run({ "reset" }, gm)
check("reset: usage", last(r).err == "usage", last(r).err)
r = run({ "reset", "1" }, gm)
check("reset: map 1 has no binds", last(r).err == "not_instance", last(r).err)
gm._o.mapId = 533
r = run({ "reset", "533" }, gm)
check("reset: refuses while inside", last(r).err == "inside", last(r).err)
gm._o.mapId = 0

-- ---- the reply line cap -----------------------------------------------------------------------------------------
place_at(900, 0, 10)
for i = 1, 45 do
    mk("creature", "L-" .. i, { x = 900 + i * 0.1, y = 0, z = 10, entry = 600 + i, name = string.rep("n", 80) })
end
r = run({ "q", "units", "40", "all" }, gm)
check("a long reply stays within the cap", #rawReplies[1] <= 3520, #rawReplies[1])
check("a long reply is flagged trunc", last(r).trunc == true and last(r).n == 45, last(r).n)
check("a long reply keeps a list, cut", #(last(r).units or {}) < 45 and #(last(r).units or {}) > 0,
    #(last(r).units or {}))

-- ---- load-time warning when the root is not configured ----------------------------------------------------------
TT = nil
local savedRegister = Cmd.register
Cmd.register = function() return false, "root not configured" end
logs = {}
do
    local fresh = assert(loadfile("forever/lua/tt/tt_core.lua"))
    fresh()
end
check("a refused command root is logged at load", #logs == 1 and logs[1].text:find("CommandRoots", 1, true) ~= nil,
    logs[1] and logs[1].text)
Cmd.register = savedRegister

print(string.format("%d checks, %d failed", count, fails))
os.exit(fails == 0 and 0 or 1)
