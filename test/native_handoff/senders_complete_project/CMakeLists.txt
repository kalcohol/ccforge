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
target_link_libraries(senders_complete_consumer PRIVATE forge::std)
set(fixture_targets senders_complete_consumer)
if(FORGE_ENABLE_EXTENSIONS)
    include("${FORGE_ROOT_DIR}/test/TestOwner.cmake")
    _forge_test_owner(forge_fixture_owner OWNER FORGE)
    add_executable(forge_senders_complete_consumer
        "${FORGE_ROOT_DIR}/test/forge/compile/native_handoff_any_stop_token.cpp")
    target_link_libraries(forge_senders_complete_consumer PRIVATE ${forge_fixture_owner})
    list(APPEND fixture_targets forge_senders_complete_consumer)
endif()
foreach(fixture_target IN LISTS fixture_targets)
    target_include_directories(${fixture_target} BEFORE PRIVATE
        "${FORGE_BACKPORT_DIR}"
        "${surface_include}")
    if(SENDERS_CASE STREQUAL "missing_scheduler_allocator")
        target_compile_definitions(${fixture_target} PRIVATE FORGE_TEST_OMIT_SCHEDULER_ALLOCATOR)
    endif()
endforeach()
