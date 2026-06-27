# add_executable(supermario_server
#         ../main.cpp
#         ../reactor.cpp
#         ../socket_utils.cpp
#         ../game_world.cpp
#         super_mario_game_world.cpp
#         ../server.cpp
# )
# target_compile_features(supermario_server PRIVATE cxx_std_20)
# target_include_directories(supermario_server PRIVATE ..)
# set_target_properties(supermario_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
#     target_compile_definitions(supermario_server PRIVATE _GNU_SOURCE)
# endif ()

# add_executable(game_server_coro
#         ../coro_main.cpp
#         ../coro_reactor.cpp
#         ../socket_utils.cpp
#         ../game_world.cpp
#         super_mario_game_world.cpp
#         ../coro_server.cpp
# )
# target_compile_features(game_server_coro PRIVATE cxx_std_20)
# target_include_directories(game_server_coro PRIVATE ..)
# set_target_properties(game_server_coro PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
#     target_compile_definitions(game_server_coro PRIVATE _GNU_SOURCE)
# endif ()

add_executable(supermario_server_workers
        ../worker_main.cpp
        ../worker_server.cpp
        ../socket_utils.cpp
        ../game_world.cpp
        super_mario_game_world.cpp
)
target_compile_features(supermario_server_workers PRIVATE cxx_std_20)
target_include_directories(supermario_server_workers PRIVATE ..)
set_target_properties(supermario_server_workers PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(supermario_server_workers PRIVATE _GNU_SOURCE)
endif ()
