# Native handoff 与无感注入

Forge 的核心设计目标：**当未来标准库原生提供相同能力后，下游升级工具链时无需任何源码修改即可自动切换到原生实现。**

CMake 层面有两个入口：`forge::std` 只暴露标准头 backport / native stand-aside
surface；`forge::forge` 在此基础上再加入 `include/forge` 的非标准扩展设施。
只需要 `<execution>`、`<simd>`、`<mdspan>` 等标准入口的 consumer 应优先链接
`forge::std`。

## 工具链原生进度（截至 2026-07）

这些特性均已（除 `unique_resource` 外）并入或服务于 C++26，但主流标准库的原生落地进度差异很大，直接决定哪个 backport 会在你的工具链上自动退场：

| 特性 | libstdc++ (GCC) | libc++ (Clang) | 含义 |
|------|-----------------|----------------|------|
| `std::simd` | 当前 GCC 16 验证镜像通过 Forge 的完整 surface probe | 未实现 | GCC 16 lane 要求完整原生并严格让位 |
| `std::constant_wrapper` / padded mdspan layouts | GCC 16 已有 `202603L` pre-P4206 `constant_wrapper` 与 padded layouts | 未实现 | `constant_wrapper` 对完整 `202606L` DR surface 单独探测；旧 native 声明按 partial 让位 |
| `std::submdspan` | 当前 GCC 16 验证镜像通过 Forge 的完整 surface probe | 未实现 | GCC 16 lane 要求完整原生并严格让位 |
| `std::execution` (P2300) | 未实现 | 未实现 | backport 仍是唯一路径 |
| `std::linalg` | 未实现 | 未实现 | backport 仍是唯一路径 |

## 三态探测

`forge.cmake` 以构建所用的 `-std` 通过 `check_cxx_source_compiles()` 做完整探测 + 痕迹探测：

- 完整原生 -> 不注入 backport
- 部分原生（已声明符号但 `__cpp_lib_*` 宏未定义，或 surface 不完整）-> 仍主动让位并 `WARNING`
- 无原生 -> 注入 backport

检测到原生时，`forge.cmake` 会通过 `FORGE_HAS_NATIVE_*` 宏通知 wrapper 头一并退场。部分原生也让位，是为了避免在 `namespace std` 中 on-top 注入造成 ODR 冲突。

## 回归验证口径

Native handoff 的回归应优先看“是否正确让位”和“是否正确注册/不注册对应测试”，不要依赖单个全局 CTest 数量：

- `scripts/verify-native.sh gcc16` 是 native stand-aside 的主线验证：当前固定镜像要求 `std::simd`、padded mdspan layouts 与 `std::submdspan` 通过完整原生 probe，并要求 pre-DR `std::constant_wrapper` 命中 partial-native 让位分支。脚本分别匹配两类诊断，完整能力退化为 partial 时会失败。
- `native_handoff_partial_simd_configure` 使用合成的 incomplete `<simd>` 头验证 partial-native 分支：CMake probe 必须报告 incomplete native support，wrapper 必须 stand aside，不能把 backport 注入到已有 `std::simd` 声明之上。
- `scripts/verify-native.sh llvm` / `zig` 覆盖 backport inject path。
- `scripts/verify-native.sh gcc-exec` 单独覆盖 libstdc++ 上的 `std::execution` backport，因为主流标准库还没有稳定 native `std::execution` 实现。
- `scripts/probe-stdexec-feasibility.sh` 只是可选 reference probe。它可以帮助比较 sender/receiver 语义，但 stdexec 使用 `stdexec::` surface，不能证明 Forge `<execution>` 已经完成 native handoff。

改动 `forge.cmake` probe、wrapper guard 或 feature macro 时，至少运行对应的 native stand-aside lane 和 inject-path lane，并检查相关测试/示例的注册形态。

## Force flags（强制开关）

若确需在部分原生工具链上强制启用 backport（UB 风险，仅供诊断），可设：

- `FORGE_FORCE_SIMD_BACKPORT`
- `FORGE_FORCE_SENDERS_BACKPORT`
- `FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT`
- `FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT`
- `FORGE_FORCE_SUBMDSPAN_BACKPORT`
- `FORGE_FORCE_LINALG_BACKPORT`

这些开关会强制把对应 backport 注入到 include path 和 wrapper guard 中。它们不适合生产配置，因为 partial-native 工具链上很容易触发 `namespace std` 重定义。

## 注入方式

Forge backport 满足以下要素：

- **标准入口不变**：下游写 `#include <memory>` / `#include <execution>` / `#include <linalg>`，而不是 `forge/...`
- **命名空间不变**：API 以 `std::` / `std::execution::` / `std::linalg::` 形式出现
- **API 形状优先对齐标准**：公开接口优先跟随标准最终形态；少量实验性偏离或兼容 wrapper 会在对应特性章节明确说明
- **自动开关（三态让位）**：完整原生和部分原生均让位，无原生才注入 backport

实现方式：`forge.cmake` 将 `backport/` 前置到 include path；`backport/` 内提供与标准同名的包装头（如 `backport/memory`、`backport/linalg`），先包含真实标准库头，再条件注入 backport 实现。

标准入口保持无后缀头名；`forge::` 扩展入口保持 `.hpp` 头名。这是有意边界：
extensionless 头只模拟标准库入口，项目扩展不伪装成标准库头。

> 注意：向 `namespace std` 注入声明/定义在严格意义上属于未定义行为。这是 backport 为达成“无感切换”的工程性权衡，通过“仅在工具链缺失该特性时启用”来降低风险面。
