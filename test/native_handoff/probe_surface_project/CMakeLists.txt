cmake_minimum_required(VERSION 3.20)

project(forge_probe_surface_handoff LANGUAGES CXX)

if(NOT DEFINED SURFACE_CASE OR SURFACE_CASE STREQUAL "")
    message(FATAL_ERROR "SURFACE_CASE is required")
endif()
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(surface_include
    "${CMAKE_CURRENT_SOURCE_DIR}/partial_include/${SURFACE_CASE}")
if(NOT IS_DIRECTORY "${surface_include}")
    message(FATAL_ERROR "unknown SURFACE_CASE: ${SURFACE_CASE}")
endif()

set(CMAKE_REQUIRED_INCLUDES "${surface_include}")
set(FORGE_ENABLE_FORGE_IO OFF CACHE STRING "" FORCE)
include("${FORGE_ROOT_DIR}/forge.cmake")

add_executable(probe_surface_consumer main.cpp)
target_include_directories(probe_surface_consumer BEFORE PRIVATE
    "${FORGE_BACKPORT_DIR}"
    "${surface_include}")
target_link_libraries(probe_surface_consumer PRIVATE forge::forge)
