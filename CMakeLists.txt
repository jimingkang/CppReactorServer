cmake_minimum_required(VERSION 3.24)
project(cppReactorServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_IMGUI_CLIENT "Build the ImGui learning client" ON)
option(BUILD_GAME_SERVER "Build the coroutine reactor game server" ON)

add_subdirectory(client)

if (BUILD_GAME_SERVER AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE))
    add_subdirectory(server/supermario)
    add_subdirectory(server/guessnumber)
    add_subdirectory(server/2048)
else ()
    message(STATUS "Skipping supermario_server: reactor backend requires Linux epoll or macOS/BSD kqueue.")
endif ()
