# Forge Project CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(CCForge VERSION 0.1.0 LANGUAGES CXX)

# This project relies on C++23 language features (e.g. requires-expressions).
# Do not silently downgrade user-provided standards; require at least C++23.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "Forge requires C++23 or later. Please set -DCMAKE_CXX_STANDARD=23 (or newer).")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always include forge.cmake to configure the library
include(${CMAKE_CURRENT_SOURCE_DIR}/forge.cmake)

# Detect if this is the top-level project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FORGE_IS_TOP_LEVEL TRUE)
else()
    set(FORGE_IS_TOP_LEVEL FALSE)
endif()

option(FORGE_ENABLE_INSTALL "Install CC Forge headers and CMake package config" ${FORGE_IS_TOP_LEVEL})

if(FORGE_ENABLE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(FORGE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CCForge"
        CACHE STRING "Install directory for CC Forge CMake package files")
    set(FORGE_INSTALL_BACKPORTDIR "${CMAKE_INSTALL_DATADIR}/ccforge/backport"
        CACHE STRING "Install directory for CC Forge standard-header backports")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CCForgeConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CCForgeConfig.cmake"
        INSTALL_DESTINATION "${FORGE_INSTALL_CMAKEDIR}"
        PATH_VARS
            CMAKE_INSTALL_INCLUDEDIR
            FORGE_INSTALL_BACKPORTDIR
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CCForgeConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion
        ARCH_INDEPENDENT
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/backport/"
        DESTINATION "${FORGE_INSTALL_BACKPORTDIR}")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/forge.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeBackportProbes.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeMsvcHeaders.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeProbeFingerprint.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeConfiguration.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForgeLanguageGuard.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CCForgeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CCForgeConfigVersion.cmake"
        DESTINATION "${FORGE_INSTALL_CMAKEDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/probes/"
        DESTINATION "${FORGE_INSTALL_CMAKEDIR}/probes")
endif()

# Build focused verification copies without changing ordinary target flags.
# Call in the source target's directory so directory-scoped imported links resolve.
function(forge_add_deprecation_strict_copy target source_test)
    get_target_property(_sources ${target} SOURCES)
    add_executable(${target}_deprecation_strict EXCLUDE_FROM_ALL ${_sources})
    foreach(_property IN ITEMS INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS
            COMPILE_FEATURES LINK_LIBRARIES LINK_OPTIONS LINK_DIRECTORIES
            CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS)
        get_target_property(_value ${target} ${_property})
        if(NOT _value STREQUAL "_value-NOTFOUND")
            set_property(TARGET ${target}_deprecation_strict PROPERTY ${_property} "${_value}")
        endif()
    endforeach()
    if(MSVC)
        target_compile_options(${target}_deprecation_strict PRIVATE /we4996)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target}_deprecation_strict PRIVATE -Werror=deprecated-declarations)
    endif()

    set(_strict_test "${source_test}_deprecation_strict")
    add_test(NAME ${_strict_test}_compile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target}_deprecation_strict)
    set_tests_properties(${_strict_test}_compile PROPERTIES
        RESOURCE_LOCK forge_main_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    # The original CTest provides runtime coverage; copies only check compilation.
endfunction()

# If top-level project, build examples and tests
if(FORGE_IS_TOP_LEVEL)
    message(STATUS "Forge: Building as top-level project")

    option(FORGE_BUILD_EXAMPLES "Build Forge examples" ON)
    option(FORGE_BUILD_TESTS "Build Forge tests" ON)
    option(FORGE_VERIFY_EXTENSION_DEPRECATIONS
        "Treat deprecations as errors in all positive tests and examples" OFF)

    if(FORGE_BUILD_TESTS)
        enable_testing()
    endif()

    if(FORGE_BUILD_EXAMPLES)
        add_subdirectory(example)
    endif()

    if(FORGE_BUILD_TESTS)
        add_subdirectory(test)
    endif()
else()
    message(STATUS "Forge: Building as subproject")
endif()

if(FORGE_IS_TOP_LEVEL AND FORGE_BUILD_TESTS)
    add_custom_target(test_extension_deprecations_strict)
    foreach(_target IN ITEMS test_execution_extension_deprecations_strict
            test_linalg_extension_deprecations_strict
            test_forge_any_sender_deprecation_strict
            test_forge_strict_any_sender_deprecation_strict)
        if(TARGET ${_target})
            add_dependencies(test_extension_deprecations_strict ${_target})
        endif()
    endforeach()
    foreach(_target IN ITEMS test_forge_any_sender_deprecation_strict
            test_forge_strict_any_sender_deprecation_strict)
        if(TARGET ${_target} AND TARGET test_execution_extension_deprecations_strict)
            add_dependencies(test_execution_extension_deprecations_strict ${_target})
        endif()
    endforeach()
    unset(_target)
endif()

if(FORGE_IS_TOP_LEVEL AND FORGE_BUILD_TESTS AND FORGE_VERIFY_EXTENSION_DEPRECATIONS)
    # Include EXCLUDE_FROM_ALL compile probes as well as runtime tests and examples.
    # Compatibility sources suppress only their intentional legacy calls locally.
    function(forge_collect_deprecation_targets directory result)
        get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        set(_checked_targets)
        foreach(_target IN LISTS _targets)
            get_target_property(_type ${_target} TYPE)
            get_target_property(_source_dir ${_target} SOURCE_DIR)
            get_target_property(_skip ${_target} FORGE_DEPRECATION_STRICT_SKIP)
            if(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$"
                    AND _source_dir MATCHES "^${PROJECT_SOURCE_DIR}/(test|example)(/|$)"
                    AND NOT _skip)
                if(MSVC)
                    target_compile_options(${_target} PRIVATE /we4996)
                elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                    target_compile_options(${_target} PRIVATE -Werror=deprecated-declarations)
                endif()
                list(APPEND _checked_targets ${_target})
            endif()
        endforeach()
        get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(_child IN LISTS _children)
            forge_collect_deprecation_targets("${_child}" _child_targets)
            list(APPEND _checked_targets ${_child_targets})
        endforeach()
        set(${result} "${_checked_targets}" PARENT_SCOPE)
    endfunction()

    forge_collect_deprecation_targets("${CMAKE_CURRENT_SOURCE_DIR}" _forge_deprecation_targets)
    if(_forge_deprecation_targets)
        add_dependencies(test_extension_deprecations_strict ${_forge_deprecation_targets})
    endif()
    unset(_forge_deprecation_targets)
endif()
