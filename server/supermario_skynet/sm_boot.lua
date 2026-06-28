local skynet = require "skynet"

skynet.start(function()
    local world = skynet.newservice("sm_world")
    local watchdog = skynet.newservice("sm_watchdog")

    skynet.call(watchdog, "lua", "start", {
        host = skynet.getenv("sm_host") or "127.0.0.1",
        port = tonumber(skynet.getenv("sm_port")) or 7779,
        world = world,
    })

    skynet.error(string.format("[supermario] boot ok host=%s port=%s world=%s watchdog=%s",
        skynet.getenv("sm_host") or "127.0.0.1",
        skynet.getenv("sm_port") or "7779",
        tostring(world),
        tostring(watchdog)))
end)
