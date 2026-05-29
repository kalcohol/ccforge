# CC Forge

现代 C++ header-only 库，提供标准库扩展和无感 C++26 backport。

## Backport 一览

| 特性 | 标准提案 | 入口头文件 | 状态 |
|------|---------|-----------|------|
| `std::unique_resource` | P0052R15 | `#include <memory>` | 完整 (实验性) |
| `std::simd` | P1928 | `#include <simd>` | 核心表面完整（Layer 1 向量化） |
| `std::execution` (senders/receivers) | P2300 | `#include <execution>` | Phase 1-4（部分 draft 行为仍有限制） |
| `std::linalg` (BLAS Level 1/2/3) | P1673R13 | `#include <linalg>` | 实用 BLAS 子集（实验性） |
| `std::submdspan` | P2630/P3355 | `#include <mdspan>` | 实用 P2630-era 子集 |

**注意：** `std::unique_resource` 当前仅在 Library Fundamentals TS v3 中，尚未进入 C++26 标准。

## 工具链原生进度（截至 2026-05）

这五个特性均已（除 `unique_resource` 外）并入 C++26，但**主流标准库的原生落地进度差异很大**，直接决定哪个 backport 会在你的工具链上自动退场：

| 特性 | libstdc++ (GCC) | libc++ (Clang) | 含义 |
|------|-----------------|----------------|------|
| `std::simd` | GCC 16 起部分原生（experimental，`-std=c++26`，命名仍在演进） | 未实现 | 新工具链上 backport 开始让位 |
| `std::submdspan` | GCC 16 起部分原生（如 `submdspan_extents`→`subextents` 改名仍在变动） | 未实现 | 同上 |
| `std::execution` (P2300) | 未实现 | 未实现 | backport 仍是唯一路径 |
| `std::linalg` | 未实现 | 未实现 | backport 仍是唯一路径 |

**Forge 的应对：** `forge.cmake` 现在以**三态探测**判定每个特性——完整原生 / 部分原生 / 无原生。一旦检测到原生实现（**哪怕只是部分、哪怕尚未定义 `__cpp_lib_*` 宏**），Forge 会**主动让位、不再注入 backport**，避免在 `namespace std` 中与原生声明重定义（ODR 冲突），并打印 `STATUS`/`WARNING` 说明。若确需在部分原生工具链上强制启用 backport（UB 风险，仅供诊断），可设 `-DFORGE_FORCE_<SIMD|SUBMDSPAN|LINALG|SENDERS>_BACKPORT=ON`。

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

`forge.cmake` 会自动探测工具链是否已原生支持各特性，仅在缺失时启用 backport。

## 要求

- C++23 或更高版本
- CMake 3.17 或更高版本
- `std::linalg` 需要工具链提供 `<mdspan>`（如 Zig/LLVM 18+，GCC 14+）

## 无感注入机制

Forge 的核心设计目标：**当未来标准库原生提供相同能力后，下游升级工具链时无需任何源码修改即可自动切换到原生实现。**

为此，backport 满足以下要素：

- **标准入口不变**：下游写 `#include <memory>` / `#include <execution>` / `#include <linalg>`，而不是 `forge/...`
- **命名空间不变**：API 以 `std::` / `std::execution::` / `std::linalg::` 形式出现
- **API 形状一致**：公开接口与标准最终版保持一致，不引入额外扩展
- **自动开关（三态让位）**：`forge.cmake` 以构建所用的 `-std` 通过 `check_cxx_source_compiles()` 做**完整探测 + 痕迹探测**两层判定：完整原生 → 不注入；**部分原生**（已声明符号但 `__cpp_lib_*` 宏未定义）→ 仍**主动让位**并 `WARNING`，避免在 `namespace std` 中 on-top 注入造成 ODR 冲突；无原生 → 注入 backport。检测到原生时通过 `FORGE_HAS_NATIVE_*` 宏通知 wrapper 头一并退场。可用 `FORGE_FORCE_*_BACKPORT` 覆盖（UB 风险）

实现方式：`forge.cmake` 将 `backport/` 前置到 include path；`backport/` 内提供与标准同名的包装头（如 `backport/memory`、`backport/linalg`），先包含真实标准库头，再条件注入 backport 实现。

> 注意：向 `namespace std` 注入声明/定义在严格意义上属于未定义行为。这是 backport 为达成"无感切换"的工程性权衡，通过"仅在工具链缺失该特性时启用"来降低风险面。

## `std::execution` 说明

当前为 P2300 senders/receivers 的 Phase 1-4 backport（Phase 4 部分功能）：

**已实现：**
- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、`let_stopped`、`write_env`
- 调度器适配器：`starts_on`、`continues_on`（schedule_from）、`transfer_just`、`bulk`（串行）
- 组合器：`into_variant`、`when_all`（完整笛卡尔积签名、外层取消传播）、`when_all_with_variant`、`split`、`ensure_started`、`start_detached`、`spawn_future`
- 消费者：`sync_wait`（单一 value completion 返回 `optional<tuple<...>>`，多组 value completions 返回 `optional<variant<tuple<...>, ...>>`）、`sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、`any_stop_token`（类型擦除）、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines；单一 value completion 保持返回 `tuple`，多组 value completions 返回 `variant<tuple<...>, ...>`）
- 基础设施：`completion_signatures_of_t`、`enable_sender`、`get_completion_scheduler`、`get_completion_domain`、`transform_completion_signatures`、CPO 分发基础设施
- 域调度：`default_domain`、`get_domain` CPO、receiver-env late domain 选取、`connect_t` domain `transform_sender` / `transform_env` wrapper
- Async scope（P3149R11）：`simple_counting_scope`、`counting_scope`（独立 stop-aware scope）

**当前限制：**
- Receiver completion callbacks 当前必须为 `noexcept`，包括 `set_value`、`set_error` 和 `set_stopped`；throwing completion callbacks 尚不支持。
- Library-provided sender 的 `connect_t` 提供 rvalue 移动路径与 copyable lvalue 拷贝路径；non-copyable lvalue sender 仍需显式 `std::move` 后连接。
- Execution domain 支持仍是 draft 子集：`connect_t` 已按 receiver env 选取 start domain，并支持 scheduler-derived completion domain、`transform_sender` recovery 和 `transform_env` wrapper；scheduler-derived domain 仅在 scheduler 显式定制 `get_completion_domain` 时生效，否则会回退到 `default_domain`；完整标准递归 `transform_sender` 分发模型尚未实现。
- `ensure_started` 当前采用多消费者缓存语义；缓存结果以 lvalue 形式投递，因此 move-only value 结果尚不支持；销毁返回 sender 不会请求停止，source 会继续运行到完成。
- `start_detached` 当前对 `set_error` 采用 terminate-on-error 契约；若 sender 可能失败，应先接入 `upon_error` / `let_error` 等错误处理再 detach。
- `spawn_future` 当前返回 move-only single-consumer future sender；其 shared-state 分配会使用 `env` 中的 `get_allocator`，但 consumer/callback 辅助分配尚未完整 allocator-aware。
- `counting_scope::join()` 当前保留 Forge 既有阻塞扩展；标准 sender-returning join 形态尚未接入。
- `forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；自定义 receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的 task operation-state。

> Forge 自带 sender/receiver/scheduler 已优先采用当前 C++26 draft 的成员式定制（如 `connect` / `get_env` / `set_value` / `schedule`）。CPO 层仍保留 `tag_invoke` fallback 以兼容既有自定义类型；新代码建议优先使用成员式定制。当原生 `<execution>` 可用时，整个 backport 自动禁用。

> `scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution 子集的 ThreadSanitizer 与 ASan+UBSan 路径；`llvm` 目标覆盖 libc++ inject-path 全量测试。

> 测试子目录可用 CMake 开关独立启停，默认全开：`FORGE_TEST_ENABLE_EXECUTION`、`FORGE_TEST_ENABLE_SIMD`、`FORGE_TEST_ENABLE_UNIQUE_RESOURCE`、`FORGE_TEST_ENABLE_SUBMDSPAN`、`FORGE_TEST_ENABLE_LINALG`、`FORGE_TEST_ENABLE_FORGE`、`FORGE_TEST_ENABLE_NATIVE_HANDOFF`。

> 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`。Forge 的 tests/examples 会在检测到 `tbb` 时自动链接。

## `std::linalg` 说明

当前为 P1673R13 风格的实验性 backport，提供一个不依赖外部 BLAS 库的实用 BLAS 子集：

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、`vector_sum_of_squares`、`setup_givens_rotation`、`apply_givens_rotation`

**BLAS Level 2：** `matrix_vector_product`、`triangular_matrix_vector_product`、`triangular_matrix_vector_solve`、`symmetric_matrix_vector_product`、`hermitian_matrix_vector_product`、`matrix_rank_1_update`/`_c`、`symmetric_matrix_rank_1/2_update`

**BLAS Level 3：** `matrix_product`、`triangular_matrix_product`、`triangular_matrix_matrix_left_solve`、`symmetric_matrix_product`、`symmetric_matrix_rank_k/2k_update`

**辅助组件：** `scaled`/`conjugated`/`transposed`/`conjugate_transposed` 视图函数、`scaled_accessor`、`conjugated_accessor`、`layout_transpose`、`layout_blas_packed`、标记类型（`upper_triangle`/`lower_triangle`/`column_major`/`row_major` 等）

> 依赖 C++23 `<mdspan>`，在无 `<mdspan>` 的工具链上（如 GCC 13）优雅跳过。当原生 `<linalg>` 可用时（`__cpp_lib_linalg >= 202311`），backport 自动禁用。未实现 execution policy 重载，不链接系统 BLAS；SIMD 是 Forge 自身的可选实现细节。Level 1、Level 2 与辅助视图有直接回归测试，Level 3 当前主要由示例和轻量 smoke coverage 覆盖。

> Level 2 rank-update 采用当前 draft 的 overwrite/update 分离：不带输入矩阵 `E` 的重载覆盖输出矩阵，带 `E` 的重载计算 `A = E + update`。旧参数顺序仍作为兼容 wrapper 保留，但文档化 API 以后续 draft 拼写为准。

**SIMD 加速：** BLAS Level 1 归约操作（`dot`、`vector_two_norm`、`vector_abs_sum`）以及 `copy`、`scale` 在 Forge `std::simd` 可用时自动使用 SIMD 加速路径；`matrix_vector_product`（GEMV）内层循环也已 SIMD 化。支持全部非复数标准算术类型。已在 x86_64（原生）、aarch64、riscv64、loongarch64 四个架构上通过 zig 交叉编译 + qemu 验证。

**OpenMP 并行：** 无 execution policy 的 `std::linalg` overload 默认保持顺序执行；Forge 当前不再因为翻译单元带 `-fopenmp` 就隐式并行化 GEMM/GEMV。未来若补并行路径，会放在显式 opt-in 或 execution policy overload 下。

## `std::submdspan` 说明

当前为 P2630/P3355 时代接口的实用子集，覆盖 `full_extent`、`strided_slice`、`submdspan_mapping_result`、`submdspan_extents`、`layout_left` / `layout_right` / `layout_stride` 的 `submdspan_mapping`，以及 `submdspan()` 本体。

> 当前 C++26 draft 的 `submdspan` 命名和规范仍在演进，后续草案已经引入 `subextents` / slice canonicalization 等形态变化；Forge 会在检测到原生 `std::submdspan` 时主动让位，避免与标准库实现重定义。未实现 `layout_left_padded` / `layout_right_padded` 的 mapping，`strided_slice::extent` 语义按当前实现注释中的 working draft 基线处理。

## `std::simd` 说明

当前 `std::simd` backport 已完整覆盖 [simd.syn] 公开表面：

- **核心 API**：`simd<T, Abi>`、`simd_mask<T, Abi>`、构造/转换/下标/算术/比较/位运算
- **内存操作**：`copy_from`/`copy_to`（含 flags）、`load`/`store`（含 partial/unchecked 重载）、`gather`/`scatter`（含 range 重载）
- **归约与排列**：`reduce`、`hmin`/`hmax`、`split`/`cat`、`select`
- **Layer 1 向量化**：GCC/Clang vector extension 后端，`if consteval` 保持 constexpr 正确性
- **Feature macro**：定义 `__cpp_lib_simd = 202411L`，表明完整 [simd.syn] 覆盖

已在 x86_64、aarch64、riscv64、loongarch64 四架构验证。

## `forge::` 扩展工具

`include/forge/` 下提供标准之外的扩展工具（`namespace forge`，非 backport）：

**类型擦除组件：**
- `forge::any_sender_of<Sigs...>` — 类型擦除 sender，SBO 64B + 堆回退，提供 `sync_wait()` 方法直接运行所存储的 sender
- `forge::any_receiver_of<Sigs...>` — 类型擦除 receiver，用于在泛型代码中存储不同类型的 receiver

**并发组件：**
- `forge::static_thread_pool` — 线程池，提供 `scheduler` 接口，与 `std::execution` 集成
- `forge::system_context` — 全局线程池单例，提供便捷的全局调度器访问
- `forge::task<T>` — 协程返回类型，实现 `sender` 接口，可与 `sync_wait` 配合使用；task body 中可 `co_await` 同步或异步 sender（需要 C++20 coroutines）

## 编码规范

- **文件编码**：UTF-8 without BOM（符合 C++23 标准）
- **代码注释**：仅使用英文
- **许可证**：MIT License (2026)

## 文档

- [示例](example/) - 使用示例

## 参考实现

本项目的 backport 实现参考了以下开源项目：

- **std::simd**: [std-simd](https://github.com/VcDevel/std-simd) - Matthias Kretz 的参考实现
- **std::linalg**: [kokkos/stdBLAS](https://github.com/kokkos/stdBLAS) - P1673 参考实现
- **std::execution**: [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) - P2300 参考实现
- **SIMD 优化**: [ncnn](https://github.com/Tencent/ncnn) - 腾讯的高性能神经网络推理框架

感谢这些项目的贡献者。

## 许可证

MIT License - 详见 LICENSE 文件
