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

`FORGE_ENABLE_FORGE_IO` and `FORGE_ENABLE_FORGE_ACCEL` are `AUTO` placeholders
until real backends exist. They do not probe epoll, io_uring, IOCP, CUDA, HIP,
or SYCL yet.

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
