cmake_minimum_required(VERSION 3.20)

project(thor_engine
    VERSION 4.0.0
    DESCRIPTION "THOR Engine - VAS DSL runtime"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(thor
    src/main.cpp
    src/cli.cpp
    src/runtime.cpp
    src/executor.cpp
    src/parser.cpp
    src/engines.cpp
    src/engines/rotational_dynamics.cpp
    src/expressions.cpp
    src/registry.cpp
    src/state.cpp
    src/value_ops.cpp
)

target_include_directories(thor PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(MSVC)
    target_compile_options(thor PRIVATE /W4 /permissive-)
else()
    target_compile_options(thor PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Copy demo.vas into the build directory so ./thor run demo.vas works
# from inside build/.
add_custom_command(TARGET thor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/demo.vas
        $<TARGET_FILE_DIR:thor>/demo.vas
)
