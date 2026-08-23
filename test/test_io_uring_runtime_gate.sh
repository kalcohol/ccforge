#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runner="${repo_root}/scripts/run-io-uring-runtime-gate.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/ccforge-io-uring-gate.XXXXXX")"
trap 'find "${work}" -depth -delete' EXIT

make_probe() {
    local name="$1"
    local status="$2"
    local path="${work}/${name}"

    printf '#!/usr/bin/env bash\nexit %s\n' "${status}" > "${path}"
    chmod +x "${path}"
}

make_probe probe-skip 77
make_probe probe-fail 23
make_probe probe-pass 0

printf '%s\n' '{"syscalls": []}' > "${work}/seccomp.json"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "%s\n" "$@" > "${FAKE_PODMAN_ARGS}"' \
    > "${work}/podman"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'shift' \
    'cp "$1" "$2"' \
    > "${work}/python"
chmod +x "${work}/podman" "${work}/python"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'printf "%s\n" "$*" >> "${FORGE_TIMEOUT_MARKER}"' \
    'shift 3' \
    'exec "$@"' \
    > "${work}/forge-timeout"
chmod +x "${work}/forge-timeout"
export FORGE_IO_URING_TIMEOUT_COMMAND="${work}/forge-timeout"
export FORGE_TIMEOUT_MARKER="${work}/timeout.calls"

FAKE_PODMAN_ARGS="${work}/podman.args" \
PODMAN="${work}/podman" \
FORGE_IO_URING_PYTHON="${work}/python" \
FORGE_IO_URING_SECCOMP_BASE="${work}/seccomp.json" \
FORGE_IO_URING_PROBE_BUILD_DIR="${work}/container-probe" \
FORGE_IO_URING_PROBE_IMAGE=fake-image \
FORGE_IO_URING_REQUIRE_RUNTIME=1 \
    "${repo_root}/scripts/probe-io-uring-container.sh"
mapfile -t podman_args < "${work}/podman.args"
podman_arg_count="${#podman_args[@]}"
[[ "${podman_args[podman_arg_count - 6]}" == bash ]]
[[ "${podman_args[podman_arg_count - 5]}" == \
    scripts/run-io-uring-runtime-gate.sh ]]
[[ "${podman_args[podman_arg_count - 2]}" == 1 ]]
[[ "${podman_args[podman_arg_count - 1]}" == scripts/probe-io-uring.sh ]]

ordinary_output="$(
    "${runner}" c++ 5 0 "${work}/probe-skip" 2>&1
)"
[[ "${ordinary_output}" == *'runtime unavailable; bounded skip'* ]]

strict_status=0
strict_output="$(
    "${runner}" c++ 5 1 "${work}/probe-skip" 2>&1
)" || strict_status=$?
[[ ${strict_status} -eq 1 ]]
[[ "${strict_output}" == *'runtime required but unavailable'* ]]

failure_status=0
"${runner}" c++ 5 0 "${work}/probe-fail" \
    >/dev/null 2>&1 || failure_status=$?
[[ ${failure_status} -eq 23 ]]

build_dir="${work}/build"
binary_dir="${build_dir}/test/forge"
marker_dir="${work}/markers"
mkdir -p "${binary_dir}" "${marker_dir}"

for name in \
        test_forge_io_uring_context \
        test_forge_io_uring_rw \
        test_forge_io_uring_fault; do
    printf '%s\n' \
        '#!/usr/bin/env bash' \
        'touch "${FORGE_IO_URING_TEST_MARKER_DIR}/'"${name}"'"' \
        > "${binary_dir}/${name}"
    chmod +x "${binary_dir}/${name}"
done

FORGE_IO_URING_TEST_MARKER_DIR="${marker_dir}" \
    "${runner}" c++ 5 1 "${work}/probe-pass" "${build_dir}" \
    >/dev/null

for name in \
        test_forge_io_uring_context \
        test_forge_io_uring_rw \
        test_forge_io_uring_fault; do
    [[ -e "${marker_dir}/${name}" ]]
done

[[ -s "${FORGE_TIMEOUT_MARKER}" ]]
