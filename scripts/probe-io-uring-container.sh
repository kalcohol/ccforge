#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FORGE_IO_URING_PROBE_BUILD_DIR:-${repo_root}/build/probes-container}"
base_profile="${FORGE_IO_URING_SECCOMP_BASE:-/usr/share/containers/seccomp.json}"
profile="${build_dir}/io-uring-seccomp.json"
image="${FORGE_IO_URING_PROBE_IMAGE:-forge-tsan}"
compiler="${FORGE_IO_URING_CONTAINER_CXX:-clang++}"
podman="${PODMAN:-podman}"
# Optional space-separated list of build directories (relative to the repo
# root) whose io_uring runtime tests run inside the allow-io_uring container
# after the raw probe. A raw-probe 77 is a bounded skip for kernels that
# disable io_uring unless FORGE_IO_URING_REQUIRE_RUNTIME=1; once the probe
# succeeds, a runtime-test skip is a bug.
test_build_dirs="${FORGE_IO_URING_TEST_BUILD_DIRS:-}"
test_timeout_seconds="${FORGE_IO_URING_TEST_TIMEOUT_SECONDS:-120}"
allow_ptrace="${FORGE_IO_URING_ALLOW_PTRACE:-0}"
require_runtime="${FORGE_IO_URING_REQUIRE_RUNTIME:-0}"

if [[ ! -r "${base_profile}" ]]; then
    printf 'base seccomp profile is not readable: %s\n' "${base_profile}" >&2
    exit 2
fi

cmake -E make_directory "${build_dir}"
python3 - "${base_profile}" "${profile}" <<'PY'
import json
import sys

source, destination = sys.argv[1:]
with open(source, encoding="utf-8") as input_file:
    profile = json.load(input_file)

rules = profile.get("syscalls")
if not isinstance(rules, list):
    raise SystemExit("base seccomp profile has no syscall rule list")

rules.append(
    {
        "names": [
            "io_uring_enter",
            "io_uring_register",
            "io_uring_setup",
        ],
        "action": "SCMP_ACT_ALLOW",
        "args": [],
        "comment": "Allow io_uring only for the ccforge backend probe",
        "includes": {},
        "excludes": {},
    }
)

with open(destination, "w", encoding="utf-8", newline="\n") as output_file:
    json.dump(profile, output_file, indent=2)
    output_file.write("\n")
PY

container_args=(
    run --rm
    --userns=keep-id
    --cap-drop=all
    --network=none
    --security-opt "seccomp=${profile}"
    --security-opt label=disable
    --security-opt no-new-privileges
    -v "${repo_root}:/src:ro,Z"
    -w /src
)
if [[ "${allow_ptrace}" == "1" ]]; then
    container_args+=(--cap-add=SYS_PTRACE)
fi
for variable in ASAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS; do
    if [[ -n "${!variable+x}" ]]; then
        container_args+=(--env "${variable}=${!variable}")
    fi
done
container_args+=("${image}")

test_build_dir_args=()
if [[ -n "${test_build_dirs}" ]]; then
    # Build directories are a space-separated list by contract.
    read -r -a test_build_dir_args <<< "${test_build_dirs}"
fi

"${podman}" "${container_args[@]}" \
    bash scripts/run-io-uring-runtime-gate.sh \
        "${compiler}" \
        "${test_timeout_seconds}" \
        "${require_runtime}" \
        scripts/probe-io-uring.sh \
        "${test_build_dir_args[@]}"
