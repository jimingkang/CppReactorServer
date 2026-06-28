local skynet = require "skynet"
local socket = require "skynet.socket"

local CMD = {}

local fd
local addr
local world
local watchdog
local player_id
local stopped = false

local function send_text(text)
    if not fd or not text or text == "" then
        return
    end
    socket.write(fd, text)
end

local function cleanup()
    if stopped then
        return
    end
    stopped = true

    if player_id then
        pcall(skynet.call, world, "lua", "leave", player_id)
        player_id = nil
    end

    if fd then
        pcall(socket.close, fd)
        pcall(skynet.send, watchdog, "lua", "closed", fd)
        fd = nil
    end

    skynet.exit()
end

local function serve()
    socket.start(fd)
    player_id = assert(skynet.call(world, "lua", "join", skynet.self(), addr))

    send_text(string.format("WELCOME player=%d\n", player_id))
    send_text("COMMANDS INPUT seq vx vy | MOVE dx dy | POS x y | ATTACK playerId | COIN coinId | ROOM [roomId] | STATE | PING | QUIT\n")
    send_text(skynet.call(world, "lua", "snapshot", player_id))

    while true do
        local line = socket.readline(fd, "\n")
        if not line then
            break
        end

        local response, should_quit = skynet.call(world, "lua", "command", player_id, line)
        if response and response ~= "" then
            send_text(response)
        end
        if should_quit then
            break
        end
    end

    cleanup()
end

function CMD.start(conf)
    fd = assert(conf.fd)
    addr = conf.addr or "unknown"
    world = assert(conf.world)
    watchdog = assert(conf.watchdog)
    skynet.fork(serve)
end

function CMD.push(text)
    if not stopped then
        send_text(text)
    end
end

function CMD.stop()
    cleanup()
end

skynet.start(function()
    skynet.dispatch("lua", function(_, _, cmd, ...)
        local f = assert(CMD[cmd], cmd)
        skynet.ret(skynet.pack(f(...)))
    end)
end)
