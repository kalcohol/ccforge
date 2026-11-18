set(FORGE_UNIQUE_RESOURCE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_UNIQUE_RESOURCE_RUNTIME_DIR "${FORGE_UNIQUE_RESOURCE_TEST_ROOT}/runtime")
set(FORGE_UNIQUE_RESOURCE_COMPILE_DIR "${FORGE_UNIQUE_RESOURCE_TEST_ROOT}/compile")

forge_add_runtime_test(
    test_unique_resource
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource.cpp"
    unique_resource_runtime)
forge_add_runtime_test(
    test_unique_resource_checked
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_checked.cpp"
    unique_resource_checked)
forge_add_runtime_test(
    test_unique_resource_exception
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_exception.cpp"
    unique_resource_exception)
forge_add_runtime_test(
    test_unique_resource_wording
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_wording.cpp"
    unique_resource_wording)

forge_add_compile_probe(
    test_unique_resource_api
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_api.cpp"
    unique_resource_api_compile)
forge_add_compile_probe(
    test_unique_resource_checked_noexcept
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_checked_noexcept.cpp"
    unique_resource_checked_noexcept_compile)
forge_add_compile_probe(
    test_unique_resource_include
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_include.cpp"
    unique_resource_include_compile)

function(forge_check_unique_resource_removal)
    # Supply a vendor stand-in for experimental memory behind Forge's roots.
    list(PREPEND CMAKE_REQUIRED_INCLUDES
        "${FORGE_INCLUDE_DIR}"
        "${FORGE_UNIQUE_RESOURCE_TEST_ROOT}/fixtures")
    if(FORGE_NEEDS_BACKPORT)
        list(PREPEND CMAKE_REQUIRED_INCLUDES "${FORGE_BACKPORT_DIR}")
        # Preserve native stand-aside and MSVC standard-header forwarding
        # when the probe uses the shadow root.
        get_target_property(std_definitions forge_std INTERFACE_COMPILE_DEFINITIONS)
        if(std_definitions)
            foreach(definition IN LISTS std_definitions)
                list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${definition}")
            endforeach()
        endif()
    endif()
    set(source "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/reject_legacy_unique_resource.cpp")
    forge_expect_cxx_source_compiles(FORGE_UNIQUE_RESOURCE_CONTROL "${source}" TRUE
        "Forge unique_resource must be available through its extension header")

    foreach(legacy_case IN ITEMS EXPERIMENTAL PROMOTED EXPERIMENTAL_FACTORY PROMOTED_FACTORY)
        list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_TEST_UNIQUE_RESOURCE_${legacy_case}=1)
        forge_expect_cxx_source_compiles(FORGE_UNIQUE_RESOURCE_LEGACY_${legacy_case} "${source}" FALSE
            "The legacy unique_resource lookup ${legacy_case} must fail")
        list(POP_BACK CMAKE_REQUIRED_DEFINITIONS)
    endforeach()
endfunction()

forge_check_unique_resource_removal()
