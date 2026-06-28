root = "./skynet/"
luaservice = root .. "service/?.lua;" .. root .. "test/?.lua;" .. root .. "examples/?.lua;./server/supermario_skynet/?.lua"
lualoader = root .. "lualib/loader.lua"
lua_path = root .. "lualib/?.lua;" .. root .. "lualib/?/init.lua;./server/supermario_skynet/?.lua"
lua_cpath = root .. "luaclib/?.so"
cpath = root .. "cservice/?.so"
snax = root .. "examples/?.lua;" .. root .. "test/?.lua"

thread = 4
logger = nil
logpath = "."
harbor = 0
start = "sm_boot"
bootstrap = "snlua bootstrap"

sm_host = "127.0.0.1"
sm_port = 7789
sm_tick_cs = 5
sm_broadcast_cs = 10
