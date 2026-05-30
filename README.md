# CC Forge

现代 C++ header-only 库，提供标准库扩展和无感 C++26 backport。

## Backport 一览

| 特性 | 标准提案 | 入口头文件 | 状态 |
|------|---------|-----------|------|
| `std::unique_resource` | P0052R15 | `#include <memory>` | 完整 (实验性 TS v3) |
| `std::simd` | P1928 | `#include <simd>` | 核心表面完整（Layer 1 向量化） |
| `std::execution` (senders/receivers) | P2300 | `#include <execution>` | Phase 1-4（部分 draft 行为仍有限制） |
| `std::linalg` (BLAS Level 1/2/3) | P1673R13 | `#include <linalg>` | 实用 BLAS 子集（实验性） |
| `std::constant_wrapper` | P2781 | `#include <utility>` | C++26 backport |
| `std::submdspan` | P2630/P3663/P3982 + P2642 | `#include <mdspan>` | 当前 C++26 draft surface |

`forge.cmake` 会按当前 `CMAKE_CXX_STANDARD` 自动探测原生支持：完整原生 -> 让位；部分原生 -> 让位并警告；无原生 -> 注入 backport。细节见 [native handoff](docs/native-handoff.md)。

## 快速开始

```cpp
#include <memory>
// std::unique_resource — RAII 资源管理
auto file = std::make_unique_resource_checked(fopen("data.txt", "r"), nullptr, &fclose);
```

```cpp
#include <execution>
// P2300 senders/receivers — 结构化异步
auto result = std::execution::sync_wait(
    std::execution::just(42)
    | std::execution::then([](int x) { return x * 2; })
);
// *result == tuple{84}
```

```cpp
#include <linalg>
#include <mdspan>
// std::linalg — BLAS Level 1/2/3
double a[] = {1,2,3}, b[] = {4,5,6};
std::mdspan va(a, 3), vb(b, 3);
double d = std::linalg::dot(va, vb); // 1*4 + 2*5 + 3*6 = 32
```

```cpp
#include <execution>
// when_all — 结构化并发
auto [a, b] = *std::execution::sync_wait(
    std::execution::when_all(
        std::execution::just(42),
        std::execution::just(3.14)
    )
);
// a == 42, b == 3.14
```

## CMake 集成

```cmake
include(/path/to/forge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

或使用 `add_subdirectory`：

```cmake
add_subdirectory(forge)
target_link_libraries(myapp PRIVATE forge::forge)
```

## 要求

- C++23 或更高版本
- CMake 3.17 或更高版本
- `std::linalg` / `std::submdspan` 需要工具链提供 `<mdspan>`（如 Zig/LLVM 18+，GCC 14+）

## 验证

基础本地命令：

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

完整容器验证入口：

```bash
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

更多测试开关、podman 目标和 sanitizer 说明见 [testing](docs/testing.md)。

## 文档

- [Native handoff 与无感注入](docs/native-handoff.md)
- [测试与验证](docs/testing.md)
- [`forge::` 扩展工具](docs/forge-utilities.md)
- Backports:
  - [`std::execution`](docs/backports/execution.md)
  - [`std::linalg`](docs/backports/linalg.md)
  - [`std::simd`](docs/backports/simd.md)
  - [`std::submdspan` / `std::constant_wrapper`](docs/backports/mdspan.md)
  - [`std::unique_resource`](docs/backports/unique_resource.md)
- [示例](example/)

## `forge::` 扩展工具

`include/forge/` 下提供标准之外的扩展工具（`namespace forge`，非 backport）：

聚合头：

```cpp
#include <forge/execution.hpp>
```

- `forge::static_thread_pool` — 线程池，提供 `scheduler` 接口，与 `std::execution` 集成
- `forge::single_thread_context` — 单线程调度上下文，适合串行化执行和测试调度切换
- `forge::timer_context` — 定时调度上下文，提供 `schedule_after` / `schedule_at`
- `forge::runtime_context` — 显式拥有的运行时上下文，组合 CPU scheduler 和 timer 调度
- `forge::async_scope` — 结构化并发 scope，拥有 eager-start sender work，支持 close / stop / wait
- `forge::bounded_channel<T>` — 有界 FIFO 消息通道，提供 async send/recv 和显式 close / stop 语义
- `forge::resource_context` — 资源/会话 owning runtime shell，组合 runtime、timer 和 scope 生命周期
- `forge::strand` — scheduler 串行化 wrapper，保证接受的任务 FIFO 且不并发执行
- `forge::system_context` — 全局线程池单例，提供便捷的全局调度器访问
- `forge::task<T>` — 协程返回类型，实现 `sender` 接口，可与 `sync_wait` 配合使用；task body 中可 `co_await` 同步或异步 sender（需要 C++20 coroutines）
- `forge::any_scheduler` — 窄 scheduler 类型擦除，面向 `schedule()` 常见形状
- `forge::erased_sender<Sigs>` — connectable sender 类型擦除，支持多 value 形状、`std::exception_ptr` error 和 stopped
- `forge::any_sender_of<Sigs...>` / `forge::any_receiver_of<Sigs...>` — 窄类型擦除工具，使用 64B SBO + 堆回退；`any_sender_of` 目前提供 `sync_wait()` 便利路径，不是通用 connectable erased sender

详细语义和限制见 [`forge::` 扩展工具](docs/forge-utilities.md)。

## 参考实现

本项目的 backport 实现参考了以下开源项目：

- **std::simd**: [std-simd](https://github.com/VcDevel/std-simd) - Matthias Kretz 的参考实现
- **std::linalg**: [kokkos/stdBLAS](https://github.com/kokkos/stdBLAS) - P1673 参考实现
- **std::execution**: [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) - P2300 参考实现
- **SIMD 优化**: [ncnn](https://github.com/Tencent/ncnn) - 腾讯的高性能神经网络推理框架

感谢这些项目的贡献者。

## 许可证

MIT License - 详见 LICENSE 文件
