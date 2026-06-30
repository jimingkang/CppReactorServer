add_executable(guess_number_server_workers
        worker_main.cpp
        ../worker_server.cpp
        ../service_context.cpp
        ../service_runtime_contexts.cpp
        ../request_reply_service_context.cpp
        ../session_service_context.cpp
        ../socket_utils.cpp
        guess_number_server_binding.cpp
        guess_number_game_world.cpp
        guess_number_room_manager.cpp
        guess_number_session_agent.cpp
)
target_compile_features(guess_number_server_workers PRIVATE cxx_std_20)
target_include_directories(guess_number_server_workers PRIVATE ..)
set_target_properties(guess_number_server_workers PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(guess_number_server_workers PRIVATE _GNU_SOURCE)
endif ()

