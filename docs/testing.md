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

## Windows/MSVC Smoke

Windows 验证是可选的手动 smoke gate。它不替代 Linux/podman 全量矩阵；当前
目标是确认 MSVC 能 configure/build/test `std::execution` backport、
`std::unique_resource` 和非 Linux IO 的 `forge::` runtime utility 子集。

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

如果 Visual Studio 使用标准安装位置，也可以省略 `FORGE_WINDOWS_VC_VARS`，
让脚本按 `VsVersion` 或 `vswhere` 查找。脚本会打印 MSVC compiler version、
关键 Forge gate 状态和最终 CTest 数量。它还会做 configure-only gate 检查：
Windows 上 `FORGE_ENABLE_FORGE_IO=AUTO` 应跳过 IO backend，显式
`FORGE_ENABLE_FORGE_IO=ON` 应给出清楚 configure error。可设置
`FORGE_WINDOWS_SKIP_GATE_CHECKS=1` 临时跳过这些 gate 检查。

主 smoke 默认关闭 Linux-only IO backend 和 SIMD/submdspan/linalg/native-handoff 测试：

```cmake
FORGE_ENABLE_FORGE_IO=OFF
FORGE_TEST_ENABLE_SIMD=OFF
FORGE_TEST_ENABLE_SUBMDSPAN=OFF
FORGE_TEST_ENABLE_LINALG=OFF
FORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
```

已验证的 Windows baseline 是 VS 2026 Build Tools / MSVC 19.51。VS 2022 /
MSVC 19.44 可以 configure，但在 P2300/domain/write_env 的 constrained CPO
模板路径上仍有编译器兼容缺口；它目前不是强制 gate。

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
- `FORGE_ENABLE_FORGE_TYPED_ERASURE`

`FORGE_ENABLE_FORGE_IO=AUTO` enables the Linux epoll/eventfd backend when the
platform supports it and skips IO tests/examples elsewhere. `ON` requires that
backend and reports a configure error if unavailable; `OFF` skips IO
tests/examples. `FORGE_ENABLE_FORGE_ACCEL=AUTO` enables the portable mock accel
backend when Forge runtime/resource gates are enabled; `ON` requires those gates
and `OFF` skips accel tests/examples. It does not probe CUDA, HIP, SYCL, or
vendor SDKs.

## Example smoke tests

When both `FORGE_BUILD_EXAMPLES=ON` and `FORGE_BUILD_TESTS=ON`, examples that are
actually built are also registered as CTest smoke tests named
`example_<target>_smoke`. This keeps the cookbook paths executable instead of
only compile-checked. Feature-gated examples, such as Linux IO or mdspan-based
linalg examples, only register their smoke tests when their target exists.

Focused example check:

```bash
ctest --test-dir build/local -R '^example_' --output-on-failure
```

focused execution 示例：

```bash
cmake --build build/llvm --target test_execution_wave1
ctest --test-dir build/llvm -R 'execution_wave1' --output-on-failure
```

## Gotchas

- `test/CMakeLists.txt` expects `3rdparty/googletest`; fresh checkout 缺失时需要初始化 submodule 或提供该目录
- 不要意外 stage vendored/untracked `3rdparty/` 内容
- SIMD configure probes 在 CMake configure 阶段运行；configure failure 可能是 probe failure，还没进入 build target
- `linalg` 和 `submdspan` tests/examples 依赖 `<mdspan>`，在较旧标准库上会跳过
- 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`；Forge tests/examples 会在检测到 `tbb` 时自动链接
