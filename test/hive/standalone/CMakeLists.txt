cmake_minimum_required(VERSION 3.20)
project(ForgeHiveStandalone LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "The Hive harness requires C++23 or later")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(FORGE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(FORGE_HIVE_GTEST_SOURCE_DIR "${FORGE_ROOT_DIR}/3rdparty/googletest"
    CACHE PATH "Existing pinned GoogleTest source directory")
if(NOT EXISTS "${FORGE_HIVE_GTEST_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set FORGE_HIVE_GTEST_SOURCE_DIR to an existing GoogleTest source tree")
endif()

# A header alone is enough to treat Hive as partial native support. Do not
# inject into that implementation, even when it lacks the feature macro.
include(CheckCXXSourceCompiles)
unset(FORGE_HIVE_NATIVE_PROBE_OK CACHE)
check_cxx_source_compiles("#include <version>
#if defined(__cpp_lib_hive)
#error Native Hive feature macro
#endif
#if defined(__has_include)
#if __has_include(<hive>)
#error Native Hive header
#endif
#else
#error Cannot establish that a native Hive header is absent
#endif
int main() { return 0; }" FORGE_HIVE_NATIVE_PROBE_OK)
if(NOT FORGE_HIVE_NATIVE_PROBE_OK)
    message(FATAL_ERROR "Cannot establish absence of native Hive; refusing backport injection (see CMake configure log)")
endif()
if(MSVC)
    include("${FORGE_ROOT_DIR}/cmake/ForgeMsvcHeaders.cmake")
    _forge_find_msvc_standard_header(FORGE_HIVE_NATIVE_MSVC_HEADER hive)
    if(FORGE_HIVE_NATIVE_MSVC_HEADER)
        message(FATAL_ERROR "Native MSVC Hive header found; refusing backport injection")
    endif()
endif()

# Expose neither the unrelated shadow headers nor a synthetic <version>.
set(hive_include "${CMAKE_CURRENT_BINARY_DIR}/hive-include")
file(MAKE_DIRECTORY "${hive_include}/cpp26")
configure_file("${FORGE_ROOT_DIR}/backport/hive" "${hive_include}/hive" COPYONLY)
configure_file("${FORGE_ROOT_DIR}/backport/cpp26/hive.hpp"
    "${hive_include}/cpp26/hive.hpp" COPYONLY)

set(hive_compile_dir "${CMAKE_CURRENT_LIST_DIR}/../compile")
function(hive_compile_check name expect_success)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(result "${CMAKE_CURRENT_BINARY_DIR}/compile-checks/${name}"
        "${hive_compile_dir}/test_hive_${name}.cpp"
        CMAKE_FLAGS
            "-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}"
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DCMAKE_CXX_EXTENSIONS=OFF"
            "-DINCLUDE_DIRECTORIES=${hive_include}"
        OUTPUT_VARIABLE output)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compile-checks/${name}.log" "${output}")
    if(expect_success AND NOT result)
        message(FATAL_ERROR "Hive positive compile check ${name} failed; see compile-checks/${name}.log")
    elseif(NOT expect_success AND result)
        message(FATAL_ERROR "Hive rejection compile check ${name} unexpectedly succeeded; see compile-checks/${name}.log")
    endif()
    message(STATUS "Hive compile check ${name}: as expected")
endfunction()

# Each rejection has a corresponding positive control compiled first.
hive_compile_check(include TRUE)
hive_compile_check(api TRUE)
hive_compile_check(fancy_api TRUE)
hive_compile_check(accept_from_range TRUE)
hive_compile_check(reject_incompatible_from_range FALSE)
hive_compile_check(accept_construct_only_range TRUE)
hive_compile_check(accept_assign_range TRUE)
hive_compile_check(reject_nonassignable_assign_range FALSE)
hive_compile_check(accept_iterator_allocator TRUE)
hive_compile_check(reject_non_allocator FALSE)
hive_compile_check(accept_legacy_input TRUE)
hive_compile_check(reject_non_input_iterator FALSE)

enable_testing()
set(INSTALL_GTEST OFF CACHE BOOL "Do not install the standalone test dependency" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "The Hive harness only uses GoogleTest" FORCE)
if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the consumer CRT" FORCE)
endif()
add_subdirectory("${FORGE_HIVE_GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
set(hive_runtime_dir "${CMAKE_CURRENT_LIST_DIR}/../runtime")
add_executable(hive_runtime
    "${hive_runtime_dir}/test_hive_allocators.cpp"
    "${hive_runtime_dir}/test_hive_core_model.cpp"
    "${hive_runtime_dir}/test_hive_exceptions.cpp"
    "${hive_runtime_dir}/test_hive_fancy_allocator.cpp"
    "${hive_runtime_dir}/test_hive_ranges_types.cpp"
    "${hive_runtime_dir}/test_hive_semantics.cpp"
    "${hive_runtime_dir}/test_hive_splice_edges.cpp")
target_include_directories(hive_runtime PRIVATE "${hive_include}")
target_link_libraries(hive_runtime PRIVATE GTest::gtest_main)
add_test(NAME hive_runtime COMMAND hive_runtime)

add_executable(hive_multi_tu
    "${hive_runtime_dir}/test_hive_multi_tu_main.cpp"
    "${hive_runtime_dir}/test_hive_multi_tu_other.cpp")
target_include_directories(hive_multi_tu PRIVATE "${hive_include}")
add_test(NAME hive_multi_tu COMMAND hive_multi_tu)
set_tests_properties(hive_runtime hive_multi_tu PROPERTIES TIMEOUT 120)

message(STATUS "Hive <version>-only feature macro check: DEFERRED (no shared version backport)")
message(STATUS "Hive mutation script: DEFERRED (not compatible with isolated toolchain/header scope)")
