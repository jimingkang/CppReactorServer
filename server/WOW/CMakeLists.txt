add_executable(wow_worldserver
        worldserver/Main.cpp
        ../worker_server.cpp
        ../service_context.cpp
        ../service_runtime_contexts.cpp
        ../request_reply_service_context.cpp
        ../session_service_context.cpp
        ../socket_utils.cpp
        wow_server_binding.cpp
        wow_game_world.cpp
        wow_session_agent.cpp
        wow_service_contexts.cpp
        game/Globals/ObjectMgr.cpp
        game/Maps/MapMgr.cpp
        game/Server/Protocol/WorldSocket.cpp
        game/Server/WorldSession.cpp
        game/World/World.cpp
)
target_compile_features(wow_worldserver PRIVATE cxx_std_20)
target_include_directories(wow_worldserver PRIVATE ..)
set_target_properties(wow_worldserver PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(wow_worldserver PRIVATE _GNU_SOURCE)
endif ()
