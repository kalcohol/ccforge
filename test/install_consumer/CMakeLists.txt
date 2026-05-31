cmake_minimum_required(VERSION 3.17)

project(CCForgeInstallConsumer LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "CC Forge install consumer requires C++23 or later")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CCForge CONFIG REQUIRED)

add_executable(ccforge_install_consumer main.cpp)
target_link_libraries(ccforge_install_consumer PRIVATE forge::forge)
