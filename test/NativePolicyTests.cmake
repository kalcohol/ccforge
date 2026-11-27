# Native execution policy modes of the test suite.
#
# The library decides per translation unit (backport/forge_native_execution.hpp).
# The tests need the answer at configure time as well, to choose what to
# register, so this file measures it independently of the wrapper:
#   DEFAULT       the build as configured;
#   EXPERIMENTAL  libc++ with -fexperimental-library on compile and link,
#                 applied to dedicated test targets only.
# Each mode links the four native names from the system <execution>, without
# the shadow include root, and then requires the wrapper to agree.
#
# Scope: like every Forge configure probe, the measurement sees the compiler,
# CMAKE_CXX_FLAGS and the toolchain file. A flag that changes the policy mode
# and reaches the test targets another way (a per-configuration flag variable,
# or directory options of an enclosing project) is not supported by the test
# suite. It cannot pass unnoticed: native_policy_config.hpp stops the build of
# every policy test target whose own translation unit disagrees.

set(FORGE_TEST_POLICY_SUPPORT "${CMAKE_CURRENT_LIST_DIR}/support/execution")
set(FORGE_TEST_EXPERIMENTAL_POLICY_FLAG -fexperimental-library)
set(FORGE_TEST_EXPERIMENTAL_POLICIES OFF)

# The supported experimental configuration is libc++'s documented one: compile
# and link with the flag, which links libc++experimental. clang++ adds the
# archive itself. zig c++ accepts the flag but ships no such archive, so name
# it there: the configuration is then unavailable on Zig and skips visibly.
set(_forge_policy_experimental_libraries "")
get_filename_component(_forge_policy_driver "${CMAKE_CXX_COMPILER}" NAME)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Zig" OR _forge_policy_driver MATCHES "^zig")
    set(_forge_policy_experimental_libraries c++experimental)
endif()
unset(_forge_policy_driver)

# Prepare a linked probe in the given mode. Once the mode has been measured,
# the probe also receives its expectations, like forge_policy_test_target.
function(forge_prepare_policy_probe mode)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE PARENT_SCOPE)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${FORGE_TEST_POLICY_SUPPORT}")
    if(mode STREQUAL "EXPERIMENTAL")
        # CMAKE_REQUIRED_FLAGS reaches both the compile and the link line.
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG}" PARENT_SCOPE)
        list(APPEND CMAKE_REQUIRED_LIBRARIES ${_forge_policy_experimental_libraries})
    endif()
    list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        list(APPEND CMAKE_REQUIRED_LIBRARIES "${FORGE_TEST_TBB_LIBRARY}")
    endif()
    if(DEFINED FORGE_TEST_${mode}_POLICY_OBJECTS)
        list(APPEND CMAKE_REQUIRED_DEFINITIONS
            -DFORGE_TEST_EXPECT_POLICY_OBJECTS=${FORGE_TEST_${mode}_POLICY_OBJECTS}
            -DFORGE_TEST_EXPECT_POLICY_UNSEQ=${FORGE_TEST_${mode}_POLICY_UNSEQ})
    endif()
    foreach(_input IN ITEMS DEFINITIONS LIBRARIES INCLUDES)
        set(CMAKE_REQUIRED_${_input} "${CMAKE_REQUIRED_${_input}}" PARENT_SCOPE)
    endforeach()
endfunction()

# Native oracle: the system header only, with neither library interface's
# include roots or definitions. The result is 1 or 0.
function(_forge_probe_native_policy result mode)
    forge_prepare_policy_probe(${mode})
    set(CMAKE_REQUIRED_INCLUDES "")
    set(CMAKE_REQUIRED_DEFINITIONS ${ARGN})
    file(READ "${FORGE_TEST_POLICY_SUPPORT}/native_policy_objects.cpp" _source)
    unset(${result} CACHE)
    _forge_check_native_cxx_source_compiles("${_source}" ${result})
    if(${result})
        set(${result} 1 PARENT_SCOPE)
    else()
        set(${result} 0 PARENT_SCOPE)
    endif()
endfunction()

# OBJECTS means that seq, par and par_unseq all link. A library that links only
# some of them counts as having none, and the wrapper must then say so too.
function(forge_measure_native_policies mode)
    set(_objects 1)
    foreach(_policy IN ITEMS seq par par_unseq unseq)
        _forge_probe_native_policy(FORGE_TEST_${mode}_NATIVE_${_policy} ${mode}
            -DFORGE_TEST_NATIVE_POLICY_NAME=${_policy})
        if(NOT _policy STREQUAL "unseq" AND NOT FORGE_TEST_${mode}_NATIVE_${_policy})
            set(_objects 0)
        endif()
    endforeach()
    set(_unseq 0)
    set(_copyable 0)
    if(_objects)
        set(_unseq ${FORGE_TEST_${mode}_NATIVE_unseq})
        _forge_probe_native_policy(FORGE_TEST_${mode}_NATIVE_PAR_COPYABLE ${mode}
            -DFORGE_TEST_NATIVE_POLICY_NAME=par -DFORGE_TEST_NATIVE_POLICY_COPYABLE=1)
        set(_copyable ${FORGE_TEST_${mode}_NATIVE_PAR_COPYABLE})
    endif()
    message(STATUS "Forge: native execution policies (${mode}): objects=${_objects} unseq=${_unseq} copyable=${_copyable}")
    set(FORGE_TEST_${mode}_POLICY_OBJECTS ${_objects} PARENT_SCOPE)
    set(FORGE_TEST_${mode}_POLICY_UNSEQ ${_unseq} PARENT_SCOPE)
    set(FORGE_TEST_${mode}_POLICY_COPYABLE ${_copyable} PARENT_SCOPE)
endfunction()

# Without a backport there is no wrapper to check. <forge/bulk.hpp> then uses
# its feature-test fallback, which the Forge tests check under OWNER FORGE.
function(forge_check_policy_gate mode)
    if(NOT FORGE_NEEDS_BACKPORT)
        return()
    endif()
    forge_prepare_policy_probe(${mode})
    forge_expect_cxx_source_compiles(FORGE_TEST_${mode}_POLICY_GATE
        "${FORGE_TEST_POLICY_SUPPORT}/native_policy_gate.cpp" TRUE
        "forge_native_execution.hpp disagrees with the native policy names that link (${mode})" OWNER STD)
endfunction()

# Give a test target its mode. Every policy test target gets the expectations,
# and its sources include native_policy_config.hpp to compare them with the
# header's decision in that translation unit.
function(forge_policy_test_target target mode)
    target_include_directories(${target} PRIVATE "${FORGE_TEST_POLICY_SUPPORT}")
    target_compile_definitions(${target} PRIVATE
        FORGE_TEST_EXPECT_POLICY_OBJECTS=${FORGE_TEST_${mode}_POLICY_OBJECTS}
        FORGE_TEST_EXPECT_POLICY_UNSEQ=${FORGE_TEST_${mode}_POLICY_UNSEQ}
        FORGE_TEST_EXPECT_POLICY_COPYABLE=${FORGE_TEST_${mode}_POLICY_COPYABLE})
    if(mode STREQUAL "EXPERIMENTAL")
        target_compile_options(${target} PRIVATE ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG})
        target_link_options(${target} PRIVATE ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG})
        if(_forge_policy_experimental_libraries)
            target_link_libraries(${target} PRIVATE ${_forge_policy_experimental_libraries})
        endif()
    endif()
endfunction()

# The flagged configuration names its CTests here whenever it is enabled. Each
# must be registered, and its target (test_<name>, without a _compile suffix)
# must really carry the flag, so a default-mode build cannot stand in for it.
function(forge_require_experimental_policy_tests directory)
    get_property(_subdirectories DIRECTORY PROPERTY SUBDIRECTORIES)
    if(NOT directory IN_LIST _subdirectories)
        message(FATAL_ERROR "Forge: required experimental policy CTests are missing from ${directory}: ${ARGN}")
    endif()
    get_property(_tests DIRECTORY "${directory}" PROPERTY TESTS)
    foreach(_test IN LISTS ARGN)
        if(NOT _test IN_LIST _tests)
            message(FATAL_ERROR "Forge: required experimental policy CTest is missing: ${_test}")
        endif()
        string(REGEX REPLACE "_compile$" "" _target "test_${_test}")
        foreach(_property IN ITEMS COMPILE_OPTIONS LINK_OPTIONS)
            get_target_property(_options ${_target} ${_property})
            if(NOT FORGE_TEST_EXPERIMENTAL_POLICY_FLAG IN_LIST _options)
                message(FATAL_ERROR "Forge: ${_target} must set ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG} in ${_property}")
            endif()
        endforeach()
    endforeach()
endfunction()

set(CMAKE_REQUIRED_INCLUDES "")
forge_measure_native_policies(DEFAULT)
forge_check_policy_gate(DEFAULT)
if(FORGE_NEEDS_BACKPORT)
    # Build the same comparison as an ordinary default-mode target, whatever
    # was measured and whichever test groups are selected. It sees the real
    # target flags, so a policy mode outside the supported scope stops the
    # build even when no other default-mode policy target is registered.
    add_executable(test_native_policy_gate "${FORGE_TEST_POLICY_SUPPORT}/native_policy_gate.cpp")
    target_link_libraries(test_native_policy_gate PRIVATE forge::std Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        # Like the probe above and the test helpers: libstdc++'s policy
        # header can need TBB at link time.
        target_link_libraries(test_native_policy_gate PRIVATE "${FORGE_TEST_TBB_LIBRARY}")
    endif()
    forge_policy_test_target(test_native_policy_gate DEFAULT)
    add_test(NAME execution_native_policy_gate COMMAND test_native_policy_gate)
    set_tests_properties(execution_native_policy_gate PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()
if(FORGE_TEST_REQUIRE_NATIVE_POLICIES
        AND NOT (FORGE_TEST_DEFAULT_POLICY_OBJECTS AND FORGE_TEST_DEFAULT_POLICY_UNSEQ))
    message(FATAL_ERROR "Forge: this lane requires all four native execution policy objects in the default mode")
endif()

# The flagged mode exists on libc++ only. <version> identifies the library
# even where it has no usable <execution>.
set(_forge_policy_libcpp OFF)
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang|Zig)$")
    unset(FORGE_TEST_NATIVE_LIBCPP CACHE)
    _forge_check_native_cxx_source_compiles([=[
        #include <version>
        #ifndef _LIBCPP_VERSION
        #error Not libc++
        #endif
        int main() {}
    ]=] FORGE_TEST_NATIVE_LIBCPP)
    set(_forge_policy_libcpp "${FORGE_TEST_NATIVE_LIBCPP}")
endif()
if(_forge_policy_libcpp)
    forge_measure_native_policies(EXPERIMENTAL)
    # The flagged mode qualifies all four objects, so unseq is required too.
    # The library is C++23, where libc++ declares unseq with the others.
    if(FORGE_TEST_EXPERIMENTAL_POLICY_OBJECTS AND FORGE_TEST_EXPERIMENTAL_POLICY_UNSEQ)
        set(FORGE_TEST_EXPERIMENTAL_POLICIES ON)
        forge_check_policy_gate(EXPERIMENTAL)
    else()
        if(FORGE_TEST_EXPERIMENTAL_POLICY_OBJECTS)
            set(_reason "std::execution::unseq does not link with ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG}, and the flagged tests cover all four policy objects")
        elseif(_forge_policy_experimental_libraries)
            set(_reason "the supported configuration links libc++experimental with ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG}, and Zig ships no such archive")
        else()
            set(_reason "the native libc++ policy objects do not link with ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG} (see the CMake configure log)")
        endif()
        message(STATUS "Forge: libc++ experimental execution policy tests skipped (${_reason})")
        add_test(NAME execution_policy_experimental_unavailable
            COMMAND ${CMAKE_COMMAND} -E echo "SKIP: ${_reason}")
        set_tests_properties(execution_policy_experimental_unavailable PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
        unset(_reason)
    endif()
endif()
if(FORGE_TEST_REQUIRE_LIBCPP_EXPERIMENTAL_POLICIES AND NOT FORGE_TEST_EXPERIMENTAL_POLICIES)
    message(FATAL_ERROR
        "Forge: this lane requires libc++ and all four native execution policy objects to link with ${FORGE_TEST_EXPERIMENTAL_POLICY_FLAG}")
endif()
unset(_forge_policy_libcpp)
