# CC Forge

[English](README.md) | 简体中文 | [日本語](README.ja.md)

CC Forge 是一个 C++23 header-only 库，提供面向 C++26 的标准库 backport，
以及一组小而实用的 `forge::` 运行时工具，用于结构化异步、资源生命周期管理、
消息通路、IO proof 和中立的 accel command proof。

项目分两层：

- `backport/`：标准设施 backport，按标准头文件名暴露，例如 `<execution>`、
  `<simd>`、`<mdspan>`、`<linalg>`、`<memory>` 和 `<utility>`。
- `include/forge/`：非标准扩展，全部在 `namespace forge`，包括线程池、
  scope、channel、strand、IO/accel proof、coroutine task 和类型擦除工具。

`forge.cmake` 会在 consumer configure 阶段按当前 compiler、standard library 和
`CMAKE_CXX_STANDARD` 探测原生支持：完整原生则让位，部分原生则让位并警告，无原生则
注入 Forge backport。

## 功能概览

| 领域 | 入口 | 状态 |
| --- | --- | --- |
| `std::execution` senders/receivers | `<execution>` | 实用 P2300 子集 |
| `std::simd` | `<simd>` | 核心 C++26 表面 |
| `std::submdspan` 与 padded layouts | `<mdspan>` | 当前 C++26 draft surface |
| `std::linalg` | `<linalg>` | 实用 BLAS level 1/2/3 子集 |
| `std::unique_resource` | `<memory>` | 实验性 TS v3 backport |
| `std::constant_wrapper` | `<utility>` | C++26 backport |
| `forge::` runtime utilities | `<forge/execution.hpp>` | 结构化异步支撑层 |
| `forge::io` | `<forge/io.hpp>` | Linux epoll/eventfd 与 Windows IOCP proof backend |
| `forge::accel` | `<forge/accel.hpp>` | runtime vocabulary plus mock/reference command backend |

更精确的语义、限制和状态说明见 [文档目录](docs/README.md)。

## 快速开始

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

更多用法见 [`example/`](example/) 和 [cookbook](docs/forge-cookbook.md)。

## CMake 集成

安装后使用：

```cmake
find_package(CCForge CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE forge::forge)
```

如果只需要标准头 backport 与 native stand-aside 行为，不需要 `forge::` 扩展设施，
可以链接 `forge::std`：

```cmake
target_link_libraries(myapp PRIVATE forge::std)
```

源码树直接引用：

```cmake
include(/path/to/ccforge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

或：

```cmake
add_subdirectory(ccforge)
target_link_libraries(myapp PRIVATE forge::forge)
```

安装后的 package config 会在 consumer 项目里重新运行 native-vs-backport probes，
不会固化打包机器的探测结果。

标准形态入口刻意保持无后缀，例如 `<execution>` 与 `<simd>`；非标准 `forge::`
扩展继续使用 `.hpp` 入口，例如 `<forge/io.hpp>`。这样可以清楚区分项目扩展与标准
库头，也避免目录名与头文件名冲突。

## 要求与验证

- C++23 或更新
- CMake 3.17 或更新
- 可选：podman，用于容器验证
- 可选：Windows/MSVC 主机，用于 Windows IOCP smoke gate

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

完整容器入口：

```bash
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

安装包 smoke：

```bash
scripts/verify-install-package.sh
```

测试分组、sanitizer、Windows smoke 和 install-package 验证见
[testing](docs/testing.md)。

## 文档

- [文档目录](docs/README.md)
- [forge cookbook](docs/forge-cookbook.md)
- [标准 backport 说明](docs/backports/)
- [`forge::` 扩展工具](docs/forge-utilities.md)
- [`forge::io`](docs/forge-io.md)
- [`forge::accel`](docs/forge-accel.md)
- [native handoff 与无感注入](docs/native-handoff.md)
- [测试与验证](docs/testing.md)
- [roadmap](ROADMAP.md)

## 许可证

MIT License。许可证文本见源码文件头。
