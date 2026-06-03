# CC Forge

[English](README.md) | [简体中文](README.zh-CN.md) | 日本語

CC Forge は C++23 のヘッダーオンリーライブラリです。C++26 に向けた標準ライブラリ機能の
backport と、構造化された非同期システムを作るための小さな `forge::` runtime utilities
を提供します。

このプロジェクトは二つの層に分かれています。

- `backport/`: `<execution>`、`<simd>`、`<mdspan>`、`<linalg>`、
  `<memory>`、`<utility>` など、標準ヘッダー名で公開される backport。
- `include/forge/`: `namespace forge` に置かれる非標準の拡張。scheduler、
  scope、channel、strand、IO/accel proof、coroutine task、型消去 helper
  などを含みます。

`forge.cmake` は consumer の configure 時に、compiler、standard library、
`CMAKE_CXX_STANDARD` に合わせてネイティブ実装を検出します。完全なネイティブ実装があれば
Forge は退き、部分的なネイティブ実装なら警告して退き、ネイティブ実装がなければ Forge の
backport ヘッダーを注入します。

## 機能概要

| 領域 | 入口 | 状態 |
| --- | --- | --- |
| `std::execution` senders/receivers | `<execution>` | 実用的な P2300 subset |
| `std::simd` | `<simd>` | C++26 の主要 surface |
| `std::submdspan` と padded layouts | `<mdspan>` | 現行 C++26 draft surface |
| `std::linalg` | `<linalg>` | 実用的な BLAS level 1/2/3 subset |
| `std::unique_resource` | `<memory>` | experimental TS v3 backport |
| `std::constant_wrapper` | `<utility>` | C++26 backport |
| `forge::` runtime utilities | `<forge/execution.hpp>` | 構造化非同期の支援層 |
| `forge::io` | `<forge/io.hpp>` | Linux epoll/eventfd と Windows IOCP proof backends |
| `forge::accel` | `<forge/accel.hpp>` | runtime vocabulary plus mock/reference command backend |

正確な意味、制限、現在の状態については [ドキュメント索引](docs/README.md) を参照してください。

## クイックスタート

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

より多くの例は [`example/`](example/) と [cookbook](docs/forge-cookbook.md) にあります。

## CMake

インストール済み package:

```cmake
find_package(CCForge CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE forge::forge)
```

標準ヘッダー backport と native stand-aside だけが必要で、`forge::` 拡張を使わない
target は `forge::std` にリンクできます。

```cmake
target_link_libraries(myapp PRIVATE forge::std)
```

ソースツリーを直接使う場合:

```cmake
include(/path/to/ccforge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

または:

```cmake
add_subdirectory(ccforge)
target_link_libraries(myapp PRIVATE forge::forge)
```

インストールされた package config は consumer project 側で native-vs-backport probes
を再実行します。そのため、一つの install prefix を異なる compiler、standard library、
`CMAKE_CXX_STANDARD` に適応させられます。

標準形の入口は `<execution>` や `<simd>` のように拡張子なしです。一方、非標準の
`forge::` utilities は `<forge/io.hpp>` のような `.hpp` ヘッダーを使い続けます。
これにより、標準ライブラリヘッダーとプロジェクト拡張を明確に区別し、directory 名と
header 名の衝突も避けられます。

## 要件と検証

- C++23 以上
- CMake 3.17 以上
- 任意: podman。提供されている native-verification containers に使用します。
- 任意: Windows/MSVC host。Windows IOCP smoke gate に使用します。

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

全体の container entrypoint:

```bash
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

Package smoke テスト:

```bash
scripts/verify-install-package.sh
```

テストグループ、sanitizer gate、Windows smoke、install-package verification については
[testing](docs/testing.md) を参照してください。

## ドキュメント

- [ドキュメント索引](docs/README.md)
- [forge cookbook](docs/forge-cookbook.md)
- [backport notes](docs/backports/)
- [`forge::` utilities](docs/forge-utilities.md)
- [`forge::io`](docs/forge-io.md)
- [`forge::accel`](docs/forge-accel.md)
- [native handoff](docs/native-handoff.md)
- [testing and verification](docs/testing.md)
- [roadmap](ROADMAP.md)

## 参考

Forge の backport は標準提案と既存実装を参考にしています。主な参考先は
NVIDIA/stdexec、VcDevel/std-simd、Kokkos stdBLAS、ncnn です。

## ライセンス

MIT License。[LICENSE](LICENSE) を参照してください。
