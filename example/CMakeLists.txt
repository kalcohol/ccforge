# Forge Examples

add_executable(unique_resource_example unique_resource_example.cpp)
target_link_libraries(unique_resource_example PRIVATE forge::forge)

add_executable(simd_example simd_example.cpp)
target_link_libraries(simd_example PRIVATE forge::forge)

add_executable(simd_complete_example simd_complete_example.cpp)
target_link_libraries(simd_complete_example PRIVATE forge::forge)

# std::execution (P2300 MVP) example
#
# This is built unconditionally as part of the repo's examples to ensure the
# wrapper injection shape stays functional.
find_library(FORGE_EXAMPLE_TBB_LIBRARY NAMES tbb)

add_executable(execution_example execution_example.cpp)
target_link_libraries(execution_example PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_example PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# std::submdspan (P2630) example
# Only built when the toolchain provides <mdspan> (e.g. zig/clang libc++ 18+).
# GCC 13 libstdc++ does not ship <mdspan>, so this is guarded.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_EXAMPLE_HAS_MDSPAN)

if(FORGE_EXAMPLE_HAS_MDSPAN)
    add_executable(submdspan_example submdspan_example.cpp)
    target_link_libraries(submdspan_example PRIVATE forge::forge)
    target_include_directories(submdspan_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endif()

# Phase 3 execution example (built unconditionally)
add_executable(execution_phase3_example execution_phase3_example.cpp)
target_link_libraries(execution_phase3_example PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_phase3_example PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_phase3_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# linalg SIMD example (only with mdspan)
if(FORGE_EXAMPLE_HAS_MDSPAN)
    add_executable(linalg_simd_example linalg_simd_example.cpp)
    target_link_libraries(linalg_simd_example PRIVATE forge::forge)
    target_include_directories(linalg_simd_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endif()
