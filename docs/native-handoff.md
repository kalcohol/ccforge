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
| `std::simd` | 当前 GCC 16 验证镜像在 C++26 有 core declarations，但缺 math、bit、complex、creation 与 gather/scatter 等完整 surface；C++23 下 `<simd>` 不提供声明 | 未实现 | GCC 16 lane 要求 C++26 命中 partial-native 让位、C++23 正确注入 |
| `std::constant_wrapper` / padded mdspan layouts | GCC 16 已有 `202603L` pre-P4206 `constant_wrapper`；padded layouts 缺 dynamic-padding 默认模板参数 | 未实现 | 两者都通过完整 surface probe 与 partial probe 区分；旧 native 声明按 partial 让位 |
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

- `scripts/verify-native.sh gcc16` 是 native stand-aside 的主线验证：当前固定镜像在
  C++26 要求 `std::simd`、padded mdspan layouts 与 pre-DR
  `std::constant_wrapper` 命中 partial-native 让位分支，并要求
  `std::submdspan` 通过完整原生 probe；随后
  以 C++23 运行 SIMD-only suite，要求 declaration-free `<simd>` 命中 BACKPORT。
  脚本分别匹配这些诊断；FULL probe 覆盖各主要 public sub-surface，不以少数 core
  符号代替完整性证据。
- `native_handoff_partial_simd_configure` 使用合成的 incomplete `<simd>` 头验证
  partial-native 分支；`native_handoff_probe_surface_{sampled,independent_roots}`
  进一步让 `<execution>`、`<simd>`、padded mdspan、submdspan 和 linalg 分别只暴露
  “被旧探针采样的表面”或“未被采样的独立 marker/completion/connection/query 根”。每个
  execution 根都有隔离 fixture；两类都必须报告 PARTIAL 并让 wrapper
  stand aside。`native_handoff_senders_complete_configure` 则使用独立维护的、符合当前
  `202506L` 表面形状的 synthetic native `<execution>` / `<stop_token>` fixture，避免
  用 Forge backport 证明自身完整。FULL probe 必须命中 `env<>`、四个当前 protocol
  marker、`get_env` receiver query、`connect`/`start` operation protocol、完整 scheduler concept 所需的
  `schedule`/forward-progress 表面、`get_await_completion_adaptor`、stop token、
  transfer/composition、精确 completion/result shape 和 `std::this_thread::sync_wait`，
  并能承载 Forge 自有的 `any_stop_token`。
- `scripts/verify-native.sh llvm` / `zig` 覆盖 backport inject path。
- `scripts/verify-native.sh gcc-exec` 单独覆盖 libstdc++ 上的 `std::execution` backport，因为主流标准库还没有稳定 native `std::execution` 实现。
- `scripts/probe-stdexec-feasibility.sh` 只是可选 reference probe。它可以帮助比较 sender/receiver 语义，但 stdexec 使用 `stdexec::` surface，不能证明 Forge `<execution>` 已经完成 native handoff。

改动 `forge.cmake` probe、wrapper guard 或 feature macro 时，至少运行对应的 native stand-aside lane 和 inject-path lane，并检查相关测试/示例的注册形态。

## Force flags（诊断开关）

以下开关仅用于诊断 backport 本身：

- `FORGE_FORCE_SIMD_BACKPORT`
- `FORGE_FORCE_SENDERS_BACKPORT`
- `FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT`
- `FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT`
- `FORGE_FORCE_SUBMDSPAN_BACKPORT`
- `FORGE_FORCE_LINALG_BACKPORT`

这些开关会要求 wrapper 走 backport guard，但不会移除标准库已经声明的同名实体。
因此它们只在 declaration-free 的语言模式 / 标准库组合上可靠；partial-native 或
complete-native 工具链可能直接因 `namespace std` 重定义而编译失败，也可能形成 ODR
风险。它们不是 partial-native 的兼容逃生通道，更不能作为 production 配置。遇到
partial native 时，应等待工具链补全，或选择尚未暴露这些声明的语言模式。

## 注入方式

Forge backport 满足以下要素：

- **标准入口不变**：下游写 `#include <memory>` / `#include <execution>` / `#include <linalg>`，而不是 `forge/...`
- **命名空间不变**：API 以 `std::` / `std::execution::` / `std::linalg::` 形式出现
- **API 形状优先对齐标准**：公开接口优先跟随标准最终形态；少量实验性偏离或兼容 wrapper 会在对应特性章节明确说明
- **自动开关（三态让位）**：完整原生和部分原生均让位，无原生才注入 backport

实现方式：`forge.cmake` 将 `backport/` 前置到 include path；`backport/` 内提供与标准同名的包装头（如 `backport/memory`、`backport/linalg`），先包含真实标准库头，再条件注入 backport 实现。

受支持的消费方式是链接 `forge::std` / `forge::forge`（或使用安装包导出的同名
target），让 `forge.cmake` 的探针宏与 include path 一起传播。仅手工添加
`-I backport` 不属于兼容契约：在已有 partial/native 声明的工具链上，缺少
`FORGE_HAS_NATIVE_*` 决策宏可能产生重定义。Padded layouts、`submdspan` 和 `linalg`
还依赖工具链先提供可用的 C++23 base `<mdspan>`；没有该 foundation 时 CMake 会把这些
依赖面报告为 `UNAVAILABLE`，不会声称已注入。

标准入口保持无后缀头名；`forge::` 扩展入口保持 `.hpp` 头名。这是有意边界：
extensionless 头只模拟标准库入口，项目扩展不伪装成标准库头。

> 注意：向 `namespace std` 注入声明/定义在严格意义上属于未定义行为。这是 backport 为达成“无感切换”的工程性权衡，通过“仅在工具链缺失该特性时启用”来降低风险面。
