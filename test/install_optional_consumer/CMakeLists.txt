cmake_minimum_required(VERSION 3.17)

project(ccforge_install_optional_consumer NONE)

set(CMAKE_DISABLE_FIND_PACKAGE_Threads TRUE)
find_package(CCForge CONFIG QUIET)

if(CCForge_FOUND)
    message(FATAL_ERROR "CCForge unexpectedly succeeded without Threads")
endif()
