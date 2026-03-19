# Forge CMake Configuration
# Include this file in your project to use Forge library

cmake_minimum_required(VERSION 3.17)

# Get the directory where this file is located
get_filename_component(FORGE_ROOT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Set Forge include directory
set(FORGE_INCLUDE_DIR "${FORGE_ROOT_DIR}/include")
set(FORGE_BACKPORT_DIR "${FORGE_ROOT_DIR}/backport")

# Create INTERFACE library target for header-only library
if(NOT TARGET forge)
    add_library(forge INTERFACE)
    add_library(forge::forge ALIAS forge)

    # Set include directories
    target_include_directories(forge INTERFACE
        $<BUILD_INTERFACE:${FORGE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )

    # MSVC: Enable UTF-8 source and execution charset (C++23 compliance)
    target_compile_options(forge INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    )

    # Feature detection for backports
    include(CheckCXXSourceCompiles)

    set(FORGE_NEEDS_BACKPORT FALSE)

    # Check for std::unique_resource (C++26)
    check_cxx_source_compiles("
        #include <memory>
        int main() {
            auto r = std::make_unique_resource(42, [](int){});
            return 0;
        }
    " HAS_STD_UNIQUE_RESOURCE)

    if(NOT HAS_STD_UNIQUE_RESOURCE)
        set(FORGE_NEEDS_BACKPORT TRUE)
        message(STATUS "Forge: std::unique_resource backport enabled")
    endif()

    # Check for std::simd (C++26 current draft)
    check_cxx_source_compiles("
        #include <simd>
        int main() {
            std::simd::vec<float> v(1.0f);
            auto sum = std::simd::reduce(v);
            auto mask = v == v;
            return static_cast<int>(sum + std::simd::reduce_count(mask));
        }
    " HAS_STD_SIMD)

    if(NOT HAS_STD_SIMD)
        set(FORGE_NEEDS_BACKPORT TRUE)
        message(STATUS "Forge: std::simd backport enabled")
    endif()

    # Add backport path if any feature needs it
    if(FORGE_NEEDS_BACKPORT)
        if(MSVC)
            set(FORGE_MSVC_MEMORY_HEADER "")
            set(_forge_msvc_include_candidates ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})

            if(DEFINED ENV{INCLUDE})
                string(REPLACE ";" ";" _forge_env_include "$ENV{INCLUDE}")
                list(APPEND _forge_msvc_include_candidates ${_forge_env_include})
            endif()

            if(DEFINED ENV{VCToolsInstallDir})
                list(APPEND _forge_msvc_include_candidates "$ENV{VCToolsInstallDir}/include")
            endif()

            foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
                if(EXISTS "${_forge_include_dir}/memory")
                    file(TO_CMAKE_PATH "${_forge_include_dir}/memory" FORGE_MSVC_MEMORY_HEADER)
                    break()
                endif()
            endforeach()

            if(NOT FORGE_MSVC_MEMORY_HEADER)
                message(FATAL_ERROR "Forge: failed to locate MSVC standard library header <memory>")
            endif()

            set(FORGE_MSVC_SIMD_HEADER "")

            foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
                if(EXISTS "${_forge_include_dir}/simd")
                    file(TO_CMAKE_PATH "${_forge_include_dir}/simd" FORGE_MSVC_SIMD_HEADER)
                    break()
                endif()
            endforeach()

            target_compile_definitions(forge INTERFACE
                FORGE_MSVC_MEMORY_HEADER=\"${FORGE_MSVC_MEMORY_HEADER}\"
            )

            if(FORGE_MSVC_SIMD_HEADER)
                target_compile_definitions(forge INTERFACE
                    FORGE_MSVC_SIMD_HEADER=\"${FORGE_MSVC_SIMD_HEADER}\"
                )
            endif()
        endif()

        target_include_directories(forge BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>
        )
    endif()

    message(STATUS "Forge library configured: ${FORGE_INCLUDE_DIR}")
endif()
