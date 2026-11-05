# Exercise the same public usage requirements as standard-header consumers.
set(hive_test_library forge::std)
set(hive_source_backport "${CMAKE_CURRENT_SOURCE_DIR}/../../backport")

function(hive_compile_check name expect_success)
    set(target "hive_compile_${name}")
    add_library(${target} OBJECT "${hive_compile_dir}/test_hive_${name}.cpp")
    target_link_libraries(${target} PRIVATE forge::std)
    if(NOT expect_success)
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()

    add_test(NAME ${target}
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DBUILD_CONFIG=$<CONFIG>"
            "-DTARGET_NAME=${target}"
            "-DEXPECT_SUCCESS=${expect_success}"
            "-DLOG=${CMAKE_CURRENT_BINARY_DIR}/compile-checks/${name}.log"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectCompile.cmake")
    set_tests_properties(${target} PROPERTIES
        RESOURCE_LOCK forge_main_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    if(expect_success)
        # Rejection tests require a successful build through the same target.
        set_tests_properties(${target} PROPERTIES FIXTURES_SETUP ${target})
    else()
        set_tests_properties(${target} PROPERTIES
            FIXTURES_REQUIRED "hive_compile_include;hive_compile_${ARGV2}")
    endif()
endfunction()

include("${CMAKE_CURRENT_SOURCE_DIR}/HiveTests.cmake")
