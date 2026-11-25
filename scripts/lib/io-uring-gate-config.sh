# Shared reader for the top-level CMake configuration marker.
forge_io_uring_config_has() {
    local dir="$1"
    local setting="$2"
    local marker="${dir}/ccforge-io-uring-gate.txt"
    [[ -r "${marker}" ]] && grep -Fxq "${setting}" "${marker}"
}
