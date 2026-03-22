# Forge Project CMakeLists.txt
cmake_minimum_required(VERSION 3.17)
project(CCForge VERSION 1.0.0 LANGUAGES CXX)

# This project relies on C++23 language features (e.g. requires-expressions).
# Do not silently downgrade user-provided standards; require at least C++23.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "Forge requires C++23 or later. Please set -DCMAKE_CXX_STANDARD=23 (or newer).")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always include forge.cmake to configure the library
include(${CMAKE_CURRENT_SOURCE_DIR}/forge.cmake)

# Detect if this is the top-level project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FORGE_IS_TOP_LEVEL TRUE)
else()
    set(FORGE_IS_TOP_LEVEL FALSE)
endif()

# If top-level project, build examples and tests
if(FORGE_IS_TOP_LEVEL)
    message(STATUS "Forge: Building as top-level project")

    option(FORGE_BUILD_EXAMPLES "Build Forge examples" ON)
    option(FORGE_BUILD_TESTS "Build Forge tests" ON)

    if(FORGE_BUILD_EXAMPLES)
        add_subdirectory(example)
    endif()

    if(FORGE_BUILD_TESTS)
        enable_testing()
        add_subdirectory(test)
    endif()
else()
    message(STATUS "Forge: Building as subproject")
endif()
