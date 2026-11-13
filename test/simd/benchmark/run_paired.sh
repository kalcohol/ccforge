#!/usr/bin/env bash
set -euo pipefail
# Read-only Git commands must not refresh the repository index on disk.
export GIT_OPTIONAL_LOCKS=0

if (( $# < 2 )); then
    echo 'usage: bash test/simd/benchmark/run_paired.sh CPU CXX [extra compiler flags...]' >&2
    exit 2
fi
cpu=$1
cxx=$2
shift 2
[[ $cpu =~ ^[0-9]+$ ]] || exit 2
base=${BASE_REF:-c37fd4b}
iterations=${ITERATIONS:-1000000}
pairs=${PAIRS:-10}
[[ $iterations =~ ^[1-9][0-9]*$ && $pairs =~ ^[1-9][0-9]*$ ]] || exit 2
root=$(git rev-parse --show-toplevel)
cd "$root"
# Keep snapshots and all generated output outside the repository.
out=$(mktemp -d /tmp/simd-benchmark.XXXXXXXX)
base_commit=$(git rev-parse --verify "${base}^{commit}")
mkdir -p "$out/base-source"
git archive "$base_commit" backport | tar -x -C "$out/base-source"
mkdir -p "$out/candidate-source"
# Copy filesystem contents, including staged, unstaged and untracked files.
# Dereference symlinks so compilation cannot read back into the live tree.
cp -RL "$root/backport" "$out/candidate-source/backport"
cp -RL "$root/test/simd/benchmark" "$out/harness"
(
    cd "$out"
    for tree in base-source candidate-source harness; do
        find "$tree" -type f -print0 | LC_ALL=C sort -z |
            xargs -0 sha256sum > "$tree.sha256"
    done
    sha256sum base-source.sha256 candidate-source.sha256 harness.sha256 > snapshots.sha256
)
git diff --binary "$base_commit" -- backport > "$out/branch.patch"
{
    date -u
    uname -a
    lscpu
    taskset -pc $$
    "$cxx" --version
    printf 'base_commit=%s\n' "$base_commit"
    git rev-parse HEAD
    git status --short
    printf 'cpu=%s iterations=%s pairs=%s\n' "$cpu" "$iterations" "$pairs"
    printf 'compiler=%q extra_flags=' "$cxx"
    printf '%q ' "$@"
    printf '\ncommon_flags=-std=c++23 -O3 -DNDEBUG\n'
    cat "$out/snapshots.sha256"
} > "$out/metadata.txt"
source_file="$out/harness/measure_simd.cpp"
"$cxx" -std=c++23 -O3 -DNDEBUG "$@" -I"$out/base-source/backport" "$source_file" -o "$out/base"
"$cxx" -std=c++23 -O3 -DNDEBUG "$@" -I"$out/candidate-source/backport" "$source_file" -o "$out/branch"
sha256sum "$source_file" "$out/base" "$out/branch" > "$out/sha256.txt"
for ((sample=1; sample<=pairs; ++sample)); do
    order=(base branch)
    if (( sample % 2 == 0 )); then order=(branch base); fi
    for candidate in "${order[@]}"; do
        taskset -c "$cpu" "$out/$candidate" "$iterations" |
            awk -v candidate="$candidate" -v sample="$sample" \
                '{ print $0 ",candidate=" candidate ",sample=" sample }' >> "$out/samples.csv"
    done
done
printf 'Results: %s\n' "$out"
