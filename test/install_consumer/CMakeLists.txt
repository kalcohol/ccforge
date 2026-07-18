cmake_minimum_required(VERSION 3.20)

project(CCForgeInstallConsumer LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "CC Forge install consumer requires C++23 or later")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CCForge CONFIG REQUIRED)

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

add_executable(ccforge_install_consumer main.cpp)
target_link_libraries(ccforge_install_consumer PRIVATE forge::forge)

add_executable(ccforge_install_std_consumer std_main.cpp)
target_link_libraries(ccforge_install_std_consumer PRIVATE forge::std)

add_library(ccforge_install_exported_consumer INTERFACE)
target_link_libraries(ccforge_install_exported_consumer INTERFACE forge::forge)

add_library(ccforge_install_std_exported_consumer INTERFACE)
target_link_libraries(ccforge_install_std_exported_consumer INTERFACE forge::std)

install(TARGETS
        ccforge_install_exported_consumer
        ccforge_install_std_exported_consumer
    EXPORT CCForgeInstallConsumerTargets)
install(EXPORT CCForgeInstallConsumerTargets
    NAMESPACE ccforge_install_consumer::
    DESTINATION lib/cmake/CCForgeInstallConsumer)
