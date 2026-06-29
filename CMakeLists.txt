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

    add_executable(game2048_client client/2048/2048_client.cpp)
    target_link_libraries(game2048_client PRIVATE imgui glfw OpenGL::GL)


    if (APPLE)
        #target_compile_definitions(cppImgui PRIVATE GL_SILENCE_DEPRECATION)
        target_compile_definitions(super_mario_client PRIVATE GL_SILENCE_DEPRECATION)
        target_compile_definitions(game2048_client PRIVATE GL_SILENCE_DEPRECATION)
    endif ()
endif ()

add_executable(stress_client client/stress_client.cpp)
target_compile_features(stress_client PRIVATE cxx_std_20)

if (BUILD_GAME_SERVER AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE))
    add_subdirectory(server/supermario)
    add_subdirectory(server/guessnumber)
    add_subdirectory(server/2048)
else ()
    message(STATUS "Skipping supermario_server: reactor backend requires Linux epoll or macOS/BSD kqueue.")
endif ()
