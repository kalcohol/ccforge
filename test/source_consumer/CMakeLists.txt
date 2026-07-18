cmake_minimum_required(VERSION 3.20)

project(CCForgeSourceConsumer LANGUAGES CXX)

if(NOT DEFINED CCFORGE_SOURCE_DIR)
    message(FATAL_ERROR "CCFORGE_SOURCE_DIR must point at the CC Forge source tree")
endif()
if(NOT DEFINED CCFORGE_CONSUMER_MODE)
    message(FATAL_ERROR "CCFORGE_CONSUMER_MODE must be INCLUDE or ADD_SUBDIRECTORY")
endif()

if(CCFORGE_CONSUMER_MODE STREQUAL "INCLUDE")
    include("${CCFORGE_SOURCE_DIR}/forge.cmake")
elseif(CCFORGE_CONSUMER_MODE STREQUAL "ADD_SUBDIRECTORY")
    add_subdirectory("${CCFORGE_SOURCE_DIR}" ccforge)
else()
    message(FATAL_ERROR "Unsupported CCFORGE_CONSUMER_MODE=${CCFORGE_CONSUMER_MODE}")
endif()

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
            "CC Forge source consumption unexpectedly created ${_ccforge_gtest_target}")
    endif()
endforeach()

add_executable(ccforge_source_consumer main.cpp)
target_link_libraries(ccforge_source_consumer PRIVATE forge::forge)
