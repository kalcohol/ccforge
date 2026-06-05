# 测试与验证

## 本地基线

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

如果 host 没有可用 C++ 编译器，使用 podman 验证镜像和 `scripts/verify-native.sh`，不要猜测工具链配置。

## Self-hosted verification floor（自托管验证基线）

Forge 不把验证流程绑定到 GitHub hosted CI。当前标准是本地或 self-hosted
verification floor：稳定机器直接调用仓库脚本，未来要迁移到 Jenkins、Buildkite、
GitHub self-hosted runner 或其它 CI 时，只需要挂接这些入口。

默认 owner-neutral 行为变更建议跑：

```bash
scripts/verify-selfhosted-floor.sh
```

该脚本按顺序调用现有验证入口，默认 native lanes 为 `llvm gcc-exec tsan asan`，
先运行 docs/examples audit，随后运行 install-package smoke。它不会并发运行 podman
`:Z` bind mount lane。
如需增加或替换 native lane，直接传参或设置环境变量：

```bash
scripts/verify-selfhosted-floor.sh llvm gcc16 gcc-exec tsan asan
FORGE_VERIFY_FLOOR_NATIVE_LANES='llvm zig gcc-exec tsan asan' scripts/verify-selfhosted-floor.sh
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

## Docs/example audit（文档与示例审计）

`scripts/audit-docs-and-examples.sh` 是轻量防回归脚本。它检查：

- docs / README 中引用的 `example/*.cpp` 是否真实存在；
- `example/CMakeLists.txt` 中注册的 example source 是否存在；
- 每个 `example/*.cpp` 是否被 `example/CMakeLists.txt` 注册；
- docs / README 中的本地 Markdown link 是否仍能解析到文件；
- committed docs/scripts 是否泄漏私有主机名或本地路径；
- 非 WD 的 `std::execution::ensure_started` / `std::execution::start_detached`
  是否只出现在“已移除”说明中。

该脚本不编译代码，也不证明 API correctness；它只负责文档和 example registry 的
机械一致性。`verify-selfhosted-floor.sh` 默认运行它；特殊情况下可设置：

```bash
FORGE_VERIFY_FLOOR_SKIP_DOC_AUDIT=1 scripts/verify-selfhosted-floor.sh
```

## 容器验证

完整验证入口：

```bash
scripts/verify-native.sh [gcc16|llvm|zig|local|gcc-exec|tsan|asan|all]
```

目标含义：

- `llvm`：LLVM/libc++ 容器，`-std=c++26`，覆盖 libc++ inject-path 全量测试
- `zig`：Zig 容器，C++23 backport inject path
- `gcc16`：GCC 16 容器，验证 `std::simd` / `std::constant_wrapper` / padded mdspan layouts / `std::submdspan` native stand-aside
- `gcc-exec`：GCC 16 容器，单独覆盖 libstdc++ 上的 `std::execution` backport，不跑 SIMD probes
- `tsan`：LLVM/libc++ 容器，`-fsanitize=thread`，覆盖 execution 与 `forge::` 扩展子集
- `asan`：LLVM/libc++ 容器，`-fsanitize=address,undefined`，覆盖 execution 与 `forge::` 扩展子集
- `all`：`gcc16 + llvm + zig + local + gcc-exec + tsan + asan`

手动运行容器时，使用：

```bash
podman run --rm --userns=keep-id -v "$PWD:/src:Z" -w /src ...
```

这样可以避免 root-owned build artifacts 和 orphaned containers。

## Windows/MSVC smoke（Windows 冒烟验证）

Windows 验证是可选的本机或 self-hosted smoke gate。它不替代 Linux/podman 全量矩阵；
当前目标是确认 MSVC 能 configure/build/test `std::execution` backport、
`std::unique_resource`、`forge::` runtime utility 子集和 Windows IOCP backend。

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

To run the repeatable smoke as a small self-hosted/manual matrix, use the
wrapper. It defaults to the current VS lane (`18`) and uses local-source mode
unless overridden:

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VS_VERSIONS='18' \
scripts/verify-windows-msvc-matrix.sh
```

Older MSVC lanes are exploratory. For example, `FORGE_WINDOWS_VS_VERSIONS='18
17'` can be used to measure VS 2026 plus VS 2022 compatibility, but the older
lane is not a reason to degrade modern C++ code generation or contort public
APIs.

默认 SSH wrapper 会在远端 clone `FORGE_WINDOWS_REPO` 的 `FORGE_WINDOWS_REF`。验证本地
尚未 push 的工作树时，使用 local-source 模式；wrapper 会把当前 worktree 打包到远端
临时目录，运行后清理：

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_USE_LOCAL_SOURCE=1 \
FORGE_WINDOWS_CTEST_REGEX=forge_io_iocp \
FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK=1 \
scripts/verify-windows-msvc-ssh.sh
```

`FORGE_WINDOWS_CTEST_REGEX` 用于 focused smoke，例如只跑 `forge_io_iocp`。省略时使用
PowerShell 脚本默认的 `execution|unique_resource|forge`。`FORGE_WINDOWS_KEEP=1`
可保留远端临时源码/clone 以便调试；不要把具体主机名、用户目录或工具链安装路径写进
仓库文档。

如果 Visual Studio 使用标准安装位置，也可以省略 `FORGE_WINDOWS_VC_VARS`，
让脚本按 `VsVersion` 或 `vswhere` 查找。脚本会打印 MSVC compiler version、
关键 Forge gate 状态和最终 CTest 数量。它还会做 gate 注册检查：
Windows 上 `FORGE_ENABLE_FORGE_IO=AUTO` / `ON` 应启用 IOCP backend 并注册
IO tests/examples，`FORGE_ENABLE_FORGE_IO=OFF` 应注册 0 个 IO tests/examples；
`FORGE_ENABLE_FORGE_ACCEL=AUTO` / `ON` 应注册 accel tests/examples，
`FORGE_ENABLE_FORGE_ACCEL=OFF` 应注册 0 个 accel tests/examples。gate 检查还会
实际 build/run 少量稳定 examples：IOCP example、basic accel copy/event examples
和 reference runtime example。这保证 Windows smoke 覆盖 cookbook 的关键路径，而不把
所有 examples 都塞进默认主测试。脚本避免使用单个硬编码全局 CTest 数量作为验收标准，
因为测试总数会随覆盖增长而变化。可设置 `FORGE_WINDOWS_SKIP_GATE_CHECKS=1` 临时跳过
这些 gate 检查。默认 smoke 也会执行 install package consumer check；可设置
`FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK=1` 临时跳过。

主 smoke 默认启用可用的 Forge IO backend，并关闭 SIMD/submdspan/linalg/native-handoff
测试：

```cmake
FORGE_ENABLE_FORGE_IO=AUTO
FORGE_TEST_ENABLE_SIMD=OFF
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

- `scripts/verify-native.sh gcc16` 检查 GCC 已暴露标准库特性时的 partial-native
  stand-aside path。
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

`scripts/audit-runtime-wakeups.sh` lists condition-variable, notify,
stop-callback, and cancellation sites under `include/forge` and the execution
backport. It is a manual review aid, not a proof. Use it after touching
cancellation or shutdown paths. The key rule is:

> 如果 waiter 在 mutex 下观察 predicate，那么修改该 predicate 也应在同一把 mutex 下
> publish，然后再 `notify_one` / `notify_all`。

Atomic predicate 加 unlocked notify 仍可能 lost wakeup。

当前 high-risk wakeup coverage map：

| 区域 | 代表性 wakeup / cancellation site | 覆盖 |
| --- | --- | --- |
| execution scope join | `simple_counting_scope` / `counting_scope` join registration 和最后一次 association release | `execution_counting_scope` deterministic tests 与 `execution_counting_scope_stress` 覆盖 `join start` vs last release、多 joiner、以及 join completion 内再次启动 join |
| `forge::timer_context` | deadline wakeup、per-item stop callback、`wait()` pending drain | `forge_timer_context` 覆盖 long-deadline cancellation、stop-vs-deadline、self-destroying receiver 和 repeated prompt wake；`forge_runtime_stress` 竞争 short deadline completion 与 receiver stop |
| `forge::bounded_channel` | pending send/recv stop callback 与 close/stop drain | `forge_channel` 覆盖 rendezvous、backpressure、close-drain、cancellation 和 self-destroying receiver；`forge_runtime_stress` 竞争 pending recv/send stop callback 与 direct send/recv handoff |
| `forge::async_scope` | active-count drain 与 request-stop propagation | `forge_async_scope` 覆盖 spawn lifetime、blocking destructor/wait、stop propagation 和 first-error handling；`forge_runtime_stress` 竞争 `wait()` 与 last scheduled completion |
| `forge::strand` / pool | serialized queue drain、shutdown 和 worker handoff | `forge_strand`、`forge_thread_pool` 和 scheduler roundtrip tests 覆盖 FIFO、reentrant scheduling、bounded queues、shutdown 和 worker self-wait；`forge_runtime_stress` 覆盖 concurrent strand scheduling 与 pool schedule-vs-shutdown completion |
| `forge::io` / `forge::accel` | backend stop callback、pending record completion 和 context drain | `forge_io_*` 与 `forge_accel_*` 覆盖 exactly-once completion、typed error、stop/drain、event wait，以及支持处的 self-destroying receiver |

这张表是 representative map，不是“每一条 audit line 都有独立 stress”的证明。修改
wakeup site 后，应指向一个覆盖同类 predicate/notify discipline 的现有测试，或补一个
focused stress case，再把该 audit 视为 closed。

## 测试分组开关

测试子目录可用 CMake 开关独立启停，默认全开：

- `FORGE_TEST_ENABLE_EXECUTION`
- `FORGE_TEST_ENABLE_SIMD`
- `FORGE_TEST_ENABLE_UNIQUE_RESOURCE`
- `FORGE_TEST_ENABLE_SUBMDSPAN`
- `FORGE_TEST_ENABLE_LINALG`
- `FORGE_TEST_ENABLE_FORGE`
- `FORGE_TEST_ENABLE_FORGE_RUNTIME`
- `FORGE_TEST_ENABLE_FORGE_RESOURCE`
- `FORGE_TEST_ENABLE_FORGE_IO`
- `FORGE_TEST_ENABLE_FORGE_ACCEL`
- `FORGE_TEST_ENABLE_FORGE_ERASURE`
- `FORGE_TEST_ENABLE_NATIVE_HANDOFF`

`FORGE_TEST_ENABLE_FORGE` 是 `include/forge/` extension tests 的 parent switch。
更窄的 `FORGE_TEST_ENABLE_FORGE_*` 开关默认仍启用当前测试，同时允许 future
resource、IO、accel 和 erasure subsets 被独立配置。Resource-policy tests 还要求
`FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON`。

也可以独立设置 Forge extension feature gates：

- `FORGE_ENABLE_FORGE_RUNTIME`
- `FORGE_ENABLE_FORGE_RESOURCE_POLICY`
- `FORGE_ENABLE_FORGE_IO`
- `FORGE_ENABLE_FORGE_ACCEL`
- `FORGE_ENABLE_FORGE_ACCEL_CPU`

`FORGE_ENABLE_FORGE_IO=AUTO` 在平台支持时启用 Linux epoll/eventfd backend 或 Windows
IOCP backend，其它平台跳过 IO tests/examples。`ON` 要求 supported backend，缺失时
configure 报错；`OFF` 跳过 IO tests/examples。`FORGE_ENABLE_FORGE_ACCEL=AUTO` 在
Forge runtime/resource gates 启用时启用 portable mock accel backend；`ON` 要求这些
gate 可用，`OFF` 跳过 accel tests/examples。它不会探测 CUDA、HIP、SYCL 或 vendor
SDK。`FORGE_ENABLE_FORGE_ACCEL_CPU=AUTO` / `ON` 在 accel umbrella gate 启用时注册
CPU reference backend tests/examples；若 `FORGE_ENABLE_FORGE_ACCEL=OFF`，CPU backend
即使显式 `ON` 也不会注册任何 accel target。Erasure facilities 是 header-only 且总是可用；用
`FORGE_TEST_ENABLE_FORGE_ERASURE` 控制是否运行对应测试。

## Example smoke tests（示例冒烟）

当 `FORGE_BUILD_EXAMPLES=ON` 且 `FORGE_BUILD_TESTS=ON` 时，实际构建出来的 examples
也会注册为 CTest smoke tests，名称为 `example_<target>_smoke`。这样 cookbook 中的路径
不是只 compile-check，而是可执行的 smoke。受 feature gate 控制的 examples，例如
platform IO、accel 或 mdspan-based linalg examples，只在对应 target 存在时注册 smoke。

Example smoke tests 按决定 target 是否构建的 feature gates 分组。它们不逐一镜像所有
`FORGE_TEST_ENABLE_FORGE_*` 细分测试开关。这样 examples 聚焦 public build surface，
test tree 则继续保留 runtime、resource、IO、accel 和 erasure coverage 的细粒度启停。

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

cmake -S . -B build/gate-accel-off -G Ninja \
  -DFORGE_BUILD_TESTS=ON \
  -DFORGE_BUILD_EXAMPLES=ON \
  -DFORGE_ENABLE_FORGE_ACCEL=OFF
ctest --test-dir build/gate-accel-off -N -R 'forge_accel|example_forge_accel|inference_runtime'
```

未来 platform/vendor backend proofs 也必须记录 focused tests、sanitizer expectation、
install-package behavior，以及任何 manual/self-hosted platform smoke。不要把私有
hostname 或本地安装路径写进已提交的 docs/scripts。

focused execution 示例：

```bash
cmake --build build/llvm --target test_execution_wave1
ctest --test-dir build/llvm -R 'execution_wave1' --output-on-failure
```

## Install package smoke（安装包冒烟）

Install package smoke 验证一个已安装 prefix 能被独立项目通过
`find_package(CCForge CONFIG REQUIRED)` 消费。它会把 headers、backport wrappers 和
CMake package config 安装到临时 build prefix，然后 configure 并运行
`test/install_consumer`：

```bash
scripts/verify-install-package.sh
```

这个检查故意不放进 default CTest，因为它会执行第二轮 configure / install / build cycle。
修改 CMake packaging 或 install layout、以及 release-oriented change 前应运行它。

## 注意事项

- `test/CMakeLists.txt` expects `3rdparty/googletest`; fresh checkout 缺失时需要初始化 submodule 或提供该目录
- 不要意外 stage vendored/untracked `3rdparty/` 内容
- SIMD configure probes 在 CMake configure 阶段运行；configure failure 可能是 probe failure，还没进入 build target
- `linalg` 和 `submdspan` tests/examples 依赖 `<mdspan>`，在较旧标准库上会跳过
- 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`；Forge tests/examples 会在检测到 `tbb` 时自动链接
