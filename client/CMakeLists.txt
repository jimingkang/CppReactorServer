add_executable(stress_client stress_client.cpp)
target_compile_features(stress_client PRIVATE cxx_std_20)

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

    add_executable(super_mario_client super_mario_client.cpp)
    target_link_libraries(super_mario_client PRIVATE imgui glfw OpenGL::GL)

    add_executable(game2048_client 2048/2048_client.cpp)
    target_link_libraries(game2048_client PRIVATE imgui glfw OpenGL::GL)

    add_executable(guess_number_client guessnumber/guess_number_client.cpp)
    target_link_libraries(guess_number_client PRIVATE imgui glfw OpenGL::GL)
    target_compile_features(guess_number_client PRIVATE cxx_std_20)
    set_target_properties(guess_number_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    if (APPLE)
        target_compile_definitions(super_mario_client PRIVATE GL_SILENCE_DEPRECATION)
        target_compile_definitions(game2048_client PRIVATE GL_SILENCE_DEPRECATION)
        target_compile_definitions(guess_number_client PRIVATE GL_SILENCE_DEPRECATION)
    endif ()
endif ()
