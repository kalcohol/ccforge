#!/usr/bin/env bash
set -euo pipefail
# Build the same benchmark source against both header trees without checking out.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$root"
: "${CPU:?Set CPU to one available logical CPU from taskset -pc $$}"
CXX=${CXX:-g++}
BASE_REF=${BASE_REF:-c37fd4b}
SAMPLES=${SAMPLES:-5}
ROUNDS=${ROUNDS:-5}
SIZE=${SIZE:-0}
mkdir -p build/linalg-benchmark
out=$(mktemp -d "$root/build/linalg-benchmark/run-XXXXXXXX")
printf 'Run directory: %s\n' "$out"
mkdir -p "$out/base" "$out/branch"
# Save provenance and all source inputs before invoking the compiler. The branch
# snapshot includes tracked working files (staged or unstaged) and untracked
# headers/sources, and excludes tracked files deleted from the working tree.
base_commit=$(git rev-parse "$BASE_REF^{commit}")
git rev-parse HEAD > "$out/refs.txt"
printf '%s\n' "$base_commit" >> "$out/refs.txt"
git diff HEAD --binary > "$out/working-tree.patch"
git status --short > "$out/working-tree-status.txt"
git ls-files --cached --others --exclude-standard -z -- \
    backport include test/linalg/benchmark > "$out/input-paths.nul"
while IFS= read -r -d '' path; do
    if [[ -f "$path" || -L "$path" ]]; then
        cp --parents -- "$path" "$out/branch/"
    fi
done < "$out/input-paths.nul"
git archive "$base_commit" backport include | tar -x -C "$out/base"
(
    cd "$out"
    find base branch -type f -print0 | sort -z | xargs -0 sha256sum > inputs.sha256
)
printf 'CPU=%s\nCXX=%s\nSIZE=%s\nSAMPLES=%s\nROUNDS=%s\n' \
    "$CPU" "$CXX" "$SIZE" "$SAMPLES" "$ROUNDS" > "$out/settings.txt"
"$CXX" --version > "$out/compiler.txt"
source_file="$out/branch/test/linalg/benchmark/measure_products.cpp"
"$CXX" -std=c++23 -O3 -DNDEBUG -ffp-contract=off -I"$out/base/backport" -I"$out/base/include" \
    "$source_file" -o "$out/measure_base"
"$CXX" -std=c++23 -O3 -DNDEBUG -ffp-contract=off -I"$out/branch/backport" -I"$out/branch/include" \
    "$source_file" -o "$out/measure_branch"
sha256sum "$out/measure_base" "$out/measure_branch" > "$out/binaries.sha256"
# Alternate AB/BA across rounds; retain every raw sample in separate JSONL files.
for ((round=0; round<ROUNDS; ++round)); do
    labels=(base branch)
    if ((round % 2)); then labels=(branch base); fi
    for label in "${labels[@]}"; do
        taskset -c "$CPU" "$out/measure_$label" "$SIZE" "$SAMPLES" \
            > "$out/round-$round-$label.jsonl"
    done
done
