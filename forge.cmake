# CC Forge CMake Configuration
# Include this file in your project to use CC Forge library

cmake_minimum_required(VERSION 3.17)

# Get the directory where this file is located
get_filename_component(FORGE_ROOT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Set CC Forge include directory
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

    # Probe at the SAME language standard the consumer compiles with. Detecting a
    # C++26 feature that is only reachable under -std=c++26 is meaningless if the
    # project itself builds at C++23 (the feature would not be visible there), so
    # native presence must be judged at the build standard, not the compiler max.
    if(DEFINED CMAKE_CXX_STANDARD)
        set(_forge_std "${CMAKE_CXX_STANDARD}")
    else()
        set(_forge_std 23)
    endif()
    set(_forge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(MSVC)
        if(_forge_std GREATER_EQUAL 26)
            set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++latest")
        else()
            set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++${_forge_std}")
        endif()
    else()
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++${_forge_std}")
    endif()

    # Three-state decision helper for each backportable feature.
    #
    #   full  = the COMPLETE required API surface compiles natively
    #   partial = ANY trace of a native implementation is present
    #
    #   full          -> native is complete: stand aside, signal the wrapper.
    #   !full && partial -> native is INCOMPLETE: stand aside anyway to avoid
    #                       injecting the backport on top of partial native
    #                       declarations in namespace std (ODR violation), and
    #                       warn the consumer.
    #   !full && !partial -> no native at all: inject the backport (safe; there
    #                        is nothing to collide with).
    #
    # FORGE_FORCE_<F>_BACKPORT overrides the decision and forces injection
    # (UB-prone on a partial-native toolchain — used only for testing/diagnosis).
    macro(_forge_decide _disp _suffix _full _partial)
        if(FORGE_FORCE_${_suffix}_BACKPORT)
            set(FORGE_NEEDS_BACKPORT TRUE)
            message(WARNING "CC Forge: ${_disp} backport FORCED on (FORGE_FORCE_${_suffix}_BACKPORT=ON); injecting on top of any native implementation risks ODR violations")
        elseif(${_full})
            target_compile_definitions(forge INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
            message(STATUS "CC Forge: ${_disp} native support detected — backport disabled")
        elseif(${_partial})
            target_compile_definitions(forge INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
            message(WARNING "CC Forge: ${_disp} native support is present but INCOMPLETE at -std=c++${_forge_std}; Forge stands aside to avoid ODR conflicts. Wait for the toolchain to finish it, or set -DFORGE_FORCE_${_suffix}_BACKPORT=ON to force the backport (UB-prone).")
        else()
            set(FORGE_NEEDS_BACKPORT TRUE)
            message(STATUS "CC Forge: ${_disp} backport enabled")
        endif()
    endmacro()

    option(FORGE_FORCE_SIMD_BACKPORT "Force the std::simd backport even if (partial) native support exists" OFF)
    option(FORGE_FORCE_SENDERS_BACKPORT "Force the std::execution backport even if (partial) native support exists" OFF)
    option(FORGE_FORCE_SUBMDSPAN_BACKPORT "Force the std::submdspan backport even if (partial) native support exists" OFF)
    option(FORGE_FORCE_LINALG_BACKPORT "Force the std::linalg backport even if (partial) native support exists" OFF)

    # Check for std::unique_resource (TS v3, not in C++26 yet)
    check_cxx_source_compiles("
        #include <version>
        #if !defined(__cpp_lib_unique_resource) || __cpp_lib_unique_resource < 202311L
        #error unique_resource not available
        #endif
        int main() { return 0; }
    " HAS_STD_UNIQUE_RESOURCE)

    set(FORGE_NEEDS_EXPERIMENTAL FALSE)
    if(NOT HAS_STD_UNIQUE_RESOURCE)
        set(FORGE_NEEDS_EXPERIMENTAL TRUE)
        message(STATUS "CC Forge: std::unique_resource backport enabled (TS v3)")
    endif()

    # std::simd (P1928). The C++26 <simd> header is brand new; toolchains before
    # native support only ship <experimental/simd>, so __has_include(<simd>)
    # succeeding is itself the signal that a native C++26 simd has arrived.
    check_cxx_source_compiles("
        #include <simd>
        int main() {
            std::simd::vec<float> v(1.0f);
            auto sum = std::simd::reduce(v);
            auto mask = v == v;
            return static_cast<int>(sum + std::simd::reduce_count(mask));
        }
    " FORGE_SIMD_FULL)
    check_cxx_source_compiles("
        #if !defined(__has_include) || !__has_include(<simd>)
        #error no native <simd>
        #endif
        int main() { return 0; }
    " FORGE_SIMD_PARTIAL)
    _forge_decide("std::simd" SIMD FORGE_SIMD_FULL FORGE_SIMD_PARTIAL)

    # P2300 senders/receivers under <execution>. <execution> ALWAYS exists (C++17
    # parallel policies), so it is not a discriminator — probe for a P2300 symbol.
    check_cxx_source_compiles("
        #include <execution>
        #include <tuple>
        int main() {
            auto s = std::execution::just(1);
            auto r = std::execution::sync_wait(s);
            return r ? std::get<0>(*r) : 0;
        }
    " FORGE_SENDERS_FULL)
    check_cxx_source_compiles("
        #include <execution>
        int main() {
            auto s = std::execution::just(1);
            (void)s;
            return 0;
        }
    " FORGE_SENDERS_PARTIAL)
    _forge_decide("std::execution (P2300 senders/receivers)" SENDERS FORGE_SENDERS_FULL FORGE_SENDERS_PARTIAL)

    # std::submdspan (P2630) — only meaningful when <mdspan> exists. The partial
    # probe must look for a submdspan-SPECIFIC symbol: <mdspan> alone (e.g. GCC 14)
    # has no submdspan and must still get the backport, so __has_include(<mdspan>)
    # is NOT sufficient. std::strided_slice is part of the submdspan addition.
    check_cxx_source_compiles("
        #include <mdspan>
        int main() {
            int data[12]{};
            std::mdspan<int, std::extents<int, 3, 4>> m(data);
            auto sub = std::submdspan(m, 1, std::full_extent);
            (void)sub;
            return 0;
        }
    " FORGE_SUBMDSPAN_FULL)
    check_cxx_source_compiles("
        #include <mdspan>
        using probe = std::strided_slice<int, int, int>;
        int main() { (void)sizeof(probe); return 0; }
    " FORGE_SUBMDSPAN_PARTIAL)
    _forge_decide("std::submdspan" SUBMDSPAN FORGE_SUBMDSPAN_FULL FORGE_SUBMDSPAN_PARTIAL)

    # std::linalg (P1673) — new <linalg> header, so its mere presence is the
    # native signal.
    check_cxx_source_compiles("
        #include <linalg>
        int main() {
            double data[4] = {1,2,3,4};
            std::mdspan<double, std::extents<int,4>> v(data);
            return static_cast<int>(std::linalg::vector_two_norm(v));
        }
    " FORGE_LINALG_FULL)
    check_cxx_source_compiles("
        #if !defined(__has_include) || !__has_include(<linalg>)
        #error no native <linalg>
        #endif
        int main() { return 0; }
    " FORGE_LINALG_PARTIAL)
    _forge_decide("std::linalg" LINALG FORGE_LINALG_FULL FORGE_LINALG_PARTIAL)

    set(CMAKE_REQUIRED_FLAGS "${_forge_saved_required_flags}")

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
                message(FATAL_ERROR "CC Forge: failed to locate MSVC standard library header <memory>")
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

            set(FORGE_MSVC_EXECUTION_HEADER "")

            foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
                if(EXISTS "${_forge_include_dir}/execution")
                    file(TO_CMAKE_PATH "${_forge_include_dir}/execution" FORGE_MSVC_EXECUTION_HEADER)
                    break()
                endif()
            endforeach()

            if(NOT FORGE_MSVC_EXECUTION_HEADER)
                message(FATAL_ERROR "CC Forge: failed to locate MSVC standard library header <execution>")
            endif()

            target_compile_definitions(forge INTERFACE
                FORGE_MSVC_EXECUTION_HEADER=\"${FORGE_MSVC_EXECUTION_HEADER}\"
            )

            # MSVC: locate <mdspan> for the submdspan backport wrapper
            set(FORGE_MSVC_MDSPAN_HEADER "")

            foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
                if(EXISTS "${_forge_include_dir}/mdspan")
                    file(TO_CMAKE_PATH "${_forge_include_dir}/mdspan" FORGE_MSVC_MDSPAN_HEADER)
                    break()
                endif()
            endforeach()

            if(FORGE_MSVC_MDSPAN_HEADER)
                target_compile_definitions(forge INTERFACE
                    FORGE_MSVC_MDSPAN_HEADER=\"${FORGE_MSVC_MDSPAN_HEADER}\"
                )
            endif()
        endif()

        target_include_directories(forge BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>
        )
    endif()

    # Add the backport root for experimental TS headers. Do not add
    # backport/experimental directly: that would make <memory> resolve to
    # backport/experimental/memory instead of the standard-header wrapper.
    if(FORGE_NEEDS_EXPERIMENTAL)
        target_include_directories(forge BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>
        )
    endif()

    message(STATUS "CC Forge library configured: ${FORGE_INCLUDE_DIR}")
endif()
