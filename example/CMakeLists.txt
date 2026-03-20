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
