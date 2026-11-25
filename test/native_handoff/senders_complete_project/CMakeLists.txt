cmake_minimum_required(VERSION 3.20)

project(forge_senders_complete_probe LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compile the COMPLETE probe against an independently maintained,
# current-shaped synthetic native surface. Do not use Forge's backport here:
# that would make the probe prove itself.
set(surface_include "${CMAKE_CURRENT_SOURCE_DIR}/complete_include")
set(CMAKE_REQUIRED_INCLUDES "${surface_include}")
if(SENDERS_CASE STREQUAL "missing_scheduler_allocator")
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_TEST_OMIT_SCHEDULER_ALLOCATOR)
endif()
set(FORGE_ENABLE_FORGE_IO OFF CACHE STRING "" FORCE)
include("${FORGE_ROOT_DIR}/forge.cmake")

add_executable(senders_complete_consumer main.cpp)
target_include_directories(senders_complete_consumer BEFORE PRIVATE
    "${FORGE_BACKPORT_DIR}"
    "${surface_include}")
target_link_libraries(senders_complete_consumer PRIVATE forge::forge)
if(SENDERS_CASE STREQUAL "missing_scheduler_allocator")
    target_compile_definitions(senders_complete_consumer PRIVATE FORGE_TEST_OMIT_SCHEDULER_ALLOCATOR)
endif()
