#!/usr/bin/env bash
set -euo pipefail
# Read-only Git commands must not refresh the repository index on disk.
export GIT_OPTIONAL_LOCKS=0

if (( $# < 2 )); then
    echo 'usage: bash test/execution/benchmark/run_paired.sh CPU CXX [extra compiler flags...]' >&2
    exit 2
fi
cpu=$1
cxx=$2
shift 2
[[ $cpu =~ ^[0-9]+$ ]] || exit 2
pairs=${PAIRS:-15}
iterations=${ITERATIONS:-1000000}
std=${STD:-c++26}
[[ $iterations =~ ^[1-9][0-9]*$ && $pairs =~ ^[1-9][0-9]*$ ]] || exit 2
if (( iterations < 1000 )); then
    echo 'ITERATIONS must be at least 1000' >&2
    exit 2
fi
[[ $std =~ ^c\+\+[0-9a-z]+$ ]] || exit 2
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$script_dir/../../.." && pwd)
cd "$root"
if [[ -z ${BASE_BACKPORT:-} && -z ${BASE_REF:-} ]]; then
    echo 'set BASE_REF to a commit, or BASE_BACKPORT to a baseline backport directory' >&2
    exit 2
fi
candidate_backport=${CANDIDATE_BACKPORT:-$root/backport}
# Keep snapshots and all generated output outside the repository.
out=$(mktemp -d "${OUT_ROOT:-/tmp}/stop-benchmark.XXXXXXXX")
mkdir -p "$out/base-source" "$out/candidate-source"
if [[ -n ${BASE_BACKPORT:-} ]]; then
    base_label="directory $BASE_BACKPORT"
    cp -RL "$BASE_BACKPORT" "$out/base-source/backport"
else
    base_commit=$(git rev-parse --verify "${BASE_REF}^{commit}")
    base_label="commit $base_commit"
    git archive "$base_commit" backport | tar -x -C "$out/base-source"
fi
# The baseline must be self-contained: a symbolic link could point back into
# the live tree.
if [[ -n $(find "$out/base-source" -type l -print -quit) ]]; then
    echo 'baseline snapshot contains symbolic links' >&2
    exit 1
fi
# Copy filesystem contents, including staged, unstaged and untracked files.
# Dereference symlinks so compilation cannot read back into the live tree.
cp -RL "$candidate_backport" "$out/candidate-source/backport"
cp -RL "$script_dir" "$out/harness"
(
    cd "$out"
    for tree in base-source candidate-source harness; do
        find "$tree" -type f -print0 | LC_ALL=C sort -z |
            xargs -0 sha256sum > "$tree.sha256"
    done
    sha256sum base-source.sha256 candidate-source.sha256 harness.sha256 > snapshots.sha256
)
diff -ru "$out/base-source/backport" "$out/candidate-source/backport" > "$out/candidate.patch" || true
{
    date -u
    uname -a
    if command -v lscpu >/dev/null; then lscpu; fi
    taskset -pc $$
    "$cxx" --version
    printf 'base=%s\n' "$base_label"
    printf 'candidate=%s\n' "$candidate_backport"
    if command -v git >/dev/null && git rev-parse --git-dir >/dev/null 2>&1; then
        git rev-parse HEAD
        git status --short
    else
        echo 'git metadata unavailable'
    fi
    printf 'cpu=%s iterations=%s pairs=%s std=%s\n' "$cpu" "$iterations" "$pairs" "$std"
    printf 'compiler=%q extra_flags=' "$cxx"
    printf '%q ' "$@"
    printf '\ncommon_flags=-std=%s -O3 -DNDEBUG -pthread\n' "$std"
    cat "$out/snapshots.sha256"
} > "$out/metadata.txt"
source_file="$out/harness/measure_stop_token.cpp"
for variant in base candidate; do
    "$cxx" "-std=$std" -O3 -DNDEBUG "$@" -I"$out/$variant-source/backport" \
        "$source_file" -o "$out/$variant" -pthread
    if command -v objdump >/dev/null; then
        objdump -d -C --no-show-raw-insn "$out/$variant" > "$out/$variant.dis"
    fi
done
sha256sum "$source_file" "$out/base" "$out/candidate" > "$out/sha256.txt"
for ((sample=1; sample<=pairs; ++sample)); do
    order=(base candidate)
    if (( sample % 2 == 0 )); then order=(candidate base); fi
    for variant in "${order[@]}"; do
        taskset -c "$cpu" "$out/$variant" "$iterations" |
            awk -v variant="$variant" -v sample="$sample" \
                '{ print $0 ",candidate=" variant ",sample=" sample }' >> "$out/samples.csv"
    done
done
if command -v python3 >/dev/null; then
    python3 -I "$out/harness/summarize_pairs.py" "$out/samples.csv" > "$out/summary.txt"
    cat "$out/summary.txt"
fi
printf 'Results: %s\n' "$out"
