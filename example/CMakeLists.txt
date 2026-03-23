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

# Execution examples (unconditional)
add_executable(execution_schedulers execution_schedulers_example.cpp)
target_link_libraries(execution_schedulers PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_schedulers PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_schedulers BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_let execution_let_example.cpp)
target_link_libraries(execution_let PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_let PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_let BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_stop_token execution_stop_token_example.cpp)
target_link_libraries(execution_stop_token PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_stop_token PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_stop_token BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_phase4 execution_phase4_example.cpp)
target_link_libraries(execution_phase4 PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_phase4 PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_phase4 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# Forge extension examples (unconditional)
add_executable(forge_any_sender forge_any_sender_example.cpp)
target_link_libraries(forge_any_sender PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_any_sender PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_any_receiver forge_any_receiver_example.cpp)
target_link_libraries(forge_any_receiver PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_any_receiver PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_thread_pool forge_thread_pool_example.cpp)
target_link_libraries(forge_thread_pool PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_thread_pool PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_system_context forge_system_context_example.cpp)
target_link_libraries(forge_system_context PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_system_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

# Coroutine examples (unconditional, source-level guards)
add_executable(execution_coro execution_coro_example.cpp)
target_link_libraries(execution_coro PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_coro PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_coro BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(forge_task forge_task_example.cpp)
target_link_libraries(forge_task PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_task PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

# linalg SIMD example (only with mdspan)
if(FORGE_EXAMPLE_HAS_MDSPAN)
    add_executable(linalg_simd_example linalg_simd_example.cpp)
    target_link_libraries(linalg_simd_example PRIVATE forge::forge)
    target_include_directories(linalg_simd_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_level2 linalg_level2_example.cpp)
    target_link_libraries(linalg_level2 PRIVATE forge::forge)
    target_include_directories(linalg_level2 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_level3 linalg_level3_example.cpp)
    target_link_libraries(linalg_level3 PRIVATE forge::forge)
    target_include_directories(linalg_level3 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_views linalg_views_example.cpp)
    target_link_libraries(linalg_views PRIVATE forge::forge)
    target_include_directories(linalg_views BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endif()
