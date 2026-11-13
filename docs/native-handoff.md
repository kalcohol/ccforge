# Native handoff 与无感注入

Forge 的核心设计目标：**当未来标准库原生提供相同能力后，下游升级工具链时无需任何源码修改即可自动切换到原生实现。**

CMake 层面有两个入口：`forge::std` 只暴露标准头 backport / native stand-aside
surface；`forge::forge` 在此基础上再加入 `include/forge` 的非标准扩展设施。
只需要 `<execution>`、`<simd>`、`<mdspan>` 等标准入口的 consumer 应优先链接
`forge::std`。

## 工具链原生进度（固定验证镜像，最后核对 2026-11-13）

这些特性均已（除 `unique_resource` 外）并入或服务于 C++26，但主流标准库的原生落地进度差异很大，直接决定哪个 backport 会在你的工具链上自动退场：

| 特性 | libstdc++ (GCC) | libc++ (Clang) | 含义 |
|------|-----------------|----------------|------|
| `std::simd` | 当前 GCC 16 验证镜像在 C++26 有 core declarations，但缺 math、bit、complex、creation 与 gather/scatter 等完整 surface；C++23 下 `<simd>` 不提供声明 | 未实现 | GCC 16 lane 要求 C++26 命中 partial-native 让位、C++23 正确注入 |
| `std::constant_wrapper` / padded mdspan layouts | GCC 16 已有 `202603L` pre-P4206 `constant_wrapper`；padded layouts 缺 dynamic-padding 默认模板参数 | 未实现 | 两者都通过完整 surface probe 与 partial probe 区分；旧 native 声明按 partial 让位 |
| `std::submdspan` | 当前 GCC 16 验证镜像通过 Forge 的完整 surface probe | 未实现 | GCC 16 lane 要求完整原生并严格让位 |
| `std::execution` (P2300) | 未实现 | 未实现 | backport 仍是唯一路径 |
| `std::linalg` | 未实现 | 未实现 | backport 仍是唯一路径 |
| `std::hive` | 无原生，注入 | 无原生，注入 | 以配置 probe 为准 |
| `std::unique_resource` | 无原生 | 无原生 | TS v3，具体入口见对应页面 |

## 三态探测

`forge.cmake` 以构建所用的 `-std` 通过 `check_cxx_source_compiles()` 做完整探测 + 痕迹探测：

- 完整原生 -> 不注入 backport
- 部分原生（已声明符号但 `__cpp_lib_*` 宏未定义，或 surface 不完整）-> 仍主动让位并 `WARNING`
- 无原生 -> 注入 backport

检测到原生时，`forge.cmake` 会通过 `FORGE_HAS_NATIVE_*` 宏通知 wrapper 头一并退场。部分原生也让位，是为了避免在 `namespace std` 中 on-top 注入造成 ODR 冲突。

`<hive>` 同样采用三态策略：完整与 partial native 均让位，partial 会警告；无原生
声明时才注入 backport。包含 `<hive>` 后 backport 提供 `__cpp_lib_hive >= 202502L`；
不承诺只包含 `<version>` 就能发现该宏。详见 [Hive 说明](backports/hive.md)。

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
标准声明探针使用 CMake 为当前 `CMAKE_CXX_STANDARD` 与 extensions 模式选择的
compile option，不自行拼接编译器特定的 `-std=` 方言名字。
探针结论属于当前 configure 的全局语言模式；通过 `forge::forge` 或 `forge::std`
接收 Forge 编译要求的 C++ translation unit 必须使用该模式。需要以不同模式直接消费
Forge 时，应使用独立 build tree。

直接 `include(forge.cmake)` 或 `find_package(CCForge CONFIG REQUIRED)` 前，必须显式
设置 `CMAKE_CXX_STANDARD` 为 23 或更新版本；未设置时配置会报告
`CC Forge requires an explicit CMAKE_CXX_STANDARD`。仅调用
`target_compile_features(app PRIVATE cxx_std_26)` 无法指定 Forge probe 的语言模式。
通过 `add_subdirectory` 或 FetchContent 引入源码工程时，工程仍保留 C++23 默认值；
需要其他模式时应在引入前设置 `CMAKE_CXX_STANDARD`。

`FORGE_PROBE_CXX_STANDARD` 记录 probe 请求的标准。`forge::forge` 和 `forge::std`
通过 INTERFACE usage requirement 传递一个轻量的强制包含校验头，随 CMake 的编译要求
传播；语言模式不一致时，在编译阶段报告 `CC Forge language standard mismatch`。
消息包含 consumer 的实际语言阶段、probe 的语言阶段与请求的 `CMAKE_CXX_STANDARD`，
并提示在引入 Forge 前设置该变量、统一 consumer 的 compile features 与 flags。
GCC、Clang、AppleClang 和 Zig 使用 `-include`，MSVC frontend 使用 `/FI`。
安装包在 consumer build tree 重新生成该头，单配置与多配置 generator 使用同一检查。

检查使用 `__cplusplus`（MSVC 使用 `_MSVC_LANG`）识别实际 C++23 / C++26 语言阶段，
兼容 draft 宏值及 `/std:c++latest`，不要求等于最终 ISO 宏值。判断以 probe 和实际
translation unit 的有效语言模式为准：例如 probe 使用 C++26 时，target 的
`CXX_STANDARD=23` 经自身或传递的 `cxx_std_26` 提升到 C++26，仍可正常编译。
校验头无需手动包含，不引入声明，也不保留辅助 macro。整个头仅在定义
`__cplusplus` 时生效；即使 generator 将强制包含选项传给混合 target 的 C 源文件，
C translation unit 也不会执行语言检查。

PRIVATE 依赖的边界由 CMake 决定。例如 library 通过 PRIVATE 链接 `forge::forge`，
该 library 自身的 C++ 编译接受检查；只链接此 library、未接收 Forge 编译要求的
executable 可以使用 C++20。若 executable 也使用 Forge，应显式链接相应 Forge target。

同一次 configure 中，重复引入 Forge 必须使用相同设置。
`CMAKE_REQUIRED_*` 等附加 flags、include / library 列表的未定义与空值视为等价；
`CMAKE_CXX_EXTENSIONS` 仍独立参与比较，因为 GNU 扩展方言可能改变标准库 feature macros。
全局 fingerprint 包含
标准、force flags、runtime / IO 选项及 probe 共用的输入清单，包括编译器、编译与链接
选项、sysroot、deployment target、自定义 try-compile platform variables 和头文件搜索
环境变量等输入，以及解析到的 toolchain、probe source 与头文件内容摘要；
路径不变而文件内容变化也会报告冲突。
相同 fingerprint 直接复用首次配置；不同 fingerprint 会以
`CC Forge configuration changed after first inclusion` 终止，并列出变化的输入及前后值。
该规则也适用于跨目录引入；修改配置后重新运行 CMake 属于新的 configure，仍可正常
重新探测。

partial-native 工具链上，Forge 仍严格让位。依赖该标准 surface 完整实现的 examples
会跳过注册，而不是强行混入 backport；完整原生和无原生声明的 inject path 仍构建这些
examples。

## Force flags（诊断开关）

以下开关仅用于诊断 backport 本身：

- `FORGE_FORCE_SIMD_BACKPORT`
- `FORGE_FORCE_HIVE_BACKPORT`
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

失败的探测结果有意不缓存，以便工具链或配置变化后重新判断；每次重新配置会多花几秒。
