cmake_minimum_required(VERSION 3.20)

project(thor_engine
    VERSION 0.2.0
    DESCRIPTION "THOR Engine deterministic adaptive runtime"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(THOR_BUILD_TESTS "Build THOR Engine smoke tests" ON)

# -------------------------------------------------------------------------
# thor_core: every implementation file except the executable entry point.
# -------------------------------------------------------------------------
add_library(thor_core
    src/value_ops.cpp
    src/state.cpp
    src/registry.cpp
    src/expressions.cpp
    src/parser.cpp
    src/executor.cpp
    src/runtime.cpp
    src/cli.cpp
    src/engines.cpp
    src/engines/inputs.cpp
    src/engines/rotational_dynamics.cpp
)

add_library(thor::core ALIAS thor_core)

target_include_directories(thor_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(thor_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(thor_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(thor_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -------------------------------------------------------------------------
# thor: executable entry point only.
# -------------------------------------------------------------------------
add_executable(thor src/main.cpp)

target_link_libraries(thor PRIVATE thor::core)

add_custom_command(TARGET thor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/demo.vas
        $<TARGET_FILE_DIR:thor>/demo.vas
)

# -------------------------------------------------------------------------
# Tests. CTest drives smoke test executables that exit nonzero on failure.
# -------------------------------------------------------------------------
if(THOR_BUILD_TESTS)
    enable_testing()

    add_executable(rotational_smoke tests/rotational_smoke.cpp)
    target_link_libraries(rotational_smoke PRIVATE thor::core)
    add_test(NAME rotational_smoke COMMAND rotational_smoke)

    add_executable(inputs_smoke tests/inputs_smoke.cpp)
    target_link_libraries(inputs_smoke PRIVATE thor::core)
    add_test(NAME inputs_smoke COMMAND inputs_smoke)
endif()
