cmake_minimum_required(VERSION 3.17)
project(ForgeProbeFingerprintFixture LANGUAGES CXX)

include(CheckCXXSourceCompiles)
include("${CMAKE_CURRENT_SOURCE_DIR}/ForgeProbeFingerprint.cmake")

set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    set(CMAKE_REQUIRED_FLAGS
        "/I\"${CMAKE_CURRENT_SOURCE_DIR}/flag-include\"")
else()
    set(CMAKE_REQUIRED_FLAGS
        "-I\"${CMAKE_CURRENT_SOURCE_DIR}/flag-include\"")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/probe.cmake")
