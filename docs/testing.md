# 测试与验证

## 本地基线

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

如果 host 没有可用 C++ 编译器，使用 podman 验证镜像和 `scripts/verify-native.sh`，不要猜测工具链配置。

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

## Windows/MSVC smoke

Windows 验证是可选的手动 smoke gate。它不替代 Linux/podman 全量矩阵；当前
目标是确认 MSVC 能 configure/build/test `std::execution` backport、
`std::unique_resource`、`forge::` runtime utility 子集和 Windows IOCP backend。

在 Windows 主机上直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-windows-msvc.ps1 `
  -Vcvars "C:\path\to\VC\Auxiliary\Build\vcvars64.bat"
```

从 Linux/macOS 通过 SSH 调用远端 Windows 主机：

```bash
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VC_VARS='C:\path\to\VC\Auxiliary\Build\vcvars64.bat' \
scripts/verify-windows-msvc-ssh.sh
```

To run the repeatable smoke as a small matrix, use the wrapper. It defaults to
the current VS lane (`18`) and uses local-source mode unless overridden:

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

## optional stdexec feasibility probe

`scripts/probe-stdexec-feasibility.sh` 是本地 spike 工具，不属于默认门禁。
它要求调用者提供 `STDEXEC_ROOT=/path/to/stdexec`，只验证本地 stdexec checkout
和 Forge `<execution>` backport 各自能编译最小 smoke program。它不 fetch、不
vendor，也不声明 stdexec 已经是 Forge 的 native `std::execution` handoff lane。
未设置 `STDEXEC_ROOT` 时脚本返回 77 并打印 `result=skipped`，方便把它接入本地
可选验证而不把 skip 误判为失败。

## runtime wakeup audit helper

`scripts/audit-runtime-wakeups.sh` lists condition-variable, notify,
stop-callback, and cancellation sites under `include/forge` and the execution
backport. It is a manual review aid, not a proof. Use it after touching
cancellation or shutdown paths. The key rule is:

> If a waiter observes a predicate under a mutex, publish changes to that
> predicate under the same mutex before `notify_one` / `notify_all`.

An atomic predicate plus an unlocked notify can still lose a wakeup.

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

`FORGE_TEST_ENABLE_FORGE` is the parent switch for `include/forge/` extension
tests. The narrower `FORGE_TEST_ENABLE_FORGE_*` switches keep the current tests
enabled by default while allowing future resource, IO, accel, and erasure
subsets to be configured independently. Resource-policy tests also require
`FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON`.

Forge extension feature gates are also available:

- `FORGE_ENABLE_FORGE_RUNTIME`
- `FORGE_ENABLE_FORGE_RESOURCE_POLICY`
- `FORGE_ENABLE_FORGE_IO`
- `FORGE_ENABLE_FORGE_ACCEL`

`FORGE_ENABLE_FORGE_IO=AUTO` enables the Linux epoll/eventfd backend or Windows
IOCP backend when the platform supports one, and skips IO tests/examples
elsewhere. `ON` requires a supported backend and reports a configure error if
unavailable; `OFF` skips IO tests/examples. `FORGE_ENABLE_FORGE_ACCEL=AUTO`
enables the portable mock accel
backend when Forge runtime/resource gates are enabled; `ON` requires those gates
and `OFF` skips accel tests/examples. It does not probe CUDA, HIP, SYCL, or
vendor SDKs. Erasure facilities are header-only and always available; use
`FORGE_TEST_ENABLE_FORGE_ERASURE` to include or skip their tests.

## example smoke tests

When both `FORGE_BUILD_EXAMPLES=ON` and `FORGE_BUILD_TESTS=ON`, examples that are
actually built are also registered as CTest smoke tests named
`example_<target>_smoke`. This keeps the cookbook paths executable instead of
only compile-checked. Feature-gated examples, such as platform IO, accel, or
mdspan-based linalg examples, only register their smoke tests when their target
exists.

Focused example check:

```bash
ctest --test-dir build/local -R '^example_' --output-on-failure
```

## backend proof gates

Optional backend proofs must be tested by registration shape as well as by
runtime tests. For a backend feature gate:

- `AUTO` should register backend tests/examples only when the probe succeeds;
- `ON` should require the backend and fail configure if unavailable;
- `OFF` should register zero backend tests/examples.

Use regex-specific checks instead of global test counts. For example:

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

Future platform/vendor backend proofs must also document their focused tests,
sanitizer expectation, install-package behavior, and any manual/self-hosted
platform smoke. Keep private hostnames and local installation paths out of
committed docs and scripts.

focused execution 示例：

```bash
cmake --build build/llvm --target test_execution_wave1
ctest --test-dir build/llvm -R 'execution_wave1' --output-on-failure
```

## install package smoke

The install package smoke verifies that an installed prefix can be consumed by a
separate project with `find_package(CCForge CONFIG REQUIRED)`. It installs
headers, backport wrappers, and the CMake package config to a temporary build
prefix, then configures and runs `test/install_consumer`:

```bash
scripts/verify-install-package.sh
```

This check is intentionally not part of default CTest because it performs a
second configure/install/build cycle. It should be run before release-oriented
changes to CMake packaging or install layout.

## gotchas

- `test/CMakeLists.txt` expects `3rdparty/googletest`; fresh checkout 缺失时需要初始化 submodule 或提供该目录
- 不要意外 stage vendored/untracked `3rdparty/` 内容
- SIMD configure probes 在 CMake configure 阶段运行；configure failure 可能是 probe failure，还没进入 build target
- `linalg` 和 `submdspan` tests/examples 依赖 `<mdspan>`，在较旧标准库上会跳过
- 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`；Forge tests/examples 会在检测到 `tbb` 时自动链接
