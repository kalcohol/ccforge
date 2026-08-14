cmake_minimum_required(VERSION 3.20)

project(CCForgeSourceConsumer LANGUAGES CXX)

if(NOT DEFINED CCFORGE_SOURCE_DIR)
    message(FATAL_ERROR "CCFORGE_SOURCE_DIR must point at the CC Forge source tree")
endif()
if(NOT DEFINED CCFORGE_CONSUMER_MODE)
    message(FATAL_ERROR "CCFORGE_CONSUMER_MODE must be INCLUDE or ADD_SUBDIRECTORY")
endif()

if(DEFINED CCFORGE_FOREIGN_TARGET)
    if(CCFORGE_FOREIGN_TARGET STREQUAL "std")
        add_library(forge::std INTERFACE IMPORTED)
    elseif(CCFORGE_FOREIGN_TARGET STREQUAL "forge")
        add_library(forge::forge INTERFACE IMPORTED)
    else()
        message(FATAL_ERROR
            "Unsupported CCFORGE_FOREIGN_TARGET=${CCFORGE_FOREIGN_TARGET}")
    endif()
endif()

if(CCFORGE_CONSUMER_MODE STREQUAL "INCLUDE")
    include("${CCFORGE_SOURCE_DIR}/forge.cmake")
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

add_library(ccforge_source_exported_consumer INTERFACE)
target_link_libraries(ccforge_source_exported_consumer INTERFACE forge::forge)

add_library(ccforge_source_std_exported_consumer INTERFACE)
target_link_libraries(ccforge_source_std_exported_consumer INTERFACE forge::std)

install(TARGETS
        ccforge_source_exported_consumer
        ccforge_source_std_exported_consumer
    EXPORT CCForgeSourceConsumerTargets)
install(EXPORT CCForgeSourceConsumerTargets
    NAMESPACE ccforge_source_consumer::
    DESTINATION lib/cmake/CCForgeSourceConsumer)
