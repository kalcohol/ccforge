cmake_minimum_required(VERSION 3.20)

project(forge_partial_simd_handoff LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/partial_include")
include("${FORGE_ROOT_DIR}/forge.cmake")

add_executable(partial_simd_consumer main.cpp)
target_include_directories(partial_simd_consumer BEFORE PRIVATE
    "${FORGE_BACKPORT_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/partial_include")
target_link_libraries(partial_simd_consumer PRIVATE forge::forge)
