# Verification containers

These Containerfiles back `scripts/verify-native.sh`. Prefer the script over
manual `podman build` / `podman run` commands so image names, mount flags, test
filters, and build directories stay consistent.

## Targets

| Script target | Image | Containerfile | Purpose |
| --- | --- | --- | --- |
| `llvm` | `forge-llvm` | `Containerfile.llvm` | LLVM/libc++ path at `-std=c++26`; exercises backport injection where libc++ lacks C++26 facilities. |
| `gcc16` | `forge-gcc16` | `Containerfile.gcc16` | GCC 16 native stand-aside path for `std::simd`, `std::constant_wrapper`, padded mdspan layouts, and `std::submdspan`. |
| `zig` | `forge-zig` | `Containerfile.zig` | Zig C++ compiler path at C++23; exercises the backport injection path. |
| `tsan` | `forge-tsan` | `Containerfile.tsan` | ThreadSanitizer gate for the execution and `forge::` utility subsets. |
| `asan` | `forge-asan` | `Containerfile.asan` | AddressSanitizer + UBSan gate for the execution and `forge::` utility subsets, plus focused SIMD memory tests. |
| `gcc-exec` | `forge-gcc16` | `Containerfile.gcc16` | Execution-only libstdc++ path without SIMD/native handoff probes. |

## Normal use

```sh
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

The script uses rootless-friendly mounts:

```sh
podman run --rm --userns=keep-id -v "$PWD:/src:Z" -w /src ...
```

Keep that shape for manual debugging to avoid root-owned build artifacts and
SELinux mount issues.

## Notes

- `all` runs `gcc16`, `llvm`, `zig`, `local`, `gcc-exec`, `tsan`, and `asan`.
- Sanitizer images intentionally run the execution and `forge::` utility
  subsets; ASan/UBSan also runs focused SIMD memory tests. They are targeted
  lifetime/race/memory gates, not full feature-matrix gates.
- The Zig image uses build args for the Zig version and tarball name. Update both
  together if the upstream archive naming changes.
