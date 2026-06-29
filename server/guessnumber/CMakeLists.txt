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
        guess_number_session_agent.cpp
)
target_compile_features(guess_number_server_workers PRIVATE cxx_std_20)
target_include_directories(guess_number_server_workers PRIVATE ..)
set_target_properties(guess_number_server_workers PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(guess_number_server_workers PRIVATE _GNU_SOURCE)
endif ()

# Client (Imgui-based) for guess number — built when BUILD_IMGUI_CLIENT is enabled
if (BUILD_IMGUI_CLIENT)
    add_executable(guess_number_client guess_number_client.cpp)
    target_link_libraries(guess_number_client PRIVATE imgui glfw OpenGL::GL)
    target_compile_features(guess_number_client PRIVATE cxx_std_20)
    set_target_properties(guess_number_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    if (APPLE)
        target_compile_definitions(guess_number_client PRIVATE GL_SILENCE_DEPRECATION)
    endif()
endif()
