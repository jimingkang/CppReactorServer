cmake_minimum_required(VERSION 3.24)
project(cppReactorServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_IMGUI_CLIENT "Build the ImGui learning client" ON)
option(BUILD_GAME_SERVER "Build the coroutine reactor game server" ON)

if (BUILD_IMGUI_CLIENT)
    include(FetchContent)

    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
    )

    FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.9b
    )

    FetchContent_MakeAvailable(glfw imgui)

    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(imgui PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )

    target_link_libraries(imgui PUBLIC glfw)

    find_package(OpenGL REQUIRED)

    #add_executable(cppImgui client/makov.cpp)
    #target_link_libraries(cppImgui PRIVATE imgui glfw OpenGL::GL)

    add_executable(super_mario_client client/super_mario_client.cpp)
    target_link_libraries(super_mario_client PRIVATE imgui glfw OpenGL::GL)

    if (APPLE)
        #target_compile_definitions(cppImgui PRIVATE GL_SILENCE_DEPRECATION)
        target_compile_definitions(super_mario_client PRIVATE GL_SILENCE_DEPRECATION)
    endif ()
endif ()

if (BUILD_GAME_SERVER AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE))
    add_executable(game_server
            server/main.cpp
            server/reactor.cpp
            server/socket_utils.cpp
            server/game_world.cpp
            server/server.cpp
    )
    target_compile_features(game_server PRIVATE cxx_std_20)
    target_include_directories(game_server PRIVATE server)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(game_server PRIVATE _GNU_SOURCE)
    endif ()

    add_executable(game_server_coro
            server/coro_main.cpp
            server/coro_reactor.cpp
            server/socket_utils.cpp
            server/game_world.cpp
            server/coro_server.cpp
    )
    target_compile_features(game_server_coro PRIVATE cxx_std_20)
    target_include_directories(game_server_coro PRIVATE server)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(game_server_coro PRIVATE _GNU_SOURCE)
    endif ()
else ()
    message(STATUS "Skipping game_server: reactor backend requires Linux epoll or macOS/BSD kqueue.")
endif ()
