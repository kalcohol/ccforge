# MSVC wrapper headers need the real standard-library header path because the
# backport headers shadow names such as <memory>, <utility>, and <execution>.

function(_forge_find_msvc_standard_header out_var header_name)
    set(_forge_msvc_include_candidates ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})

    if(DEFINED ENV{INCLUDE})
        string(REPLACE ";" ";" _forge_env_include "$ENV{INCLUDE}")
        list(APPEND _forge_msvc_include_candidates ${_forge_env_include})
    endif()

    if(DEFINED ENV{VCToolsInstallDir})
        list(APPEND _forge_msvc_include_candidates "$ENV{VCToolsInstallDir}/include")
    endif()

    foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
        if(EXISTS "${_forge_include_dir}/${header_name}")
            file(TO_CMAKE_PATH "${_forge_include_dir}/${header_name}" _forge_header_path)
            set(${out_var} "${_forge_header_path}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(_forge_define_msvc_standard_header target_name macro_name header_name required)
    _forge_find_msvc_standard_header(_forge_header_path "${header_name}")

    if(NOT _forge_header_path)
        if(required)
            message(FATAL_ERROR "CC Forge: failed to locate MSVC standard library header <${header_name}>")
        endif()
        return()
    endif()

    target_compile_definitions(${target_name} INTERFACE
        ${macro_name}=\"${_forge_header_path}\"
    )
endfunction()
