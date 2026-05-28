# Agent Notes

## Repo Shape
- Header-only C++23 library; the public CMake target is `forge::forge` from `forge.cmake`.
- Standard backports live under `backport/` and intentionally shadow standard headers (`<execution>`, `<simd>`, `<linalg>`, `<mdspan>`, `<memory>`). Extensions that are not standard belong under `include/forge/`, not `namespace std`.
- `forge.cmake` is the executable source of truth for native-vs-backport decisions. It probes at the configured `CMAKE_CXX_STANDARD` and uses a three-state policy: complete native -> stand aside; partial native -> stand aside with warning; no native -> inject backport.
- Force flags exist only for diagnosis and are UB-prone on partial-native toolschains: `FORGE_FORCE_SIMD_BACKPORT`, `FORGE_FORCE_SENDERS_BACKPORT`, `FORGE_FORCE_SUBMDSPAN_BACKPORT`, `FORGE_FORCE_LINALG_BACKPORT`.

## Build And Test
- Baseline local command: `cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON && cmake --build build/local && ctest --test-dir build/local --output-on-failure`.
- If the host has no C++ compiler, use the podman verification images/scripts instead of guessing toolchain setup.
- Full verification entrypoint: `scripts/verify-native.sh [gcc16|llvm|zig|local|all]`.
- Container target meanings: `llvm` exercises the all-backports-injected path at `-std=c++26`; `gcc16` verifies native stand-aside for `std::simd`/`std::submdspan`; `zig` exercises the backport path at C++23.
- When running containers manually, use `--rm --userns=keep-id -v "$PWD:/src:Z" -w /src` to avoid root-owned build artifacts and orphaned containers.
- Focused execution test example after configuring a build dir: `cmake --build build/llvm-exec --target test_execution_wave1 && ctest --test-dir build/llvm-exec -R 'execution_wave1' --output-on-failure`.

## Test Gotchas
- `test/CMakeLists.txt` expects `3rdparty/googletest`; if a fresh checkout lacks it, initialize the submodule or provide that directory before configuring tests.
- Do not accidentally stage vendored/untracked `3rdparty/` contents unless explicitly asked.
- SIMD configure probes run during CMake configure (`test/simd/configure_probes`); a configure failure can be a probe failure before any build target exists.
- `linalg` and `submdspan` tests/examples are guarded by `<mdspan>` availability and may be skipped on older standard libraries.
- Some standard library `<execution>` policy implementations may need `tbb`; examples/tests link it only when `find_library(tbb)` succeeds.

## Execution Backport Notes
- Receiver completion callbacks are currently required to be `noexcept`, including `set_value`; throwing completion callbacks are not supported.
- Many `connect_t` overloads still take senders by value, so non-copyable lvalue sender support is incomplete unless a specific overload was added.
- `std::execution` code has lifetime-sensitive synchronous completion paths; prefer focused regression tests with `start_detached`, `split`, `when_all`, `starts_on`, and `continues_on` when touching operation-state ownership.

## Style
- Keep code comments in English; README/doc prose may be Chinese.
- Prefer small, targeted changes. Backport wrappers should preserve standard header names and API shape so downstream code can switch to native implementations without source changes.

## Commit Workflow
- Commit completed logical changes promptly instead of accumulating large mixed diffs.
- Keep commits focused by topic; split docs, tests, fixes, and chores when they are independently reviewable.

## Commit Attribution
- When OpenCode contributed to a commit, use exactly `Co-authored-by: opencode-agent <221189863+opencode-agent@users.noreply.github.com>`. This is the official OpenCode organization account (GitHub id 221189863), pinned by numeric ID so attribution cannot be hijacked by anyone later registering a vanity email.
- Do NOT use `Co-authored-by: OpenCode <noreply@opencode.ai>`: the display name `OpenCode` collides with a real, unrelated GitHub user (`OpenCode`, id 265697 — "Apruzzese Francesco"), and `noreply@opencode.ai` is an unregistered vanity address that attributes to nobody today but can be claimed by whoever verifies it on their account later.
- Never use `opencode@users.noreply.github.com`: the `<username>@users.noreply.github.com` form routes to whoever holds the `opencode` username — that same real person, not the agent.
- When another AI/tool contributed, use that tool's documented co-author identity (e.g. `Co-Authored-By: Claude <noreply@anthropic.com>`). Do not omit the co-author trailer for tool-assisted commits.
