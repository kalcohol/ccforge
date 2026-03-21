# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language Policy

- Documentation and conversation: Chinese (Simplified)
- Code and comments: English only
- File encoding: UTF-8 without BOM (C++23 compliant)

## Project Overview

Forge is a header-only C++ library providing transparent C++26 backports. When `#include <memory>` or `#include <simd>` is used with Forge's include path, the backport wrappers detect whether the compiler already provides C++26 features and inject polyfills only when needed.

Current backports:
- `std::unique_resource` (P0052R15) — in `backport/cpp26/unique_resource.hpp`
- `std::simd` (P1928) — in `backport/cpp26/simd.hpp`

The `include/forge/` directory contains original extensions (not backports), e.g. `res_guard.hpp`.

## Core Design Principle — Seamless Transition

Backport implementations MUST be fully transparent to downstream users. When the standard library eventually ships the feature natively (e.g. via a newer zig/LLVM toolchain), downstream code MUST compile and work without ANY modifications — no code changes, no macro changes, no build flag changes. This means:
- Backport API surface must match the final standard exactly (no extra constructors, no extra factory functions, no extensions)
- All symbols must live in `namespace std` with identical signatures to the standard
- The injection mechanism (`forge.cmake`) auto-detects native support and disables the backport transparently

## Standard Reference Policy

Standard conformance discussions in this repository MUST use a strict reference hierarchy. Previous review confusion showed that loose citation rules lead directly to design mistakes and incorrect accept/reject decisions.

### Normative Baseline Rules

- For any claim about what the standard **currently requires**, the primary source is the **current working draft** on `eel.is` (for example `https://eel.is/c++draft/simd`).
- WG21 proposal papers (`PxxxxRy`) are **historical context by default**, not the normative baseline, unless the task explicitly says to target that exact revision.
- Never use an older proposal revision to refute a finding that was raised against the current working draft.
- If a review/report states its baseline explicitly (for example “current draft [simd]”), all replies and follow-up reviews MUST keep that same baseline unless they explicitly reopen the baseline question first.
- If the current draft and an older proposal disagree, the current draft wins for all design and review decisions in this repo unless the user explicitly asks for historical analysis.

### Citation Requirements

- Every standards-based review or review-response MUST name its baseline explicitly near the top of the document.
- When citing a rule, prefer the section name/anchor from the current draft (for example `[simd]`, `[simd.ctor]`, `[simd.permute.memory]`) and include the draft URL when practical.
- If a proposal paper is cited, include the exact revision (`P1928R9`, `P1928R15`, etc.) and state whether it is:
  - current normative baseline
  - historical background
  - implementation history
- Do not write “the standard requires” when the only source checked is an old paper revision.
- Do not write “this is standard-conforming” or “this is not required” unless the cited baseline actually supports that conclusion.

### Review and Response Discipline

- Separate these four things explicitly when needed:
  - current normative requirement
  - historical wording/proposal context
  - local implementation status
  - local design choice
- A review response must answer the original review on the review's stated baseline before offering historical context.
- If a response believes the original baseline is wrong, it must say so explicitly and justify changing the baseline before using different citations.
- If wording changed across revisions and the change matters, call that out explicitly instead of silently switching references.
- If exact wording was not verified, say so and avoid decisive accept/reject conclusions.

### Review File Timeline Discipline

- Review response files MUST use monotonically increasing answer suffixes: `...-a0.md`, `...-a1.md`, `...-a2.md`, etc.
- Once a later answer file exists, do not go back and rewrite an older answer file in that chain.
- Any follow-up correction, clarification, or changed conclusion MUST be written into the next answer number, even if the older file was wrong.
- Do not collapse or overwrite answer history; the file sequence is part of the review timeline and must remain auditably intact.

### Engineering Consequences

- Do not design APIs, constraints, overload sets, or tests from memory when standard wording details matter.
- For backports, constraints and overload participation rules are part of the public contract; wording-level mistakes are design bugs, not documentation nits.
- When a standards question materially affects code shape, browse and verify the current draft first, then implement.
- Prefer adding compile probes/tests that lock in the verified standard behavior so future review cycles do not drift.

## Build Commands

```bash
# Configure (out-of-source build)
cmake -B build -DCMAKE_CXX_STANDARD=23

# Build everything (examples + tests)
cmake --build build

# Run all tests
ctest --test-dir build

# Run a single test by name
ctest --test-dir build -R unique_resource_runtime

# Build only (skip examples or tests)
cmake -B build -DFORGE_BUILD_EXAMPLES=OFF -DFORGE_BUILD_TESTS=OFF
```

Google Test is included as a git submodule in `3rdparty/googletest/`. Run `git submodule update --init` if missing.

## Verification Policy (GCC + Zig + Podman)

When a change touches backports (`backport/`) or their tests/examples, validate with all of:

1. Local GCC (default toolchain)
```bash
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-gcc -j
ctest --test-dir build-gcc --output-on-failure
```

2. Local Zig toolchain (`zig c++` / `zig cc`)
```bash
CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-zig -j
ctest --test-dir build-zig --output-on-failure
```

3. Podman containers (use keep-id; do NOT force C++ standard)
```bash
podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc \
  'cmake -S . -B build-podman-clang -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-podman-clang -j && ctest --test-dir build-podman-clang --output-on-failure'

podman run --rm --userns=keep-id --user 0 -v "$PWD:/work:ro,Z" -w /work docker.io/gcc:latest bash -lc \
  'apt-get update -qq && apt-get install -y --no-install-recommends -qq cmake ninja-build >/dev/null && cmake -S /work -B /tmp/build-gcc -G Ninja -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build /tmp/build-gcc -j && ctest --test-dir /tmp/build-gcc --output-on-failure'
```

## Architecture

### Backport Injection Mechanism

`forge.cmake` is the core configuration. It:
1. Runs `check_cxx_source_compiles()` to detect native `std::unique_resource` and `std::simd`
2. If missing, prepends `backport/` to the include path via `target_include_directories(BEFORE)`
3. The wrapper headers (`backport/memory`, `backport/simd`) include the real standard header first, then conditionally include the backport implementation

On MSVC, the real standard header path is resolved via environment variables and passed as compile definitions (`FORGE_MSVC_MEMORY_HEADER`, `FORGE_MSVC_SIMD_HEADER`).

### Test Structure

Two kinds of tests in `test/`:
- **Runtime tests** — linked with `gtest_main`, run as normal executables (e.g. `test_simd.cpp`, `test_unique_resource.cpp`)
- **Compile probes** — built with `EXCLUDE_FROM_ALL`, tested via `cmake --build --target <name>`. These verify API surface and compile-time semantics without running

Test names in CTest don't always match filenames. Check `test/CMakeLists.txt` for the mapping.

### Integration as Subproject

Consumers use either:
```cmake
include(/path/to/forge/forge.cmake)
# or
add_subdirectory(forge)
```
Then link with `forge::forge`. When used as a subproject, examples and tests are not built.

## Collaboration Policy (Sub-Agents First)

When the user explicitly requests delegation, prefer spawning sub-agents for most work.

- Main agent responsibilities:
  - Clarify goals and acceptance criteria
  - Split the work into small, well-scoped subtasks with disjoint file ownership
  - Dispatch tasks to sub-agents and integrate results
  - Only do critical-path work locally when coordination/integration is required

- Sub-agent responsibilities:
  - Plan, implement, and run tests for their assigned subtask
  - Minimize overlap with other agents and avoid reverting others' edits
  - Report changed files and verification steps clearly
