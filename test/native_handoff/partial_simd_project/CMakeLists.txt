cmake_minimum_required(VERSION 3.20)

project(forge_partial_simd_handoff LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(simd_fixture_include "${CMAKE_CURRENT_SOURCE_DIR}/partial_include")
set(CMAKE_REQUIRED_INCLUDES "${simd_fixture_include}")
include("${FORGE_ROOT_DIR}/forge.cmake")

add_executable(partial_simd_consumer main.cpp)
target_include_directories(partial_simd_consumer BEFORE PRIVATE
    "${FORGE_BACKPORT_DIR}"
    "${simd_fixture_include}")
target_link_libraries(partial_simd_consumer PRIVATE forge::std)
if(MSVC)
    # Forward to the synthetic header used by the configure probes.
    get_target_property(definitions forge_std INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER definitions EXCLUDE REGEX "^FORGE_MSVC_SIMD_HEADER=")
    set_property(TARGET forge_std PROPERTY INTERFACE_COMPILE_DEFINITIONS "${definitions}")
    target_compile_definitions(partial_simd_consumer PRIVATE
        FORGE_MSVC_SIMD_HEADER="${simd_fixture_include}/simd")
endif()
