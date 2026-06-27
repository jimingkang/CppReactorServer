add_executable(game2048_server
        game2048.cpp
        game2048_server.cpp
)
target_compile_features(game2048_server PRIVATE cxx_std_20)
target_include_directories(game2048_server PRIVATE . ..)
set_target_properties(game2048_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
