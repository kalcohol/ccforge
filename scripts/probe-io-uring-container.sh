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
# after the raw probe. Skips are hard failures here: the probe just proved
# the environment supports io_uring, so a skipping test would be a bug.
test_build_dirs="${FORGE_IO_URING_TEST_BUILD_DIRS:-}"

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

# shellcheck disable=SC2086  # test_build_dirs is intentionally word-split
"${podman}" run --rm \
    --userns=keep-id \
    --cap-drop=all \
    --network=none \
    --security-opt "seccomp=${profile}" \
    --security-opt label=disable \
    --security-opt no-new-privileges \
    -v "${repo_root}:/src:ro,Z" \
    -w /src \
    "${image}" \
    bash -lc '
        set -euo pipefail
        CXX="$1" \
            FORGE_IO_URING_PROBE_BUILD_DIR=/tmp/ccforge-io-uring-probe \
            scripts/probe-io-uring.sh
        shift
        for dir in "$@"; do
            for name in test_forge_io_uring_context test_forge_io_uring_rw; do
                binary="${dir}/test/forge/${name}"
                if [[ ! -x "${binary}" ]]; then
                    printf "missing io_uring test binary: %s\n" \
                        "${binary}" >&2
                    exit 3
                fi
                printf "[io-uring-container] running %s\n" "${binary}"
                "${binary}"
            done
        done
    ' bash "${compiler}" ${test_build_dirs}
