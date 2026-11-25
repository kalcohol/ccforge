cmake_minimum_required(VERSION 3.20)

project(CCForgeInstallConsumer LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "CC Forge install consumer requires C++23 or later")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CCForge 0.1 CONFIG REQUIRED)

foreach(_ccforge_gtest_target IN ITEMS
        gtest
        gtest_main
        gmock
        gmock_main
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        GTest::GTest
        GTest::Main)
    if(TARGET ${_ccforge_gtest_target})
        message(FATAL_ERROR
            "The installed CC Forge package unexpectedly exposed ${_ccforge_gtest_target}")
    endif()
endforeach()

add_executable(ccforge_install_std_consumer std_main.cpp)
target_link_libraries(ccforge_install_std_consumer PRIVATE forge::std)

add_library(ccforge_install_std_exported_consumer INTERFACE)
target_link_libraries(ccforge_install_std_exported_consumer INTERFACE forge::std)

if(CCFORGE_EXPECT_STANDARD_ONLY)
    if(FORGE_ENABLE_EXTENSIONS OR TARGET forge::forge OR TARGET forge)
        message(FATAL_ERROR "The standard-only package must not enable Forge extensions")
    endif()
    foreach(_backend IN ITEMS BACKEND LINUX_EPOLL_BACKEND WINDOWS_IOCP_BACKEND URING_BACKEND)
        if(NOT DEFINED FORGE_HAS_FORGE_IO_${_backend} OR FORGE_HAS_FORGE_IO_${_backend})
            message(FATAL_ERROR "The standard-only package must publish OFF for ${_backend}")
        endif()
    endforeach()
    get_target_property(_guard_standard forge::std FORGE_PROBE_CXX_STANDARD)
    get_target_property(_guard_options forge::std INTERFACE_COMPILE_OPTIONS)
    if(NOT _guard_standard STREQUAL CMAKE_CXX_STANDARD OR
            NOT _guard_options MATCHES "CCForgeLanguageGuard[.]h" OR
            NOT EXISTS "${CMAKE_BINARY_DIR}/CMakeFiles/CCForgeLanguageGuard.h")
        message(FATAL_ERROR "forge::std did not propagate the consumer language guard")
    endif()

    # Additional negative control when CMake can request distinct dialects.
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/language-guard-negative.txt")
    if(CMAKE_CXX_STANDARD STREQUAL "23" AND "cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        file(READ "${CMAKE_BINARY_DIR}/CMakeFiles/CCForgeLanguageGuard.h" _guard)
        if(_guard MATCHES "probes used C[+][+]23")
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/guard_mismatch.cpp" "int main() {}\n")
            add_executable(ccforge_install_guard_mismatch EXCLUDE_FROM_ALL guard_mismatch.cpp)
            target_link_libraries(ccforge_install_guard_mismatch PRIVATE forge::std)
            set_property(TARGET ccforge_install_guard_mismatch PROPERTY CXX_STANDARD 26)
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/language-guard-negative.txt" "ENABLED\n")
        endif()
    endif()
else()
    add_executable(ccforge_install_consumer main.cpp)
    target_link_libraries(ccforge_install_consumer PRIVATE forge::forge)
endif()

if(FORGE_FORCE_HIVE_BACKPORT OR FORGE_HIVE_FULL OR
        (NOT FORGE_HIVE_PARTIAL_MACRO AND NOT FORGE_HIVE_PARTIAL_LIMITS
            AND NOT FORGE_HIVE_PARTIAL_TEMPLATE AND NOT FORGE_HIVE_PARTIAL_PMR))
    set(_surfaces std)
    if(NOT CCFORGE_EXPECT_STANDARD_ONLY)
        list(APPEND _surfaces forge)
    endif()
    foreach(surface IN LISTS _surfaces)
        add_executable(ccforge_install_hive_${surface} ../consumer_hive.cpp)
        target_link_libraries(ccforge_install_hive_${surface} PRIVATE forge::${surface})
    endforeach()
    target_compile_definitions(ccforge_install_hive_std PRIVATE CCFORGE_CONSUMER_STD_ONLY=1)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hive-consumers.txt" "ENABLED\n")
else()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hive-consumers.txt" "PARTIAL\n")
endif()

install(TARGETS ccforge_install_std_exported_consumer EXPORT CCForgeInstallConsumerTargets)
if(NOT CCFORGE_EXPECT_STANDARD_ONLY)
    add_library(ccforge_install_exported_consumer INTERFACE)
    target_link_libraries(ccforge_install_exported_consumer INTERFACE forge::forge)
    install(TARGETS ccforge_install_exported_consumer EXPORT CCForgeInstallConsumerTargets)
endif()
install(EXPORT CCForgeInstallConsumerTargets
    NAMESPACE ccforge_install_consumer::
    DESTINATION lib/cmake/CCForgeInstallConsumer)
