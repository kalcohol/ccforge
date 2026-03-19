# Forge Examples

add_executable(unique_resource_example unique_resource_example.cpp)
target_link_libraries(unique_resource_example PRIVATE forge::forge)

add_executable(simd_example simd_example.cpp)
target_link_libraries(simd_example PRIVATE forge::forge)

add_executable(simd_complete_example simd_complete_example.cpp)
target_link_libraries(simd_complete_example PRIVATE forge::forge)
