# Check actual native policy objects with the same include root and MSVC
# native-header definition as consumers. A header/trait alone is insufficient.
function(forge_linalg_check_policy_objects result extra_flags)
    if(FORGE_NEEDS_BACKPORT)
        # Replace caller paths so they cannot supply a different <execution>.
        set(CMAKE_REQUIRED_INCLUDES "${FORGE_BACKPORT_DIR}")
    endif()
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${extra_flags}")
    if(FORGE_TEST_TBB_LIBRARY)
        list(APPEND CMAKE_REQUIRED_LIBRARIES ${FORGE_TEST_TBB_LIBRARY})
    endif()
    get_target_property(_defs forge_std INTERFACE_COMPILE_DEFINITIONS)
    if(_defs)
        foreach(_def IN LISTS _defs)
            list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_def}")
        endforeach()
    endif()
    set(_libcpp_requirement "")
    if(extra_flags)
        set(_libcpp_requirement "#ifndef _LIBCPP_VERSION\n#error Requires libc++\n#endif")
    endif()
    unset(${result} CACHE)
    _forge_check_native_cxx_source_compiles("
        #include <forge_native_execution.hpp>
        ${_libcpp_requirement}
        #if !FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
        #error Native policy objects unavailable
        #endif
        int main() {
            static_assert(std::is_execution_policy_v<decltype(std::execution::sequenced_policy{})>);
            (void)std::execution::seq;
            (void)std::execution::par;
            (void)std::execution::par_unseq;
        }
    " ${result})
    set(${result} "${${result}}" PARENT_SCOPE)
endfunction()
