_forge_compute_probe_fingerprint(
    _forge_fingerprint 23
    "${CMAKE_CURRENT_LIST_DIR}/probe-fragment.cmake")
_forge_refresh_probe_cache(
    FORGE_B03_FINGERPRINT "${_forge_fingerprint}" FORGE_B03_PROBE)

check_cxx_source_compiles("
    #include <forge_b03_header.hpp>
    #include <forge_b03_flag_header.hpp>
    #include <version>
    static_assert(FORGE_B03_HEADER_VALUE == 1);
    static_assert(FORGE_B03_FLAG_HEADER_VALUE == 7);
    static_assert(FORGE_B03_SHADOWED_VERSION == 1);
    int main() { return 0; }
" FORGE_B03_PROBE)

if(FORGE_B03_PROBE)
    set(_forge_result TRUE)
else()
    set(_forge_result FALSE)
endif()
file(WRITE "${CMAKE_BINARY_DIR}/probe-result.txt"
    "result=${_forge_result}\n"
    "fingerprint=${FORGE_B03_FINGERPRINT}\n")
