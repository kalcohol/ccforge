# 测试与验证

## 本地基线

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

如果 host 没有可用 C++ 编译器，使用 podman 验证镜像和 `scripts/verify-native.sh`，不要猜测工具链配置。

Unix 验证辅助脚本需要 GNU `realpath -m` 或 Python 3.6+ 来解析临时工作目录；
macOS 自带的 `realpath` 不支持该选项时会使用 Python。此要求不影响 header-only 消费者。

## Self-hosted verification floor（自托管验证基线）

Forge 不把验证流程绑定到 GitHub hosted CI。当前标准是本地或 self-hosted
verification floor：稳定机器直接调用仓库脚本，未来要迁移到 Jenkins、Buildkite、
GitHub self-hosted runner 或其它 CI 时，只需要挂接这些入口。

默认 owner-neutral 行为变更建议跑：

```bash
scripts/verify-selfhosted-floor.sh
```

该脚本按顺序调用现有验证入口，默认 native lanes 为 `gcc16-slim llvm gcc-exec tsan asan`，
先运行 source-style 与 docs/examples audits，随后运行 install-package smoke。它不会
并发运行 podman `:Z` bind mount lane。
如需增加或替换 native lane，直接传参或设置环境变量：

```bash
scripts/verify-selfhosted-floor.sh llvm gcc16 gcc-exec tsan asan
FORGE_VERIFY_FLOOR_NATIVE_LANES='llvm zig gcc-exec tsan asan' scripts/verify-selfhosted-floor.sh
```

install-package smoke 是 floor 的默认组成部分。只有在诊断 floor wrapper 本身，或同一
变更已经由独立、可追溯的安装包验证覆盖时，才可临时跳过：

```bash
FORGE_VERIFY_FLOOR_SKIP_INSTALL=1 scripts/verify-selfhosted-floor.sh
```

Windows/MSVC smoke 默认不跑，因为它需要一台 Windows 主机。需要把 Windows 纳入同一
floor 时，设置 `FORGE_VERIFY_FLOOR_WINDOWS=1`，并通过 `FORGE_WINDOWS_*` 环境变量把
主机、MSVC、source/ref 等信息传给 Windows wrapper：

```bash
FORGE_VERIFY_FLOOR_WINDOWS=1 \
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VC_VARS='C:\path\to\VC\Auxiliary\Build\vcvars64.bat' \
scripts/verify-selfhosted-floor.sh
```

公开文档和脚本不得写入私有主机名、用户名或本地安装路径。运行日志可以打印本机实际
选择的路径以便调试，但这些值只属于调用环境。

Release evidence 需要证明 Linux `io_uring` runtime 实际可用时，显式启用严格 gate：

```bash
FORGE_VERIFY_FLOOR_REQUIRE_IO_URING_RUNTIME=1 \
scripts/verify-selfhosted-floor.sh
```

严格 gate 会把 raw probe 的 exit 77 从有界 skip 升级为失败。它只适用于 Linux；Windows
主机和 `FORGE_VERIFY_FLOOR_WINDOWS` 不参与这项判断。普通 portable floor 保持可跳过行为。

## Source-style audit（源码风格审计）

`scripts/audit-source-style.sh` 对 tracked 的 C++、CMake、Shell 和 PowerShell
源文件执行低噪声检查：禁止 tab、尾随空白与 CRLF，并要求非空文件以换行结束。
`.editorconfig` 为支持它的编辑器固定同一组基础规则和四空格缩进。

该审计刻意不强制 formatter、命名风格或全仓行宽，避免机械重排标准措辞和模板代码。
`verify-selfhosted-floor.sh` 默认运行它；只在诊断验证脚本本身时才应跳过：

```bash
FORGE_VERIFY_FLOOR_SKIP_STYLE_AUDIT=1 scripts/verify-selfhosted-floor.sh
```

## Docs/example audit（文档与示例审计）

`scripts/audit-docs-and-examples.sh` 是轻量防回归脚本。它检查：

- docs / README 中引用的 `example/*.cpp` 是否真实存在，每个 example 是否至少被一处文档引用；
- `example/CMakeLists.txt` 中注册的 example source 是否存在；
- 每个 `example/*.cpp` 是否被 `example/CMakeLists.txt` 注册；
- docs / README 中的本地 Markdown link 是否仍能解析到文件；
- docs / README / example 中引用的 `<forge/...>` 头文件是否存在；
- committed docs/scripts/tests 是否泄漏私有主机名或本地路径；
- 非 WD 的 `std::execution::ensure_started` / `std::execution::start_detached`
  是否只出现在“已移除”说明中；
- 公开文档和 example 中带限定的 `sync_wait` 是否写作 `std::this_thread::sync_wait`
  （Forge 扩展入口 `forge::sync_wait` 与列出迁移后拼写的对照表除外）；
- 三语 README 的特性表、代码块和链接结构是否一致；
- `test/CMakeLists.txt` 声明的 `FORGE_TEST_ENABLE_*`、`forge.cmake` 声明的
  `FORGE_ENABLE_EXTENSIONS`，以及 `cmake/ForgeExtensions.cmake` 声明的四个
  `FORGE_ENABLE_FORGE_*` 是否与 `docs/testing.md` / `docs/install-options.md` 记载的集合完全一致；
- 治理守卫：`FORGE_FORCE_*_BACKPORT` 与 backport 定义的 `__cpp_lib_*` 宏有文档；
  标准 example 被对应的 backport 页面链接；`docs/roadmap/` 下各文档的状态行与索引一致；
  example 带用途头注释，且 env / bulk 写法
  可移植。每个守卫谓词都有一对正反自测样例。

该脚本不编译代码，也不证明 API correctness；它只负责文档和 example registry 的
机械一致性。`verify-selfhosted-floor.sh` 默认运行它；特殊情况下可设置：

```bash
FORGE_VERIFY_FLOOR_SKIP_DOC_AUDIT=1 scripts/verify-selfhosted-floor.sh
```

## 容器验证

完整验证入口：

```bash
scripts/verify-native.sh [gcc16|gcc16-slim|llvm|zig|local|gcc-exec|tsan|asan|all]
```

目标含义：

- `llvm`：LLVM/libc++ 容器，`-std=c++26`，覆盖 libc++ inject-path 全量测试
- `zig`：Zig 容器，C++23 backport inject path；Debug/O0 下保留 UB checks，
  使用 `-fsanitize=undefined -fsanitize-trap=undefined`。Zig 0.14 的 reporting
  runtime 会连带引入精度不足的 `long double` 数学实现并覆盖系统 libm，
  trap 模式避免这条 reporting 依赖，不放宽数值测试；违规仍立即失败，
  但没有详细 UBSan 报告。这只是该验证 lane 的 workaround，其他依赖仍
  可能引入 compiler-rt；详细 sanitizer 诊断由独立的 `asan` / `tsan` lanes
  提供。`test_simd_math_primitives` 检查实际链接后的
  `long double` 精度与范围
- `gcc16`：GCC 16 容器，在 C++26 验证 `std::simd`、pre-P4206
  `std::constant_wrapper` 与 padded mdspan layouts 的 partial-native stand-aside，
  以及 `std::submdspan` 的 complete-native stand-aside；构建并运行 linalg 测试组，
  验证 linalg backport 与 native padded layouts、native submdspan 的组合；另在 C++23 运行
  SIMD-only suite，确认
  declaration-free `<simd>` 不会被误判为 partial native，而会正常注入 backport
- `gcc16-slim`：GCC 16 C++26，断言上述五项 probe 状态，只构建并运行 `NativeHandoff`
  单翻译单元检查；配置时启用 linalg 测试组（包含 configure probes），但不构建或运行其测试目标；
  省去完整 Hive suite、嵌套配置测试、示例与 C++23 SIMD suite。
  默认验证使用此精简入口；需要完整覆盖时显式运行 `gcc16`
- `gcc-exec`：GCC 16 容器，覆盖 libstdc++ 上的 `std::execution` backport 与全部
  `forge::` runtime、resource、IO、erasure 测试子组，不跑 SIMD probes。
  io_uring 保留默认 `AUTO` 能力探测和 runtime exit 77 跳过规则
- `tsan`：LLVM/libc++ 容器，`-fsanitize=thread`，覆盖 execution 与 `forge::` 扩展子集
- `asan`：LLVM/libc++ 容器，`-fsanitize=address,undefined`，覆盖 execution、
  `forge::` 扩展子集与 Hive runtime/compile/model，并额外运行 7 个 focused SIMD
  memory/operator/math/special-math/bit tests；脚本精确校验选中数量，零匹配不能通过
- `all`（无参数时的默认值）：`gcc16-slim + llvm + zig + local + gcc-exec + tsan + asan`

SIMD special-math fallback 的结果证据必须来自 fallback 真正生效的 lane。当前
LLVM/libc++ lane 不定义 `__cpp_lib_math_special_functions`，因此 public
`std::simd` 调用会进入 Forge fallback；libstdc++ 通常定义该宏并把调用委托给
原生标量 special math，不能拿该 lane 的“backport 对比 `std::cyl_bessel_*`”
作为 fallback 正确性证据。测试同时直接调用内部 fallback，并在 libc++ lane
通过 public API 覆盖同一组数值，以避免原生分派让 oracle 空转。

手动运行容器时，使用：

```bash
podman run --rm --userns=keep-id -v "$PWD:/src:Z" -w /src ...
```

这样可以避免 root-owned build artifacts 和 orphaned containers。

## macOS SSH 验证

`bash scripts/verify-macos-ssh.sh` 通过 `FORGE_MACOS_HOST` 指定 SSH 主机，串行运行
C++23 和 C++26 的 configure/build/test，启用 tests 与 examples。主机需要 arm64 macOS、
支持两种语言模式的 Xcode command line tools（AppleClang、Apple libc++）、CMake/CTest
与 Make；Ninja 可选，存在时优先使用。远端执行前，脚本将
`FORGE_MACOS_PATH_PREFIX`（默认 `/opt/homebrew/bin:/usr/local/bin`）添加到 `PATH` 开头，
可用该变量指定自定义工具目录（冒号分隔，空值表示不添加）；不读取 login profile。
运行前须在主机安装 CMake（包含 `cmake` 与 `ctest`），例如使用 Homebrew 执行
`brew install cmake`；也可将 `FORGE_MACOS_PATH_PREFIX` 指向 portable CMake 的 `bin`
目录。可选的 Ninja 可通过 `brew install ninja` 安装；未安装时使用 Make。
脚本打印解析后的 Xcode、CMake、CTest 与 Ninja/Make 工具路径。验证辅助脚本还需要 Python 3.6+ 或支持
`realpath -m` 的工具。SSH 应支持非交互认证；脚本不调用 sudo，也不修改主机设置。

```bash
FORGE_MACOS_HOST=<macos-host> bash scripts/verify-macos-ssh.sh
FORGE_MACOS_HOST=<macos-host> FORGE_MACOS_REF=HEAD bash scripts/verify-macos-ssh.sh
```

发送端需要 Bash、Git、tar、SSH，并已初始化 `3rdparty/googletest`。默认上传 tracked 与
untracked-not-ignored 文件，保留未提交修改；GoogleTest 单独打包，排除 `.git` 元数据。
设置 `FORGE_MACOS_REF` 时上传本地可解析 ref 的归档及其 gitlink 指定的 GoogleTest commit，
发送端的 GoogleTest checkout 必须包含该 commit。远端使用独立临时目录，成功或失败均清理；
`FORGE_MACOS_KEEP=1` 可保留源码与构建目录供诊断。各步骤打印 `[macos]` 标签，失败状态
通过 SSH 返回；每个语言模式打印退出状态摘要，任一模式失败仍继续另一模式，最后返回
首个非零状态。configure/build 失败时跳过该模式的后续步骤。清理失败也会使原本成功的验证失败。

该 lane 覆盖 arm64、AppleClang、Apple libc++，包括 SIMD NEON 与 Hive runtime。
macOS/BSD 没有 `forge::io` OS readiness/completion backend，对应的 backend 测试与示例
targets 不构建；可移植的 byte-stream/coroutine targets 仍会构建。仅在 x86 上注册的
`native_abi_*` 测试也不在此 lane 覆盖范围内。源码归档没有 Git 元数据时，
`windows_ssh_transport` 先运行不依赖 Git 的断言，再以 exit 77 向 CTest 报告
真实 Git 集成检查不可用；checkout 中缺少 Git 时仍通过独立断言。

可选接入自托管基线：

```bash
FORGE_VERIFY_FLOOR_MACOS=1 FORGE_MACOS_HOST=<macos-host> scripts/verify-selfhosted-floor.sh
```

`macos_ssh_transport` 是离线 CTest，在独立 Bash 进程中使用 SSH/Git 替身检查步骤调度、失败状态、interrupt 与清理行为，
不连接主机，也不替代真实 macOS 构建。

## Windows/MSVC smoke（Windows 冒烟验证）

Windows 验证是可选的本机或 self-hosted smoke gate。它不替代 Linux/podman 全量矩阵；
当前目标是确认 MSVC 能 configure/build/test `std::execution` backport、
`forge::unique_resource`、`forge::` runtime utility 子集和 Windows IOCP backend，
以及独立 C++23 SIMD-only builds 的默认 x64 ISA 和 `/arch:AVX2` 路径。
这条 lane 的定位是可移植的 self-hosted floor：脚本不依赖 GitHub hosted CI，也不写入
私有主机名或工具链路径。Jenkins、Buildkite、GitHub self-hosted runner 或人工 SSH
调用都应使用同一组入口，只从外部传入 host、source/ref、MSVC discovery 信息和可选
CTest regex。

主入口是 Windows 主机上直接运行的 PowerShell 脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-windows-msvc.ps1 `
  -Vcvars "C:\path\to\VC\Auxiliary\Build\vcvars64.bat"
```

从 Linux/macOS 通过 SSH 调用远端 Windows 主机时，SSH wrapper 只是 transport
layer；真正的验证逻辑仍在远端执行同一个 PowerShell 脚本：

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VC_VARS='C:\path\to\VC\Auxiliary\Build\vcvars64.bat' \
scripts/verify-windows-msvc-ssh.sh
```

SSH wrapper 的参数只描述验证环境，不改变测试语义：

- `FORGE_WINDOWS_HOST`：远端 Windows SSH 主机；
- `FORGE_WINDOWS_VS_VERSION` / `FORGE_WINDOWS_VS_ROOT` / `FORGE_WINDOWS_VC_VARS`：
  MSVC discovery override；
- `FORGE_WINDOWS_REPO` / `FORGE_WINDOWS_REF`：远端 clone 模式的 source；
- `FORGE_WINDOWS_BUILD_NAME` / `FORGE_WINDOWS_GENERATOR` /
  `FORGE_WINDOWS_CONFIGURATION`：构建目录 basename、CMake generator 和配置；
- `FORGE_WINDOWS_USE_LOCAL_SOURCE=1`：把当前 worktree 打包到远端临时目录，适合验证
  尚未 push 的改动；
- `FORGE_WINDOWS_CTEST_REGEX`：focused smoke，例如 `forge_io_iocp`；
- `FORGE_WINDOWS_SKIP_GATE_CHECKS` / `FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK`：
  临时缩小 smoke；默认不建议跳过。

可通过 wrapper 运行自托管或手工 smoke 矩阵。默认 VS lane 为 `18`，默认使用 local-source 模式：

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VS_VERSIONS='18' \
scripts/verify-windows-msvc-matrix.sh
```

旧 MSVC lane 属于探索性验证。例如 `FORGE_WINDOWS_VS_VERSIONS='18 17'` 可测量 VS 2026 与 VS 2022 兼容性；不为旧 lane 改写现代 C++ 接口或降低代码生成要求。

默认 SSH wrapper 会在远端 clone `FORGE_WINDOWS_REPO`，并使用调用方仓库当前 `HEAD`
commit 作为不可变 ref；显式提供 symbolic `FORGE_WINDOWS_REF` 时，远端会在本次运行开始时
解析成 commit、detached checkout 并把 commit id 写入日志。验证本地尚未 push 的工作树时，
使用 local-source 模式；wrapper 只打包 tracked 和非 ignored 文件，不携带 `.git`、构建目录、
agent cache 或 GoogleTest submodule worktree；后者由 Windows verifier 按固定 gitlink commit
获取。wrapper 在传输前后校验、记录 archive SHA-256，运行结束后清理远端临时目录：

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_USE_LOCAL_SOURCE=1 \
FORGE_WINDOWS_CTEST_REGEX=forge_io_iocp \
FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK=1 \
scripts/verify-windows-msvc-ssh.sh
```

`FORGE_WINDOWS_CTEST_REGEX` 用于 focused smoke，例如只跑 `forge_io_iocp`。省略时使用
PowerShell 脚本默认的 `execution|unique_resource|std_target|forge`，其中
`unique_resource` 匹配 Forge 扩展的 CTest 名称。`FORGE_WINDOWS_KEEP=1`
可保留远端临时源码/clone 以便调试；不要把具体主机名、用户目录或工具链安装路径写进
仓库文档。

SSH transport 使用 PowerShell encoded command 传递参数；远端脚本只通过一次受控的
`vcvars64.bat` 调用导入 MSVC 环境，之后按 argv 启动 `git`、`cmake`、`ctest` 和编译器，
不会把路径或 CTest regex 再拼接成 `cmd /c` 字符串。没有 `.git` metadata 的 source
archive 若缺少 GoogleTest，会固定获取仓库 gitlink 对应的 commit，而不是跟随上游默认分支。

如果 Visual Studio 使用标准安装位置，也可以省略 `FORGE_WINDOWS_VC_VARS`，
让脚本按 `VsVersion` 或 `vswhere` 查找。脚本会打印 MSVC compiler version、
关键 Forge gate 状态和最终 CTest 数量。它还会做 gate 注册检查：
Windows 上 `FORGE_ENABLE_FORGE_IO=AUTO` / `ON` 应启用 IOCP backend 并注册
backend-specific `forge_io|example_forge_io` tests/examples，`FORGE_ENABLE_FORGE_IO=OFF`
应在同一 backend regex 下注册 0 个 tests/examples。`forge_coro_*`、memory stream 和
byte vocabulary 等 backend-free IO tests/examples 不属于这个 OS backend gate。
因此 `forge_io` / `example_forge_io` 测试名前缀保留给 backend gate 控制的注册；
backend-free 的 IO/coroutine 设施测试必须取其它前缀（如 `forge_coro_*`），否则
该审计会在 OFF gate 下误报。
gate 检查还会实际 build/run 少量稳定 examples，例如 IOCP example。这保证 Windows
smoke 覆盖 cookbook 的关键路径，而不把所有 examples 都塞进默认主测试。脚本避免使用单个硬编码全局 CTest 数量作为验收标准，
因为测试总数会随覆盖增长而变化；但 targeted regex lane 会先断言至少选中 1 个测试，
gate-off 检查则显式断言注册数为 0。可设置 `FORGE_WINDOWS_SKIP_GATE_CHECKS=1` 临时跳过
这些 gate 检查。默认 smoke 也会执行 install package consumer check；可设置
`FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK=1` 临时跳过。

默认另建 `<build-name>-simd-default` 和 `<build-name>-simd-avx2`，只启用
`FORGE_TEST_ENABLE_SIMD=ON`（其余测试组关闭），使用 C++23，第二个 build 经
`CMAKE_CXX_FLAGS="/EHsc /arch:AVX2"` 选择 ISA 并保留异常展开。两者均执行 configure probes、编译 runtime
targets，并用独立 `ctest -R simd --output-on-failure` 运行 runtime 和 compile tests，
不受主 smoke 的 `FORGE_WINDOWS_CTEST_REGEX` 影响；每个 build 先检查注册数非零。
AVX2 运行要求支持该 ISA 的 x64 主机。可用 PowerShell `-SkipSimdChecks` 或 wrapper
`FORGE_WINDOWS_SKIP_SIMD_CHECKS=1` 跳过（matrix 同样转发该环境变量）。
已在 VS 2026 Build Tools / MSVC 19.51、x64、Debug、Ninja 配置验证：主 smoke
91/91，SIMD 默认 ISA 69/69，SIMD `/arch:AVX2` 69/69，install-package smoke 通过。
`/arch:AVX`、`/arch:AVX512`、ARM64、x86 32-bit 和 clang-cl 尚未验证。
现有 CMake guards 对无 `__int128` 的编译器跳过对应 rejection
probe，GNU/Clang 专用 mixed-ISA fixture 因 `cpuid.h` / `-m` flags 不在 MSVC 注册；
其余 SIMD tests 没有 MSVC 专属排除。

主 smoke 默认启用可用的 Forge IO backend，并关闭 SIMD/submdspan/linalg/native-handoff
测试：

```cmake
FORGE_ENABLE_FORGE_IO=AUTO
FORGE_TEST_ENABLE_SIMD=OFF
FORGE_TEST_ENABLE_CONSTANT_WRAPPER=ON
FORGE_TEST_ENABLE_SUBMDSPAN=OFF
FORGE_TEST_ENABLE_LINALG=OFF
FORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
```

已验证的 Windows baseline 是 VS 2026 Build Tools / MSVC 19.51。VS 2022 /
MSVC 19.44 可以 configure，但在 P2300/domain/write_env 的 constrained CPO
模板路径上仍有编译器兼容缺口；它目前不是强制 gate。

## Optional stdexec feasibility probe（可选 stdexec 探针）

`scripts/probe-stdexec-feasibility.sh` 是本地 spike 工具，不属于默认门禁。
它要求调用者提供 `STDEXEC_ROOT=/path/to/stdexec`，验证本地 stdexec checkout
能编译最小 smoke program，同时运行一组命名的 Forge execution/utility checks
（`sync_wait`、`wait_result` typed error、`erased_sender` typed error、
`any_scheduler` 和 receiver env stop-token 传播）。它不 fetch、不 vendor，也不
声明 stdexec 已经是 Forge 的 native `std::execution` handoff lane。
未设置 `STDEXEC_ROOT` 时脚本返回 77 并打印 `result=skipped`，方便把它接入本地
可选验证而不把 skip 误判为失败。

## Native handoff 与 partial-native guards

改 wrapper、probe 或 feature-test macro 时，优先使用 lane-specific evidence，不要只看
单个全局 CTest 数量：

- `scripts/verify-native.sh gcc16` 同时检查 GCC 在 C++26 已暴露标准库特性时的
  complete/partial-native stand-aside path，以及同一工具链在 C++23
  declaration-free `<simd>` 上的 backport inject path。
- `scripts/verify-native.sh llvm` 和 `scripts/verify-native.sh zig` 检查仍缺这些特性
  的工具链上的 backport inject path。
- `scripts/verify-native.sh gcc-exec` 是当前 libstdc++ execution backport lane；native
  `std::execution` 还不是常规 mainstream lane。
- `scripts/probe-stdexec-feasibility.sh` 只是 optional reference probe，因为 stdexec 使用
  自己的 `stdexec::` headers 和 namespace。

Force-backport flags 只用于诊断。在 partial-native toolchain 上，它们可能制造
`namespace std` redefinition / ODR hazard，因此 forced configuration 不能作为
production handoff safety 的证据。

## Runtime wakeup audit helper（唤醒审计）

`scripts/audit-runtime-wakeups.sh` 列出 `include/forge` 与 execution backport 中的 condition-variable、notify、stop callback 和取消位置，用于修改取消或 shutdown 后的人工检查，不能替代正确性验证。关键规则是：

> 如果 waiter 在 mutex 下观察 predicate，那么修改该 predicate 也应在同一把 mutex 下
> publish，然后再 `notify_one` / `notify_all`。

Atomic predicate 加 unlocked notify 仍可能 lost wakeup。

当前 high-risk wakeup coverage map：

| 区域 | 代表性 wakeup / cancellation site | 覆盖 |
| --- | --- | --- |
| execution scope join | `simple_counting_scope` / `counting_scope` join registration 和最后一次 association release | `execution_counting_scope` deterministic tests 与 `execution_counting_scope_stress` 覆盖 empty-inline、delayed start-scheduler handoff、`join start` vs last release、多 joiner、completion self-destroy，以及 join completion 内再次启动 join |
| `forge::timer_context` | deadline wakeup、per-item stop callback、`wait()` pending drain | `forge_timer_context` 覆盖 long-deadline cancellation、stop-vs-deadline、self-destroying receiver 和 repeated prompt wake；`forge_runtime_stress` 竞争 short deadline completion 与 receiver stop |
| `forge::bounded_channel` | pending send/recv stop callback 与 close/stop drain | `forge_channel` 覆盖 rendezvous、backpressure、close-drain、cancellation 和 self-destroying receiver；`forge_runtime_stress` 竞争 pending recv/send stop callback 与 direct send/recv handoff |
| `forge::async_scope` | active-count drain 与 request-stop propagation | `forge_async_scope` 覆盖 spawn lifetime、blocking destructor/wait、stop propagation 和 first-error handling；`forge_runtime_stress` 竞争 `wait()` 与 last scheduled completion |
| `forge::strand` / pool | serialized queue drain、shutdown 和 worker handoff | `forge_strand`、`forge_thread_pool` 和 scheduler roundtrip tests 覆盖 FIFO、reentrant scheduling、bounded queues、shutdown 和 worker self-wait；`forge_runtime_stress` 覆盖 concurrent strand scheduling 与 pool schedule-vs-shutdown completion |
| `forge::io` | backend stop callback、pending record completion 和 context drain | `forge_io_*` 覆盖 exactly-once completion、typed error、stop/drain，以及支持处的 self-destroying receiver |

这张表是 representative map，不是“每一条 audit line 都有独立 stress”的证明。修改
wakeup site 后，应指向一个覆盖同类 predicate/notify discipline 的现有测试，或补一个
focused stress case，再把该 audit 视为 closed。

## 测试分组开关

测试子目录可用 CMake 开关独立启停，默认全开：

- `FORGE_TEST_ENABLE_EXECUTION`
- `FORGE_TEST_ENABLE_SIMD`
- `FORGE_TEST_ENABLE_HIVE`
- `FORGE_TEST_ENABLE_CONSTANT_WRAPPER`
- `FORGE_TEST_ENABLE_UNIQUE_RESOURCE`
- `FORGE_TEST_ENABLE_SUBMDSPAN`
- `FORGE_TEST_ENABLE_LINALG`
- `FORGE_TEST_ENABLE_FORGE`
- `FORGE_TEST_ENABLE_FORGE_RUNTIME`
- `FORGE_TEST_ENABLE_FORGE_RESOURCE`
- `FORGE_TEST_ENABLE_FORGE_IO`
- `FORGE_TEST_ENABLE_FORGE_ERASURE`
- `FORGE_TEST_ENABLE_NATIVE_HANDOFF`

`FORGE_TEST_ENABLE_UNIQUE_RESOURCE` 独立控制 `forge::unique_resource` 的 runtime
测试、编译探测及配置阶段的旧接口移除检查。配置探测验证 Forge 入口，并分别检查
旧的实验性命名空间和标准命名空间中均无法查找到类模板与工厂函数。

`FORGE_TEST_ENABLE_CONSTANT_WRAPPER` 覆盖 P2781/P3978/P4206 的专用 compile-time
与 runtime matrix。检测到 pre-P4206 partial-native 声明时该组会跳过，因为 Forge
必须让位且不能在同一 `namespace std` 中补声明；`gcc16` lane 另外断言这个
stand-aside 决策。纯 execution、TSan 和 ASan 子集会显式关闭该组，完整 `llvm`、
`zig` 与本地完整 lanes 则保留覆盖。Windows 主 smoke 保留该组的 CMake 默认 ON，
但默认 CTest regex `execution|unique_resource|std_target|forge` 不选择
constant_wrapper 专用 tests，因此不计作该组的 runtime / CTest compile-probe 覆盖；
需要时显式用 `FORGE_WINDOWS_CTEST_REGEX=constant_wrapper` 运行。独立 SIMD builds
关闭该组。

Execution、SIMD 与 linalg 的 conformance suites 会在对应 Forge backport 实际注入时
运行；完整或 partial native 实现触发 stand-aside 时，这些依赖 Forge 私有测试辅助符号
的 suites 会跳过，native handoff fixtures 负责验证探测和让位边界。
x86-64 上的 GCC/Clang 在 SIMD backport 实际注入且配置主机能执行 AVX2 与 POPCNT 时，
另以 `-mavx2 -mpopcnt` 构建 `test_simd_mask_int_avx2`，覆盖只在启用这两项 ISA 扩展时
才使用的 mask 归约路径；交叉编译或主机不支持时不注册。

`FORGE_TEST_ENABLE_HIVE` 控制 Hive 测试组。backport、完整或 partial native 下均注册
`hive_portable_runtime`、可移植编译检查和 `hive_test_gate_matrix`；可移植测试仅依赖
工作草案的公开 API 与语义保证。`hive_runtime`、`hive_multi_tu`、实现专属编译检查及
model/mutation 等实现测试仅在启用 backport 时注册。Native handoff 的 synthetic
fixtures 检查上述注册集合，只构建共享头文件路径的 `hive_consumer`，不编译或运行
可移植测试；其中不完整或故意损坏的模拟头文件只用于验证探测与让位边界，
不证明真实标准库实现的质量。`FORGE_FORCE_HIVE_BACKPORT` 仅供
诊断，在已有 native 声明时可能导致重定义或 ODR 问题。

Hive 基准运行方法见[测量说明](../test/hive/benchmark/MEASUREMENT_PROTOCOL.txt)。

`FORGE_ENABLE_EXTENSIONS` 在有扩展头文件目录时默认 ON。设为 `OFF` 时不创建
`forge::forge`，不运行平台 IO probe，也不注册 `forge_` 示例、`test/forge` 或
`test/unique_resource`。包含完整产品集成 fixture 的 native-handoff 组也不注册，
其余标准测试组仍可运行，测试注册和 configure probe 默认采用 `forge::std`。

`FORGE_TEST_ENABLE_FORGE` 是 `include/forge/` extension tests 的 parent switch。
更窄的 `FORGE_TEST_ENABLE_FORGE_*` 开关默认仍启用当前测试，同时允许 future
resource、IO 和 erasure subsets 被独立配置。Resource-policy tests 还要求
`FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON`。

也可以独立设置 Forge extension feature gates：

- `FORGE_ENABLE_FORGE_RUNTIME`
- `FORGE_ENABLE_FORGE_RESOURCE_POLICY`
- `FORGE_ENABLE_FORGE_IO`
- `FORGE_ENABLE_FORGE_IO_URING`

`FORGE_ENABLE_FORGE_IO=AUTO` 在平台支持时启用 Linux epoll/eventfd backend 或 Windows
IOCP backend，其它平台跳过 OS backend tests/examples。`ON` 要求 supported backend，
缺失时 configure 报错；`OFF` 跳过 OS backend、`forge::io::context` 和
backend-specific tests/examples。Backend-free IO/coroutine tests 仍由
`FORGE_TEST_ENABLE_FORGE_IO` 控制。Erasure facilities 是 header-only 且总是可用；用
`FORGE_TEST_ENABLE_FORGE_ERASURE` 控制是否运行对应测试。

`FORGE_ENABLE_FORGE_IO_URING` 是不参与 portable epoll/IOCP selection 的 Linux
completion-backend gate，但从属于父 gate `FORGE_ENABLE_FORGE_IO`：父 gate `OFF` 时
io_uring backend 与其 tests/examples 一并关闭，`FORGE_ENABLE_FORGE_IO_URING=ON`
与父 gate `OFF` 的组合是 configure 错误。`AUTO` 在 Linux UAPI header、
setup/enter/register syscall numbers、`NOP`/`READ`/`WRITE`/`ASYNC_CANCEL` 和
`NODROP` compile probe 通过时启用；`ON` 缺任一 build requirement 会 configure 失败；
`OFF` 不执行该 probe，也不注册 io_uring tests/examples。Runtime kernel 或 sandbox
capability 由专用 probe/test 处理，不在 configure 时执行 target binary。

io_uring runtime tests 在受限沙箱（如容器默认 seccomp 阻断 `io_uring_setup`）下检测
构造期 `ENOSYS`/`EPERM`/`EACCES` 并以 exit 77 skip；宿主或放行 io_uring 的容器 lane
才执行完整 runtime 断言。容器内的放行验证通道使用
`scripts/probe-io-uring-container.sh`（自定义 allow-io_uring seccomp profile）：默认
只跑 raw syscall probe；设置 `FORGE_IO_URING_TEST_BUILD_DIRS=<build-dir...>`（配合
`FORGE_IO_URING_PROBE_IMAGE` 选择匹配二进制的镜像）时在同一容器内继续运行这些
build 目录下的 io_uring context/read-write tests，且此处 skip 视为失败——probe 刚证明
环境可用。raw probe 本身若以 77 报告 kernel/policy 不可用，则整段 runtime 扩展作为
有界 skip 返回成功；设置 `FORGE_IO_URING_REQUIRE_RUNTIME=1` 时，同一个 77 会成为失败，
用于 release gate 明确区分 runtime unavailable。所有 probe/test 都有 120 秒默认上限，可用
`FORGE_IO_URING_TEST_TIMEOUT_SECONDS` 调整。`scripts/verify-native.sh` 的 `tsan` 与
`asan` lane 在构建出 io_uring tests 时自动走这条通道，并转发各自 sanitizer options
与 ptrace capability，因此 io_uring runtime 使用和主 lane 相同的诊断/停止策略。
严格模式仍是 Linux-only；它与任何 Windows/MSVC 验证无关。

`forge_io_uring_fault` 是链接器支持 `--wrap=syscall` 时额外注册的注入式测试：拦截
submit-only 的 `io_uring_enter`（不带 GETEVENTS flag 且 `to_submit > 0`）注入
`ENOMEM`/`EBUSY`，钉住数据 SQE flush 硬失败时基于 shared head 的 ownership 判定、
未消费尾项的安全拒绝（`no_buffer_space`）、已消费项保持注册直到 CQE、poller 不绕过
submission mutex、生命周期唤醒链自愈，以及唤醒 NOP 无法发布进饱和 SQ 后的重驱动。
链接器不支持 `--wrap` 时该目标不注册（configure 输出提示），容器
放行通道对缺失的该二进制打印说明而不失败。

## 已移除的 API 与标准形式检查

配置阶段以实际调用检查 wait 消费者的 Mandates。一个 value completion signature
的正控制必须编译，零个、多个及不是 sender 的调用必须编译失败。domain 正控制返回规定的
optional tuple，错误返回类型必须编译失败。具有相同 decayed tuple
的两个签名也属于多个签名。正控制还检查 `forge::sync_wait` 与标准对象地址相同。
`sync_wait_with_variant` 的正控制覆盖默认调用和对原 sender 的 domain dispatch，
并断言单签名得到 `optional<variant<tuple<int>>>`，两个 value 签名得到
`optional<variant<tuple<int>, tuple<double>>>`，两种结果均排除草案字面规定的嵌套类型。
domain 返回错误的 variant 类型或草案的嵌套类型时必须编译失败，stopped-only 和
error-only sender 的默认调用也必须编译失败。
`execution_when_all` 检查多 value child 使 `when_all` 的 `sender_in` 为 false，
并以 `when_all_with_variant` 为正控制；多形态 runtime 测试保留实际 variant 内容与
静态 operation-state 分配检查。零 value 的 stopped、error 测试直接连接并启动
operation，由 receiver 记录相应 completion channel。

`execution_removed_forms_compile` 用 requires-expression 或概念检查已移除的重载与 pipe
形式，同一源文件包含标准正控制。`execution_removed_forms` 运行两阶段 domain
transform 的选择回归。`execution_standard_forms_compile` 构建标准调用示例，
`execution_standard_forms` 运行这些调用。
配置阶段通过 [标准移除探针](../test/execution/compile/test_execution_reject_removed_names.cpp)
和 [Forge 移除探针](../test/forge/compile/test_forge_reject_removed_observers.cpp)
分别检查第 1–33 项和第 34–35 项。未显式限定的名称按
`std::execution` 中的旧拼写检查，scope 成员按所属类型检查。

| 编号 | 检查移除的名称或调用形式 |
| --- | --- |
| 1–7 | `empty_env`、`make_prop`、`transfer_just`、`env_domain_t`、`receiver_domain_t`、`sender_domain_t`、`read_env_t` |
| 8–12 | `value_types_of`、`error_types_of`、`sends_stopped_v`、`receiver_of` 的旧调用形式、`sync_wait_with_variant` |
| 13–19 | `forwarding_query`、`forwarding_query_t`、`get_stop_token`、`get_stop_token_t`、`get_allocator`、`get_allocator_t`、`stop_token_of_t` 的旧命名空间拼写 |
| 20–29 | `receiver_t`、`sender_t`、`operation_state_t`、`scheduler_t`、`make_env`、`queryable`、`sender_to`、`std::stoppable_token_for`、`split`、`sync_wait` |
| 30–33 | `simple_counting_scope` 与 `counting_scope` 各自的 `count()` 和 `is_closed()` |
| 34–35 | `forge::scope_count` 与 `forge::scope_is_closed`，分别检查 |

每项正向对照都必须编译并链接为可执行文件。前 29 项使用有效的标准形式作对照，第 30–33 项检查对应 scope 的标准 `close()` 和 `join()`。
第 34 和 35 项的对照包含仍可用的 `<forge/execution_extensions.hpp>` 并验证
`forge::transfer_just`，以确认头文件和保留的接口仍可用。该函数不是快照观察接口的迁移目标。
配置期反例使用静态库目标，以编译错误判定拒绝。MSVC、GCC 和 Clang 系工具链在这些反例中
禁用警告，同组选项下只有警告的控制代码必须能够编译。
配置 MSVC 测试探针时，请勿在 `CL` 或 `_CL_` 环境变量中设置 `/WX`，因为 MSVC 会将
`CL` 的选项加在命令行选项之前，将 `_CL_` 的选项加在命令行选项之后。
`forge_serial_bulk` 覆盖 Forge 三种串行算法的 direct 与 pipe 调用、索引顺序、
chunk 边界、stopped 与 error 传递，在 ASan 和 TSan lanes 均运行。

## Example smoke tests（示例冒烟）

当 `FORGE_BUILD_EXAMPLES=ON` 且 `FORGE_BUILD_TESTS=ON` 时，实际构建出来的 examples
也会注册为 CTest smoke tests，名称为 `example_<target>_smoke`。这样 cookbook 中的路径
不是只 compile-check，而是可执行的 smoke。受 feature gate 控制的 examples，例如
platform IO 或 mdspan-based linalg examples，只在对应 target 存在时注册 smoke。

Example smoke tests 按决定 target 是否构建的 feature gates 分组。它们不逐一镜像所有
`FORGE_TEST_ENABLE_FORGE_*` 细分测试开关。这样 examples 聚焦 public build surface，
test tree 则继续保留 runtime、resource、IO 和 erasure coverage 的细粒度启停。

Focused example 检查：

```bash
ctest --test-dir build/local -R '^example_' --output-on-failure
```

## Backend proof gates（后端验证 gate）

可选 backend proof 需要同时验证 registration shape 和 runtime tests。对一个
backend feature gate：

- `AUTO` 只在 probe 成功时注册 backend tests/examples；
- `ON` 要求 backend 可用，缺失时 configure 失败；
- `OFF` 应注册 0 个 backend tests/examples。

使用 regex-specific checks，不要依赖 global test count。例如：

```bash
cmake -S . -B build/gate-io-off -G Ninja \
  -DFORGE_BUILD_TESTS=ON \
  -DFORGE_BUILD_EXAMPLES=ON \
  -DFORGE_ENABLE_FORGE_IO=OFF
ctest --test-dir build/gate-io-off -N -R 'forge_io|example_forge_io'

cmake -S . -B build/gate-io-uring-off -G Ninja \
  -DFORGE_BUILD_TESTS=ON \
  -DFORGE_ENABLE_FORGE_IO_URING=OFF
ctest --test-dir build/gate-io-uring-off -N -R 'io_uring'
```

未来 platform/vendor backend proofs 也必须记录 focused tests、sanitizer expectation、
install-package behavior，以及任何 manual/self-hosted platform smoke。不要把私有
hostname 或本地安装路径写进已提交的 docs/scripts。

focused execution 示例：

```bash
cmake --build build/llvm --target test_execution_wave1
ctest --test-dir build/llvm -R 'execution_wave1' --output-on-failure
```

## CMake consumer / install package smoke（CMake 消费与安装包冒烟）

Install package smoke 同时验证三种 header-only CMake 消费方式：直接
`include(forge.cmake)`、把源码树作为 subproject `add_subdirectory`，以及从独立项目
调用 `find_package(CCForge CONFIG REQUIRED)`。前两种方式会 configure/build/run
`test/source_consumer`；安装方式会把 headers、backport wrappers 和 CMake package
config 安装到临时 prefix，再 configure/build/run `test/install_consumer`。

三条路径都会断言没有创建 GoogleTest target；安装路径还会检查 prefix 中没有
GoogleTest artifact。`3rdparty/googletest` 只用于 CC Forge 自身的顶层测试构建，普通
header-only consumer 不需要初始化该 submodule：

```bash
scripts/verify-install-package.sh
```

默认使用 `Ninja` 和 `Debug`。可通过 `FORGE_INSTALL_CMAKE_GENERATOR`、
`FORGE_INSTALL_CONFIG` 和 `FORGE_INSTALL_CXX_STANDARD` 覆盖；脚本会对 single-config
和 multi-config generator 使用同一选定配置。例如：

```bash
FORGE_INSTALL_CMAKE_GENERATOR='Ninja Multi-Config' \
FORGE_INSTALL_CONFIG=Release scripts/verify-install-package.sh
```

只验证标准安装时可以单独运行：

```bash
scripts/verify-install-package.sh --standard-only build/install-package-standard
```

该场景从没有 `include/forge` 的源码副本安装到新前缀，移动前缀后只用
`forge::std` 构建外部消费者。它还检查导出、语言 guard、无平台 IO probe、
无扩展头文件，以及消费者显式要求开启扩展时的诊断。

这个检查故意不放进 default CTest，因为它会执行第二轮 configure / install / build cycle。
修改 CMake packaging 或 install layout、以及 release-oriented change 前应运行它。

## 注意事项

- 只有构建 CC Forge 自身测试时，`test/CMakeLists.txt` 才需要
  `3rdparty/googletest`；fresh checkout 可用
  `git submodule update --init --depth 1 -- 3rdparty/googletest` 初始化
- 不要意外 stage vendored/untracked `3rdparty/` 内容
- SIMD configure probes 在 CMake configure 阶段运行；configure failure 可能是 probe failure，还没进入 build target
- `linalg` 和 `submdspan` tests/examples 依赖 `<mdspan>`，在较旧标准库上会跳过
- 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`；Forge tests/examples 会在检测到 `tbb` 时自动链接

## 示例与负面编译测试的解释

无编译器主机请显式选择容器目标 `scripts/verify-native.sh gcc16 llvm zig gcc-exec tsan asan`，排除依赖主机工具链的 `local`；不要使用包含 local 的默认 all。
所有容器流程串行执行，不在同一工作树并发运行。镜像对应关系见[容器说明](../containers/README.md)。
负面编译测试分为两类：固定工作草案规则（例如 noexcept completion、receiver 不抛异常移动）与固定 Forge 自身子集偏差（例如 creation 时延迟验证的形状）。测试通过只说明拒绝了该输入，不代表所有拒绝都是标准规则。


gcc16 的示例流程启用 `FORGE_BUILD_TESTS=ON`、关闭全部 `FORGE_TEST_ENABLE_*` 测试组、构建示例后运行 `ctest --test-dir build/gcc16-examples -R '^example_' --output-on-failure`，因此 libstdc++ 上的示例也有运行覆盖。

`FORGE_ENABLE_FORGE_RUNTIME` 只控制 runtime 示例；runtime 测试由 `FORGE_TEST_ENABLE_FORGE_RUNTIME` 独立控制，公共头文件始终可包含。文档审计还覆盖三语 README 一致性、测试开关文档、路线图状态、特性宏、用途头与扩展拼写，并逐项运行正反自测。

## Execution 成员定制测试

`execution_member_dispatch_compile` 检查 ADL-only 的 completion、start、connect、
schedule、signatures 及环境查询不可作为定制，并配对成员形式。一个同时是
awaitable 的旧 sender 走 awaitable fallback。普通环境测试检查 ADL-only
`get_env` 返回 `env<>`、stop token 使用 `never_stop_token`、缺失 scheduler
及 domain 默认规则。配置期 `FORGE_EXECUTION_QUERY_MEMBER_MANDATE_*` 检查
throwing / 错误结果查询、无效完成签名等硬错误。用例 10、11 分别拒绝可能抛出的
forwarding member 和返回 `int` 的 forwarding member，各自有只修改目标条件的正控制。

`execution_env_queries_compile` 固定环境的具名 query、借用子环境的 cv/ref、引用结果、
variadic 参数、条件 `noexcept` 及成员选择。Completion scheduler / domain 探针覆盖
TRY-QUERY、递归终止、HIDE-SCHED 以及 `continues_on` destination attributes 优先于
scheduler 的 domain 选择。为 child 提供各 completion domain 并使 destination 查询缺失，
以检查 child domain 不经转发泄漏。Schedule sender 及其 `write_env` 包装还覆盖
`connect` / `start` 对 receiver 复制与移动的异常规格。

`execution_scheduler_env_queries` 覆盖不同 loop 的 scheduler identity、inline
scheduler 和 attrs 的环境相关答案、stop token 传递及三个 `let` adaptor 的
completion-scheduler 选择。在 receiver 缺少 scheduler 时，inline predecessor
的 start scheduler 仍为 inline scheduler。SCHED-ENV 控制检查直接 `get_domain` 在保存的 scheduler 的 const 左值上查询、指向 scheduler
内部的 domain 答案的生存期、重复的按值查询不消耗保存的 scheduler，以及复制构造声明为 `noexcept(false)` 的 scheduler 仍得到
非抛出的 `get_start_scheduler` 答案。`execution_borrowed_env` 通过公开的 `let_*`、
`starts_on`、两种形式的 `on`、`continues_on`、`write_env`、共享 adaptor、
`stopped_as_error`、`affine`、锁定的 `associate` 与用户包装 sender 检查 successor / child
环境类型只取决于去除引用与 const 的环境类型（借用、右值引用和按值的环境得到同一类型，签名与
连接对非 volatile 的环境一致；volatile 环境的左值与右值引用被借用并以 const volatile 查询）、
物化环境由 `get_env` 的纯右值直接初始化并归 operation state 所有（转发构造函数的环境与库的
tag 类型）、视图副本在原视图销毁后仍有效且不调用环境的复制或移动、各 adaptor
对 child 的参与检查、数组环境，以及静态 signature 成员的常量求值。配置期
query Mandate 探针的第 36 例检查非常量 `forwarding_query(Q{})` 得到约束诊断。
第 37–39 例各有配对的正控制，分别拒绝可能抛出的 `get_scheduler_allocator` 成员、
经该 CPO 回退到可能抛出的 `get_allocator` 成员，以及不满足 `simple-allocator` 的结果。
反例只在依赖的 `requires` 表达式允许调用时调用 CPO，以区分硬性 Mandate 与约束移除。
`execution_wave1` 使用不同的
predecessor 与 receiver scheduler，分别检查 `get_start_scheduler` 和 `get_scheduler`。
`execution_query_conformance_compile` 与 `execution_query_conformance` 检查 query
命名空间、constexpr、env/prop 的 CTAD 与赋值约束、scheduler 和 receiver 类别、
domain 函数及 utilities、scheduler 递归和环境单次求值。
Allocator 查询用例检查专用成员的优先级、普通 allocator 回退、缺失时不可调用、
constexpr forwarding 分类、借用结果的引用身份，以及经 FWD-ENV 与 `then` 转发的
`prop`/`env` overlay。具名查询的控制通过 `consteval` 转换取得全局 query object 的地址，
包含从 CPO 副本调用、`get_scheduler` 的底层成员只求值一次、completion-scheduler
初始 tag 与递归 value tag、pack 优先及 unary 回退、同型相等与不等、跨类型递归和终端借用引用。
`get_scheduler_allocator` 的两个分支均验证所选对象。两种 allocator 查询均接受 fancy pointer
和非 `void` deallocate。

普通 Forge runtime、erasure 和 IO 测试使用成员定制。`forge_task_member_queries`
涵盖 task promise 的查询。`forge_member_queries` 编译探针随
`FORGE_TEST_ENABLE_FORGE_RUNTIME`、`FORGE_TEST_ENABLE_FORGE_ERASURE` 和
`FORGE_TEST_ENABLE_FORGE_IO` 的任一组启用，并只检查已启用的组。

`forge_execution_extensions` 使用依赖 scheduler 查询的 source，检查 `forge::split`
在有无 scheduler 的订阅环境中都公布空环境下的缓存签名，并验证实际交付。
它还检查同一 source 类型的左值和右值连接各自使用对应的 operation 类型与 completion signatures。
`forge_split_allocations` 验证所选 allocator、唯一一次 start、交付和 teardown。
非 TSan lane 另验证没有全局分配。TSan 运行同名用例但不替换全局分配函数。
`forge_member_queries` 的 backport 专用编译用例检查仅支持右值 `co_await` 的可复制 awaitable
以左值传入时被 `forge::split` 拒绝，以右值传入时被接受。
完整原生 senders 路径下，`forge_execution_extensions_native_compile` 构建原生探针，
`forge_execution_extensions_native` 运行同一程序，检查惰性缓存、单次执行与重放。

## Protocol model check（协议模型检验）

[formal/](../formal/README.md) 保存不进入 CMake test build 的协议模型。
[split](../formal/split/README.md) 锁定 `<forge/split.hpp>`，覆盖惰性缓存广播、共享所有权与 abandonment；[when_all](../formal/when_all/README.md) 覆盖 child credit、取消与结果仲裁；[run_loop](../formal/run_loop/README.md) 覆盖 queue/wait 与 teardown；[sync_wait](../formal/sync_wait/README.md) 覆盖 result publication 与 stack lifetime；[spawn](../formal/spawn/README.md) 覆盖 detached state 与 allocator/association 顺序；[associate](../formal/associate/README.md) 覆盖 sender/operation association 与 receiver 自毁；[spawn_future](../formal/spawn_future/README.md) 覆盖 future 的完成、取消及所有权；[counting_scope](../formal/counting_scope/README.md) 覆盖 join、scope stop 和 fused callback；[stop_token](../formal/stop_token/README.md) 覆盖底层 stop 协议。
Python explicit-state checker 要求 **Python 3.9+**，标准库即可；此要求独立于既有 helper 的 Python 3.6+ 要求。

```bash
python3 -B formal/split/check.py --check-source
python3 -B formal/split/check.py --tier quick
python3 -B formal/split/check.py --tier full --jobs 8
python3 -B formal/when_all/check.py --check-source
python3 -B formal/when_all/check.py --tier quick
python3 -B formal/when_all/check.py --tier full --jobs 8
python3 -B formal/run_loop/check.py --check-source
python3 -B formal/run_loop/check.py --tier quick
python3 -B formal/run_loop/check.py --tier full --jobs 8
python3 -B formal/sync_wait/check.py --check-source
python3 -B formal/sync_wait/check.py --tier quick
python3 -B formal/sync_wait/check.py --tier full --jobs 8
python3 -B formal/spawn/check.py --check-source
python3 -B formal/spawn/check.py --tier quick
python3 -B formal/spawn/check.py --tier full --jobs 8
python3 -B formal/associate/check.py --check-source
python3 -B formal/associate/check.py --tier quick
python3 -B formal/associate/check.py --tier full --jobs 8
python3 -B formal/spawn_future/check.py --check-source
python3 -B formal/spawn_future/check.py --tier quick
python3 -B formal/spawn_future/check.py --tier full --jobs 8
python3 -B formal/counting_scope/check.py --check-source
python3 -B formal/counting_scope/check.py --tier quick
python3 -B formal/counting_scope/check.py --tier full --jobs 8
python3 -B formal/stop_token/check.py --check-source
python3 -B formal/stop_token/check.py --tier quick
python3 -B formal/stop_token/check.py --tier full --jobs 8
```

文档审计检查 source lock；verification floor 默认运行每个模型的 quick tier，
可用 `FORGE_VERIFY_FLOOR_SKIP_FORMAL=1` 跳过。lock 只检测变更；更新前必须审查
C++ 与模型的对应关系。C++ 变化不需要修改模型时可用 quick，assessment 必须说明模型为何不变。
任何 checker 文件（包括 CLI）或 expected 数据变化都要求 full。quick 必须沿用上次 full 的 checker 和 expected-data digests；
lock 同时保留最近资格运行和上次 full 的日期、配置数与结果摘要。
Python 不变时 full 会重现相同结果，因此 C++ 单独变化的真正保障是 assessment 中的对应关系审查，tier 本身不能证明 fidelity。
头文件的每次编辑（包括注释和空白）都要求刷新 source lock；source digest 是仅将 CRLF 转成 LF 后的字节 SHA-256。纯注释或纯空白改动可用 quick 重新确认，assessment 必须明确改动类型并说明模型为何不受影响。
维护命令及 assessment 规则见 [stop_token 模型说明](../formal/stop_token/README.md)。


### Linalg 完整重载回归

`FORGE_TEST_ENABLE_LINALG=ON` 且基础 `<mdspan>` 可用并定义 `__cpp_lib_mdspan` 时，`linalg_feature_macro_compile`
检查注入时的特性宏。Backport 注入时，`linalg_level1` 检查标准范数与 Givens 旋转，
包括复数与访问代理。
非 MSVC 模式下，基础 `<mdspan>` 可用且 native handoff 测试组启用时，
`native_handoff_probe_surface_linalg_empty_namespace` 通过正式配置探测验证空 `std::linalg`
命名空间且无特性宏时会注入 backport。
`linalg_forced_feature_macro_compile` 在 GNU、Clang、AppleClang 及 Zig 的非 MSVC 模式下
通过 `#include_next` 加载只定义旧特性宏的测试头，再验证强制注入后的特性宏。MSVC 模式由
配置时解析的 `FORGE_MSVC_LINALG_HEADER` 选择原生头，因此该测试夹具只注册在 `#include_next` 路径上。
`linalg_include_compile` 与 `linalg_overload_scalars` 在 backport 注入或 complete native 时注册。
前者检查代表性的标准范数、Givens 和可用的 policy 调用，后者用全部 78 个非 policy twin
的独立小整数期望验证标准入口。Forge 头文件的可用性及迁移见
[linalg 迁移表](backports/linalg.md#已移除的非草案形式)。

其余依赖 backport 约束诊断、marker 或特定算术路径的测试及 configure probes，均要求
实际 backport 注入：`FORGE_FORCE_LINALG_BACKPORT`，或 `FORGE_LINALG_FULL` 与
`FORGE_LINALG_PARTIAL` 同时为假。原生 full/partial 让位时不注册这些测试，也不探测 policy
variant；configure 明示跳过。该组包括既有 auxiliary / level1/2/3、proxy、布局/性能回归、
静态约束与 packed negative probes，以及新增：

- `linalg_draft_surface_compile`：用正向草案调用对照，检查旧重载和较宽类型域已移除；
  configure 单独验证 SSQ 的两个标准命名空间入口均不存在；
- `linalg_missing_overloads`：23 个新增非 policy 入口，naive product / multiply-back
  solve oracle，upper/lower × explicit/unit、静态/动态、左右布局、stride、两种 packed
  storage order、lazy views 与每个 updating alias 许可；
- `linalg_conformance` / `linalg_conformance_compile`：可写 proxy、非交换 divide、
  conjugation 的元素转换、实数对角赋值、混合类型 Hermitian 乘法、scalar / assignability /
  packed 约束、member signatures 与 solve 重载选择；mixed RHS proxy 的三种 solve、
  assignment-only SYMV/HEMV/swap 和 complex proxy dotc 同时在可用的 policy 路径实例化；
- `linalg_native_execution_only_compile`：包含 `<linalg>` 不加载 Forge P2300；
- `linalg_policy_include_order_compile` / `linalg_policy_include_order`：两个翻译单元分别在
  `<linalg>` 前后包含 `<execution>`，检查可用性一致、78 个返回类型、policy value
  category 和 policy 类型约束。

上述注入 gate 内，configure 检查原生 policy 对象后才注册 `linalg_execution_policy`，覆盖每个标准
policy 的独立结果、调用线程行为和异常 terminate（accessor 与算术异常，GoogleTest
subprocess exit-code 检查）。`unseq` 要求 `__cpp_lib_execution >= 201902L`。
没有 policy 对象时 configure 明示原因，并注册可见的 skipped
`linalg_execution_policy_unavailable`；没有 death-test 支持的平台仅跳过 terminate cases。
TBB 可用时由 linalg helpers 链接。

libc++ 默认缺少 policy 对象时，CMake 另探测 per-target `-fexperimental-library`
能否编译并链接 policy；成功才增加 `linalg_execution_policy_experimental` 与
`linalg_policy_selection_experimental_compile`，不修改 lane scripts 或 libc++ 私有宏。
探测失败时 llvm / zig / macOS 仍检查无 policy 入口的路径，configure 说明 variant 缺失。

完整 `scripts/verify-native.sh llvm`、`zig`、`gcc16` 运行新 linalg 组；`gcc16-slim`
只配置，`gcc-exec`、`tsan`、`asan` 关闭 linalg。macOS 完整 lane 保留 linalg。
当前 `verify-windows-msvc.ps1` 的主 smoke 及专用子组显式关闭 linalg；需要单独配置
`FORGE_TEST_ENABLE_LINALG=ON` 才运行这些目标，CMake 会通过 `FORGE_MSVC_EXECUTION_HEADER`
加载原生 policies。可在已配置的 binary directory 中运行
`ctest -R '^linalg_' --output-on-failure` 聚焦该组；policy skip 必须结合 configure 记录解释。

`execution_adaptor_conformance_compile`
检查条件 connect noexcept、let/starts_on 的 exception_ptr 集合、无效 callable/结果/查询的
sender_in 与 connectability、FWD-ENV 的本地查询拒绝和 forwarding 查询保留。
配套 runtime 测试 `execution_adaptor_conformance`
覆盖 starts_on 在目标 agent 连接 child、late-connect error、associate predecessor 先释放再 join、
locked predecessor 的 completion 内替换、PMR values/errors 与 then/bulk callable 的 receiver allocator，
以及无 allocator 的移动/复制计数；嵌套 const-lvalue child 覆盖普通复制抛出与 allocator 重建成功，所有相关 adaptor 的 signature、connect 类别及 noexcept 一致。配置期 `FORGE_EXECUTION_ADAPTOR_CONTROL_*` 与
`FORGE_EXECUTION_ADAPTOR_REJECT_*` 逐项配对 creation constraint 的正反例，失败不依赖链接。
既有 static-opstate 测试同时固定 predecessor 在 successor 函数前销毁、参数复制失败时 predecessor
仍保留，以及 starts_on/on 的 child connect 失败改走 runtime error channel。

`execution_scheduling_conformance_compile`
覆盖 continues_on 按环境/实际 completion tag 的 query 参与条件、infallible destination 的 error
scheduler、不能保存的 payload、schedule_from/on 的 attribute 过滤、unstoppable 的精确结果类型，
以及 affine 的 infallible scheduler、member customization 与 scheduler query 转发。
mutable-only start scheduler 的正例由环境按值返回；`prop` 返回 const 引用，用作
[exec.get.start.scheduler]/2 与 [exec.affine]/7 的拒绝控制。删除 `operator&` 的环境仍须选择
scheduler-derived domain，并使 source 的 int 签名转换为 long，连接后交付转换所得的值。
同名 runtime 目标去掉 `_compile` 后缀，覆盖 affine 的四种 cv/ref member customization、
mutable-only scheduler 的实际调度及 wrapper 的类别转发，以及 `&` / `&&` 返回不同 sender、
不同 completion 集合及不同 connectability 的调度控制。Forge pool/strand 在无 stop token
环境的正常完成与拒绝 stopped 由 `forge_strand` 检查，对应签名由 `forge_execution_header` 检查。`FORGE_AFFINE_FORGE_CONTROL_*` / `FORGE_AFFINE_FORGE_REJECT_*`
逐一配对 pool/strand 的 stoppable 正例与无 token、never_stop_token 负例；另有 env<> 的
sender_in 拒绝控制。既有 pool/strand fixture 保持原有环境与 stopped 结果。
allocator 控制按 /28 检查有无 allocator 的 get-state prvalue/reference 构造；有效 receiver 的移动项恒为 true，throwing-move receiver 用负例检查概念与连接拒绝；
不执行会在 noexcept get-state 内 terminate 的重建。带有效 signatures 却无法连接的 child，
以及仅接受 signature receiver 的 successor，均检查 connect 的软拒绝。

`forge_predecessor_lifetime`
将 pool、strand、timer、channel recv、`forge::task` 和 `erased_sender` 直接接入 `let_value`，
在同步可用路径和其他线程完成路径中销毁 predecessor；另有 pool/timer 的同步 stopped 与 task
的同步/跨线程 error 控制。Pool 和 timer 的 value completion 始终由 worker 发出；strand 以
inline scheduler 提供同步 value 对照。Channel 在启动 pending recv 后由另一线程发送，task 在
final_suspend 完成，erased sender 覆盖同步与 pool 来源。两个 sanitizer lane 的
`execution|forge` 选择器均包含此测试，两个 lane 均启用 Forge runtime 测试。

### Awaitable 与 sender 双向桥接

`execution_awaitable_conformance_compile`
固定 marker short-circuit、enable_sender specialization、void / int / int& 的独立 signatures、
member / ADL co_await、as_awaitable member、domain transform 与 noexcept。
对 Forge 入口的编译期约束检查归入 `forge_member_queries`。
配套 runtime `execution_awaitable_conformance`
覆盖 value / void / error / stopped completion 内销毁 coroutine operation、跨线程恢复后自毁、
when_all 混合 awaitable / sender 和 scope wrap / associate / spawn_future。
`forge_task` 的 backport 专用用例覆盖 split 重放，以及 Forge scope、resource_context、
start_detached、any_sender_of、erased_sender、wait_result、task 与 IO bridge。
两条 sanitizer lanes 均选择这两个 runtime 目标。
Round 2 控制另覆盖 completion / start 两种 domain transform、内部 completion awaiter 的
receiver/value ADL 隔离、operation move 所有权、split 的 rvalue-only awaitable 类别，
以及 any_sender_of 的 lvalue-only marked source 精确结果。Error / stopped 的 task
控制检查 awaiting expression 后的代码没有执行。
`forge_task` 与 `forge_task_member_queries` 由两条 sanitizer lanes 选择，
新增 just_stopped / closed channel / nested stopped task 的 value/void task 控制，
每种均验证恰好一次 stopped、owner 保留 suspended frame，以及同步/另一线程 completion 内自毁。

配置期 `FORGE_EXECUTION_AWAITABLE_CONTROL_*` / `FORGE_EXECUTION_AWAITABLE_REJECT_*`
以同一 TU 的正反控制检查 member-result Mandate、无 stopped handler 的 promise、
class-type（含 cv/ref）、continuation handler 返回类型、固定 exception_ptr 导致的 spawn 拒绝，
以及缺少 error / stopped completion 的 receiver。失败必须发生在编译阶段，不能依赖未定义符号。
原有 wave3 的零 / 多参数值、multi-alternative identity fallback 与完成时 frame 销毁覆盖继续保留。
