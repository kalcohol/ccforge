#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FORGE_IO_URING_PROBE_BUILD_DIR:-${repo_root}/build/probes-container}"
base_profile="${FORGE_IO_URING_SECCOMP_BASE:-/usr/share/containers/seccomp.json}"
profile="${build_dir}/io-uring-seccomp.json"
image="${FORGE_IO_URING_PROBE_IMAGE:-forge-tsan}"
compiler="${FORGE_IO_URING_CONTAINER_CXX:-clang++}"
podman="${PODMAN:-podman}"

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
    ' bash "${compiler}"
