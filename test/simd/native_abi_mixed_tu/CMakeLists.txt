cmake_minimum_required(VERSION 3.17)

project(forge_native_abi_mixed_tu LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
        OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "The native ABI mixed-TU fixture requires GCC/Clang on x86-64")
endif()

if(NOT DEFINED FORGE_ROOT_DIR)
    get_filename_component(FORGE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
include("${FORGE_ROOT_DIR}/forge.cmake")
if(FORGE_SIMD_FULL OR FORGE_SIMD_PARTIAL)
    message(FATAL_ERROR "The native ABI fixture requires a toolchain that injects the SIMD backport")
endif()

enable_testing()

set(baseline_flags -msse2 -mno-avx -mno-avx2 -mno-avx512f)
set(wide_flags -mavx2 -mno-avx512f)

function(add_abi_probe target role lanes)
    add_library(${target} OBJECT probe.cpp)
    target_link_libraries(${target} PRIVATE forge::forge)
    target_compile_options(${target} PRIVATE ${ARGN})
    target_compile_definitions(${target} PRIVATE
        FORGE_ABI_PROBE=probe_${role}
        FORGE_ABI_NARROW=roundtrip_narrow_${role}
        FORGE_ABI_WIDE=roundtrip_wide_${role}
        FORGE_ABI_FLOAT_NARROW=roundtrip_float_narrow_${role}
        FORGE_ABI_FLOAT_WIDE=roundtrip_float_wide_${role}
        FORGE_ABI_EXPECTED_LANES=${lanes})
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
endfunction()

add_abi_probe(abi_baseline_first first 4 ${baseline_flags})
add_abi_probe(abi_baseline_second second 4 ${baseline_flags})
add_abi_probe(abi_wide_first first 8 ${wide_flags})
add_abi_probe(abi_wide_second second 8 ${wide_flags})

function(add_abi_test target first second first_lanes second_lanes)
    add_executable(${target} main.cpp $<TARGET_OBJECTS:${first}> $<TARGET_OBJECTS:${second}>)
    target_link_libraries(${target} PRIVATE forge::forge)
    target_compile_options(${target} PRIVATE ${baseline_flags})
    target_compile_definitions(${target} PRIVATE
        FORGE_ABI_FIRST_LANES=${first_lanes}
        FORGE_ABI_SECOND_LANES=${second_lanes})
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
endfunction()

add_abi_test(native_abi_homogeneous_baseline abi_baseline_first abi_baseline_second 4 4)
add_abi_test(native_abi_homogeneous_wide abi_wide_first abi_wide_second 8 8)
add_abi_test(native_abi_mixed_tu abi_baseline_first abi_wide_second 4 8)
