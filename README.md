# CC Forge

English | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

CC Forge is a C++23 header-only library that provides C++26-oriented standard
backports and a small set of `forge::` runtime utilities for structured async
systems.

The project has two layers:

- `backport/`: standard-header backports such as `<execution>`, `<simd>`,
  `<mdspan>`, `<linalg>`, `<memory>`, and `<utility>`.
- `include/forge/`: non-standard utilities in `namespace forge`, including
  schedulers, scopes, channels, IO/accel proofs, coroutine tasks, and type
  erasure helpers.

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
| `std::constant_wrapper` | `<utility>` | C++26 backport |
| `forge::` runtime utilities | `<forge/execution.hpp>` | structured async support layer |
| `forge::io` | `<forge/io.hpp>` | Linux epoll/eventfd and Windows IOCP proof backends |
| `forge::accel` | `<forge/accel.hpp>` | portable mock command backend |

For exact behavior and caveats, see the [documentation index](docs/README.md).

## Quick Start

```cpp
#include <execution>

auto result = std::execution::sync_wait(
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

auto result = std::execution::sync_wait(std::move(work));
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

The installed package config reruns native-vs-backport probes in the consumer
project, so one install prefix can adapt to different compilers, standard
libraries, and `CMAKE_CXX_STANDARD` values.

## Requirements

- C++23 or newer
- CMake 3.17 or newer
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
- [`forge::accel`](docs/forge-accel.md)
- [native handoff](docs/native-handoff.md)
- [testing and verification](docs/testing.md)

## References

Forge's backports are informed by standards papers and existing implementations,
including NVIDIA/stdexec, VcDevel/std-simd, Kokkos stdBLAS, and ncnn.

## License

MIT License. See the source headers for the license text.
