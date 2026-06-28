local skynet = require "skynet"

local CMD = {}

local next_player_id = 1
local rooms = {}
local player_rooms = {}

local tick_cs = 5
local broadcast_cs = 10

local room_shards = 4
local map_width = 2360
local gravity = 1450
local attack_range = 110
local coin_range = 56
local hit_range = 40
local player_width = 28
local player_height = 38
local spawn_x = 70
local spawn_y = 260
local respawn_invuln = 1.0
local death_y = 620

local platform_template = {
    { x = 0, y = 430, w = 2400, h = 90 },
    { x = 260, y = 340, w = 150, h = 28 },
    { x = 540, y = 300, w = 170, h = 28 },
    { x = 850, y = 360, w = 150, h = 28 },
    { x = 1180, y = 315, w = 180, h = 28 },
    { x = 1500, y = 355, w = 220, h = 28 },
    { x = 1850, y = 305, w = 190, h = 28 },
}

local coin_template = {
    { id = 0, x = 315, y = 305, collected = false },
    { id = 1, x = 370, y = 305, collected = false },
    { id = 2, x = 600, y = 265, collected = false },
    { id = 3, x = 665, y = 265, collected = false },
    { id = 4, x = 910, y = 325, collected = false },
    { id = 5, x = 1240, y = 280, collected = false },
    { id = 6, x = 1310, y = 280, collected = false },
    { id = 7, x = 1585, y = 320, collected = false },
    { id = 8, x = 1660, y = 320, collected = false },
    { id = 9, x = 1905, y = 270, collected = false },
    { id = 10, x = 1985, y = 270, collected = false },
    { id = 11, x = 2230, y = 392, collected = false },
}

local monster_template = {
    { id = 0, x = 720, y = 402, min = 690, max = 820, speed = 70 },
    { id = 1, x = 1060, y = 402, min = 1040, max = 1160, speed = -65 },
    { id = 2, x = 1740, y = 327, min = 1730, max = 1840, speed = 55 },
    { id = 3, x = 2100, y = 402, min = 2040, max = 2200, speed = -80 },
}

local function split_words(line)
    local out = {}
    for token in string.gmatch(line or "", "%S+") do
        out[#out + 1] = token
    end
    return out
end

local function clamp(v, lo, hi)
    if v < lo then
        return lo
    end
    if v > hi then
        return hi
    end
    return v
end

local function distance_sq(ax, ay, bx, by)
    local dx = ax - bx
    local dy = ay - by
    return dx * dx + dy * dy
end

local function count_map(t)
    local n = 0
    for _ in pairs(t) do
        n = n + 1
    end
    return n
end

local function clone_array(template)
    local out = {}
    for i, item in ipairs(template) do
        out[i] = {}
        for k, v in pairs(item) do
            out[i][k] = v
        end
    end
    return out
end

local function get_or_create_room(room_id)
    local room = rooms[room_id]
    if room then
        return room
    end

    room = {
        id = room_id,
        players = {},
        agents = {},
        saved_players = {},
        platforms = clone_array(platform_template),
        coins = clone_array(coin_template),
        monsters = clone_array(monster_template),
    }
    rooms[room_id] = room
    return room
end

local function pick_room_id()
    local best_room_id = 1
    local best_count = math.huge
    for room_id = 1, room_shards do
        local room = get_or_create_room(room_id)
        local room_count = count_map(room.players)
        if room_count < best_count then
            best_count = room_count
            best_room_id = room_id
        end
    end
    return best_room_id
end

local function get_room_for_player(player_id)
    local room_id = player_rooms[player_id]
    if not room_id then
        return nil
    end
    return rooms[room_id]
end

local function respawn_player(player)
    player.x = spawn_x
    player.y = spawn_y
    player.vx = 0
    player.vy = 0
    player.on_ground = false
    player.invuln = respawn_invuln
end

local function snapshot_text(room)
    local out = {}
    out[#out + 1] = string.format("ROOM id=%d players=%d\n", room.id, count_map(room.players))
    out[#out + 1] = string.format("STATE players=%d\n", count_map(room.players))

    for _, player in pairs(room.players) do
        out[#out + 1] = string.format("PLAYER id=%d x=%d y=%d hp=%d score=%d\n",
            player.id,
            math.floor(player.x + 0.5),
            math.floor(player.y + 0.5),
            player.hp,
            player.score)
    end

    out[#out + 1] = string.format("SAVED_PLAYERS count=%d\n", count_map(room.saved_players))
    out[#out + 1] = string.format("COINS count=%d\n", #room.coins)
    for _, coin in ipairs(room.coins) do
        out[#out + 1] = string.format("COIN id=%d x=%d y=%d collected=%d\n",
            coin.id,
            coin.x,
            coin.y,
            coin.collected and 1 or 0)
    end

    out[#out + 1] = string.format("MONSTERS count=%d\n", #room.monsters)
    for _, monster in ipairs(room.monsters) do
        out[#out + 1] = string.format("MONSTER id=%d x=%d y=%d min=%d max=%d speed=%d\n",
            monster.id,
            math.floor(monster.x + 0.5),
            monster.y,
            monster.min,
            monster.max,
            monster.speed)
    end

    return table.concat(out)
end

local function broadcast_room_snapshot(room)
    if count_map(room.players) == 0 then
        return
    end

    local payload = snapshot_text(room)
    for player_id, agent in pairs(room.agents) do
        if room.players[player_id] then
            skynet.send(agent, "lua", "push", payload)
        end
    end
end

local function find_coin(room, coin_id)
    for _, coin in ipairs(room.coins) do
        if coin.id == coin_id then
            return coin
        end
    end
end

local function find_monster_near(room, player)
    for _, monster in ipairs(room.monsters) do
        if distance_sq(player.x, player.y, monster.x, monster.y) <= hit_range * hit_range then
            return monster
        end
    end
end

local function apply_platform_collision(room, player, prev_y)
    local landed = false
    local prev_bottom = prev_y + player_height
    local new_bottom = player.y + player_height

    for _, p in ipairs(room.platforms) do
        local inside_x = player.x + player_width > p.x and player.x < p.x + p.w
        local falling = player.vy >= 0
        local crossed = prev_bottom <= p.y + 6 and new_bottom >= p.y and new_bottom <= p.y + p.h + 24
        if inside_x and falling and crossed then
            player.y = p.y - player_height
            player.vy = 0
            landed = true
        end
    end

    player.on_ground = landed
end

local function simulate_player(room, player, dt)
    local prev_y = player.y

    player.vy = player.vy + gravity * dt
    player.x = clamp(player.x + player.vx * dt, 0, map_width)
    player.y = player.y + player.vy * dt

    apply_platform_collision(room, player, prev_y)

    if player.y > death_y then
        respawn_player(player)
        return
    end

    local monster = find_monster_near(room, player)
    if monster and player.invuln <= 0 then
        player.hp = math.max(0, player.hp - 10)
        player.invuln = respawn_invuln
        player.vx = -player.facing * 60
        if player.hp == 0 then
            player.hp = 100
            respawn_player(player)
            return
        end
    end

    if player.invuln > 0 then
        player.invuln = math.max(0, player.invuln - dt)
    end
end

local function simulate_monsters(room, dt)
    for _, monster in ipairs(room.monsters) do
        monster.x = monster.x + monster.speed * dt
        if monster.x < monster.min then
            monster.x = monster.min
            monster.speed = math.abs(monster.speed)
        elseif monster.x > monster.max then
            monster.x = monster.max
            monster.speed = -math.abs(monster.speed)
        end
    end
end

local function attach_player_to_room(room, player, agent)
    room.players[player.id] = player
    room.agents[player.id] = agent
    player_rooms[player.id] = room.id
end

local function detach_player_from_room(room, player_id)
    room.players[player_id] = nil
    room.agents[player_id] = nil
    player_rooms[player_id] = nil
end

local function move_player_to_room(player_id, target_room_id)
    local old_room = get_room_for_player(player_id)
    local player = old_room and old_room.players[player_id]
    if not old_room or not player then
        return nil, "ERR player_not_found\n"
    end

    local target_room = get_or_create_room(target_room_id)
    if target_room.id == old_room.id then
        return old_room, string.format("OK ROOM id=%d\n", target_room.id)
    end

    local agent = old_room.agents[player_id]
    detach_player_from_room(old_room, player_id)
    respawn_player(player)
    attach_player_to_room(target_room, player, agent)

    broadcast_room_snapshot(old_room)
    broadcast_room_snapshot(target_room)
    return target_room, string.format("OK ROOM id=%d\n", target_room.id)
end

function CMD.join(agent, addr)
    local player_id = next_player_id
    next_player_id = next_player_id + 1

    local room = get_or_create_room(pick_room_id())
    local saved = room.saved_players[player_id]
    local player = {
        id = player_id,
        x = saved and saved.x or spawn_x,
        y = saved and saved.y or spawn_y,
        vx = 0,
        vy = 0,
        hp = saved and saved.hp or 100,
        score = saved and saved.score or 0,
        facing = 1,
        on_ground = false,
        invuln = 0,
        addr = addr,
    }

    attach_player_to_room(room, player, agent)
    skynet.error(string.format("[supermario] join player=%d room=%d addr=%s", player_id, room.id, addr or "unknown"))
    return player_id
end

function CMD.leave(player_id)
    local room = get_room_for_player(player_id)
    local player = room and room.players[player_id]
    if not room or not player then
        return true
    end

    room.saved_players[player_id] = {
        x = player.x,
        y = player.y,
        hp = player.hp,
        score = player.score,
    }
    detach_player_from_room(room, player_id)
    return true
end

function CMD.snapshot(player_id)
    local room = player_id and get_room_for_player(player_id) or rooms[1]
    if not room then
        room = get_or_create_room(1)
    end
    return snapshot_text(room)
end

function CMD.command(player_id, line)
    local words = split_words(line)
    local cmd = string.upper(words[1] or "")

    if cmd == "PING" then
        return "PONG\n", false
    end

    if cmd == "HELP" then
        return "COMMANDS INPUT seq vx vy | MOVE dx dy | POS x y | ATTACK playerId | COIN coinId | ROOM [roomId] | STATE | PING | QUIT\n", false
    end

    local room = get_room_for_player(player_id)
    local player = room and room.players[player_id]
    if not room or not player then
        return "ERR player_not_found\n", true
    end

    if cmd == "STATE" then
        return snapshot_text(room), false
    end

    if cmd == "ROOM" then
        if not words[2] then
            return string.format("ROOM id=%d players=%d\n", room.id, count_map(room.players)), false
        end
        local target_room_id = tonumber(words[2])
        if not target_room_id or target_room_id < 1 then
            return "ERR invalid_room\n", false
        end
        local new_room, response = move_player_to_room(player_id, target_room_id)
        if not new_room then
            return response, false
        end
        return response .. snapshot_text(new_room), false
    end

    if cmd == "QUIT" then
        return "QUIT\n", true
    end

    if cmd == "INPUT" then
        local vx = tonumber(words[3]) or 0
        local vy = tonumber(words[4]) or 0
        player.vx = clamp(vx, -320, 320)
        if player.vx < 0 then
            player.facing = -1
        elseif player.vx > 0 then
            player.facing = 1
        end
        if vy < 0 and player.on_ground then
            player.vy = clamp(vy, -700, 700)
            player.on_ground = false
        elseif not player.on_ground then
            player.vy = clamp(vy, -700, 900)
        end
        return string.format("OK INPUT queued seq=%s vx=%d vy=%d\n", words[2] or "0", player.vx, math.floor(player.vy + 0.5)), false
    end

    if cmd == "MOVE" then
        local prev_y = player.y
        local dx = tonumber(words[2]) or 0
        local dy = tonumber(words[3]) or 0
        player.x = clamp(player.x + dx, 0, map_width)
        player.y = player.y + dy
        apply_platform_collision(room, player, prev_y)
        if player.y > death_y then
            respawn_player(player)
        end
        return string.format("OK MOVE player=%d x=%d y=%d\n", player.id, math.floor(player.x + 0.5), math.floor(player.y + 0.5)), false
    end

    if cmd == "POS" then
        local prev_y = player.y
        player.x = clamp(tonumber(words[2]) or player.x, 0, map_width)
        player.y = tonumber(words[3]) or player.y
        player.vx = 0
        player.vy = 0
        apply_platform_collision(room, player, prev_y)
        if player.y > death_y then
            respawn_player(player)
        end
        return string.format("OK POS player=%d x=%d y=%d\n", player.id, math.floor(player.x + 0.5), math.floor(player.y + 0.5)), false
    end

    if cmd == "COIN" then
        local coin = find_coin(room, tonumber(words[2]) or -1)
        if not coin then
            return "ERR coin_not_found\n", false
        end
        if coin.collected then
            return "ERR coin_already_collected\n", false
        end
        if distance_sq(player.x, player.y, coin.x, coin.y) > coin_range * coin_range then
            return "ERR coin_out_of_range\n", false
        end
        coin.collected = true
        player.score = player.score + 10
        return string.format("OK COIN id=%d score=%d\n", coin.id, player.score), false
    end

    if cmd == "ATTACK" then
        local target_id = tonumber(words[2]) or 0
        local target = room.players[target_id]
        if not target then
            return "ERR target_not_found\n", false
        end
        if distance_sq(player.x, player.y, target.x, target.y) > attack_range * attack_range then
            return "ERR target_out_of_range\n", false
        end
        target.hp = math.max(0, target.hp - 10)
        player.score = player.score + 5
        if target.hp == 0 then
            target.hp = 100
            respawn_player(target)
        end
        return string.format("OK ATTACK target=%d hp=%d\n", target_id, target.hp), false
    end

    return "ERR unknown_command. Try HELP\n", false
end

skynet.start(function()
    tick_cs = tonumber(skynet.getenv("sm_tick_cs")) or tick_cs
    broadcast_cs = tonumber(skynet.getenv("sm_broadcast_cs")) or broadcast_cs

    skynet.fork(function()
        while true do
            skynet.sleep(tick_cs)
            local dt = tick_cs / 100.0
            for _, room in pairs(rooms) do
                simulate_monsters(room, dt)
                for _, player in pairs(room.players) do
                    simulate_player(room, player, dt)
                end
            end
        end
    end)

    skynet.fork(function()
        while true do
            skynet.sleep(broadcast_cs)
            for _, room in pairs(rooms) do
                broadcast_room_snapshot(room)
            end
        end
    end)

    skynet.dispatch("lua", function(_, _, cmd, ...)
        local f = assert(CMD[cmd], cmd)
        skynet.ret(skynet.pack(f(...)))
    end)
end)
