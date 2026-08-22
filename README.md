# CC Forge

English | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

CC Forge is a C++23 header-only library that provides C++26-oriented standard
backports and a small set of `forge::` runtime utilities for structured async
systems.

The project has two layers:

- `backport/`: standard-header backports such as `<execution>`, `<simd>`,
  `<mdspan>`, `<linalg>`, `<memory>`, and `<utility>`.
- `include/forge/`: non-standard utilities in `namespace forge`, including
  schedulers, scopes, channels, IO proofs, coroutine tasks, and type erasure
  helpers.

`forge.cmake` probes the consumer toolchain at configure time. Complete native
support stands aside, partial native support stands aside with a warning, and
missing native support injects Forge's backport headers.

## Features

| Area | Header | Status |
| --- | --- | --- |
| `std::execution` senders/receivers | `<execution>` | practical P2300 subset |
| `std::simd` | `<simd>` | core C++26 surface |
| `std::submdspan` and padded layouts | `<mdspan>` | current C++26 draft surface |
| `std::linalg` | `<linalg>` | practical BLAS level 1/2/3 subset |
| `std::unique_resource` | `<memory>` | experimental TS v3 backport |
| `std::constant_wrapper` | `<utility>` | C++26 + P4206 DR backport |
| `forge::` runtime utilities | `<forge/execution.hpp>` | structured async support layer: pool, scope, channel, strand, timer |
| `forge::io` | `<forge/io.hpp>` | Linux epoll/eventfd and Windows IOCP portable backends |
| `forge::io` io_uring proof | `<forge/io/io_uring_context.hpp>` | gated Linux io_uring completion backend, outside the portable context selection |

For exact behavior and caveats, see the [documentation index](docs/README.md).

## Quick Start

```cpp
#include <execution>

auto result = std::this_thread::sync_wait(
    std::execution::just(42)
    | std::execution::then([](int value) { return value * 2; })
);
```

```cpp
#include <forge/execution.hpp>

forge::static_thread_pool pool{4};
auto work = std::execution::starts_on(
    pool.get_scheduler(),
    std::execution::just(1)
    | std::execution::then([](int value) { return value + 1; })
);

auto result = std::this_thread::sync_wait(std::move(work));
pool.shutdown();
pool.wait();
```

More examples live in [`example/`](example/) and the
[cookbook](docs/forge-cookbook.md).

## CMake

Installed package:

```cmake
find_package(CCForge CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE forge::forge)
```

Use `forge::std` when a target only needs standard-header backports and native
stand-aside behavior:

```cmake
target_link_libraries(myapp PRIVATE forge::std)
```

Source tree:

```cmake
include(/path/to/ccforge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

or:

```cmake
add_subdirectory(ccforge)
target_link_libraries(myapp PRIVATE forge::forge)
```

GoogleTest is only a dependency of CC Forge's own top-level test build. Direct
`forge.cmake` inclusion, subproject `add_subdirectory`, and installed-package
consumption neither configure nor install GoogleTest, and do not require the
`3rdparty/googletest` submodule.

The installed package config reruns native-vs-backport probes in the consumer
project, so one install prefix can adapt to different compilers, standard
libraries, and `CMAKE_CXX_STANDARD` values.
Within one configured build tree, targets consuming the standard-shaped
wrappers must use that configured language mode; target-only `CXX_STANDARD`
overrides require a separate build tree.

Standard-shaped entries intentionally use extensionless headers such as
`<execution>` and `<simd>`. Non-standard `forge::` utilities keep `.hpp`
headers such as `<forge/io.hpp>` to distinguish project extensions from
standard-library headers and avoid directory/header name collisions.

## Requirements

- C++23 or newer
- CMake 3.20 or newer
- Optional: podman for the provided native-verification containers
- Optional: a Windows/MSVC host for the Windows IOCP smoke gate

## Verification

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

Full container entrypoint:

```bash
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

Package smoke:

```bash
scripts/verify-install-package.sh
```

See [testing](docs/testing.md) for test groups, sanitizer gates, Windows smoke,
and install-package verification.

## Documentation

- [documentation index](docs/README.md)
- [forge cookbook](docs/forge-cookbook.md)
- [backport notes](docs/backports/)
- [`forge::` utilities](docs/forge-utilities.md)
- [`forge::io`](docs/forge-io.md)
- [native handoff](docs/native-handoff.md)
- [testing and verification](docs/testing.md)
- [roadmap](ROADMAP.md)

## References

Forge's backports are informed by standards papers and existing implementations,
including NVIDIA/stdexec, VcDevel/std-simd, Kokkos stdBLAS, and ncnn.

## License

MIT License. See [LICENSE](LICENSE).
