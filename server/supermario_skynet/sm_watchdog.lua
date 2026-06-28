local skynet = require "skynet"
local socket = require "skynet.socket"

local CMD = {}

local listen_fd
local world
local agents = {}

local function accept(fd, addr)
    local agent = skynet.newservice("sm_agent")
    agents[fd] = agent
    socket.abandon(fd)
    skynet.send(agent, "lua", "start", {
        fd = fd,
        addr = addr,
        world = world,
        watchdog = skynet.self(),
    })
end

function CMD.start(conf)
    assert(conf and conf.host and conf.port and conf.world, "invalid watchdog config")
    world = conf.world
    listen_fd = assert(socket.listen(conf.host, conf.port))
    socket.start(listen_fd, accept)
    skynet.error(string.format("[supermario] listening on %s:%d", conf.host, conf.port))
    return true
end

function CMD.closed(fd)
    agents[fd] = nil
end

function CMD.shutdown()
    if listen_fd then
        socket.close(listen_fd)
        listen_fd = nil
    end
    for fd, agent in pairs(agents) do
        skynet.send(agent, "lua", "stop")
        agents[fd] = nil
    end
end

skynet.start(function()
    skynet.dispatch("lua", function(_, _, cmd, ...)
        local f = assert(CMD[cmd], cmd)
        skynet.ret(skynet.pack(f(...)))
    end)
end)
