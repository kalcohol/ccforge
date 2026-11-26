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

set(surface_fixture "${SURFACE_CASE}")
set(no_shadow_root FALSE)
if(SURFACE_CASE MATCHES "^no_shadow_root(_no_mdspan)?$")
    set(surface_fixture independent_roots)
    set(no_shadow_root TRUE)
endif()
set(surface_include
    "${CMAKE_CURRENT_SOURCE_DIR}/partial_include/${surface_fixture}")
if(NOT IS_DIRECTORY "${surface_include}")
    message(FATAL_ERROR "unknown SURFACE_CASE: ${SURFACE_CASE}")
endif()
set(CMAKE_REQUIRED_INCLUDES "${surface_include}")
if(NOT SURFACE_CASE STREQUAL "linalg_empty_namespace")
    if(NOT DEFINED EXECUTION_CASE OR EXECUTION_CASE STREQUAL "")
        message(FATAL_ERROR "EXECUTION_CASE is required")
    endif()
    set(execution_include
        "${CMAKE_CURRENT_SOURCE_DIR}/partial_include/execution/${EXECUTION_CASE}")
    if(NOT IS_DIRECTORY "${execution_include}")
        message(FATAL_ERROR "unknown EXECUTION_CASE: ${EXECUTION_CASE}")
    endif()
    list(PREPEND CMAKE_REQUIRED_INCLUDES "${execution_include}")
endif()
if(no_shadow_root)
    # Provide stand-aside surfaces for the two remaining standard probes.
    include(CheckCXXSourceCompiles)
    unset(fixture_has_native_constant_wrapper CACHE)
    check_cxx_source_compiles("
        #include <utility>
        using probe = std::constant_wrapper<1zu>;
        int main() { [[maybe_unused]] probe* value = nullptr; }
    " fixture_has_native_constant_wrapper)
    if(NOT fixture_has_native_constant_wrapper)
        list(PREPEND CMAKE_REQUIRED_INCLUDES
            "${CMAKE_CURRENT_SOURCE_DIR}/partial_include/constant_wrapper")
    endif()
    list(PREPEND CMAKE_REQUIRED_INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}/../hive_project/include")
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHIVE_FIXTURE_CASE=1)
endif()
if(SURFACE_CASE STREQUAL "no_shadow_root_no_mdspan")
    set(unavailable_include "${CMAKE_CURRENT_SOURCE_DIR}/partial_include/unavailable_mdspan")
    list(PREPEND CMAKE_REQUIRED_INCLUDES "${unavailable_include}")
endif()

set(FORGE_ENABLE_FORGE_IO OFF CACHE STRING "" FORCE)
include("${FORGE_ROOT_DIR}/forge.cmake")

if(no_shadow_root)
    if(FORGE_NEEDS_BACKPORT)
        message(FATAL_ERROR "The no-shadow-root fixture must make every backport stand aside")
    endif()
    enable_testing()
    set(FORGE_TEST_TIMEOUT_SECONDS 300)
    include("${FORGE_ROOT_DIR}/test/CompileProbe.cmake")
    forge_add_compile_probe(probe_surface_consumer no_shadow_root.cpp OWNER STD)
    if(SURFACE_CASE STREQUAL "no_shadow_root_no_mdspan")
        target_include_directories(probe_surface_consumer PRIVATE "${unavailable_include}")
        target_compile_definitions(probe_surface_consumer PRIVATE FORGE_TEST_EXPECT_NO_BASE_MDSPAN)
    endif()
    if(FORGE_ENABLE_EXTENSIONS)
        _forge_test_owner(forge_fixture_owner OWNER FORGE)
        add_executable(forge_probe_surface_consumer EXCLUDE_FROM_ALL
            "${FORGE_ROOT_DIR}/test/forge/compile/native_handoff_unique_resource.cpp")
        target_link_libraries(forge_probe_surface_consumer PRIVATE ${forge_fixture_owner})
        if(SURFACE_CASE STREQUAL "no_shadow_root_no_mdspan")
            target_include_directories(forge_probe_surface_consumer PRIVATE "${unavailable_include}")
        endif()
        # The outer CTest still requests probe_surface_consumer by name.
        add_dependencies(probe_surface_consumer forge_probe_surface_consumer)
    endif()
    return()
endif()

if(SURFACE_CASE STREQUAL "linalg_empty_namespace")
    add_library(probe_surface_consumer OBJECT linalg_empty_namespace.cpp)
    # Let forge::std supply the wrapper ahead of the fixture header.
    set_property(TARGET probe_surface_consumer PROPERTY NO_SYSTEM_FROM_IMPORTED ON)
    target_include_directories(probe_surface_consumer SYSTEM PRIVATE "${surface_include}")
else()
    add_executable(probe_surface_consumer main.cpp)
    target_include_directories(probe_surface_consumer BEFORE PRIVATE
        "${FORGE_BACKPORT_DIR}"
        "${execution_include}"
        "${surface_include}")
endif()
target_link_libraries(probe_surface_consumer PRIVATE forge::std)
