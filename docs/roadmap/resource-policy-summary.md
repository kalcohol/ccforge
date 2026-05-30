# Resource Policy 轮次总任务书

本轮目标：为 `forge::` runtime 设施建立默认开启、可配置、可测试的资源策略基础，
并同步建立后续 IO / accel / typed-erasure 需要的 feature gate 框架。

这不是 backport 工作，不修改 `namespace std`，不改变标准 header 注入逻辑。

## Scope

包含：

- 新增 Forge feature gates，默认保持现有用户体验；
- 引入 resource policy / memory options 的最小公共模型；
- 让若干 runtime primitive 接受 `std::pmr::memory_resource*` 或等价 options；
- 增加能证明 allocator 路径生效的测试；
- 增加教学向 examples；
- 更新 docs，把限制写清楚。

不包含：

- IO backend；
- accel backend；
- typed-error erased sender；
- 完整无分配 guarantee；
- 自定义 task callable storage，除非实现中发现它是非常小的必要前置。

## Mandatory Decisions

1. Feature gate 命名必须稳定，并与现有 `FORGE_TEST_ENABLE_FORGE` 协调。
2. V1 resource policy 使用 `std::pmr::memory_resource*`，默认
   `std::pmr::get_default_resource()`。
3. 旧构造函数必须源码兼容，默认行为不变。
4. 文档必须明确哪些 allocation 已受 policy 控制，哪些仍可能来自标准库内部对象
   例如 `std::function` / `shared_ptr` control block。
5. Examples 是验收项，不是收尾可选项。
6. `async_scope` spawned op-state 和 `strand` runner node 的 intrusive keepalive 分配默认
   defer，不为 V1 pmr 覆盖破坏已验证的生命周期模型。
7. IO/accel/typed-erasure gate 在本轮只做占位，不做 backend 依赖探测。

## Proposed Public Shape

候选 options：

```cpp
struct resource_policy {
    std::pmr::memory_resource* memory = std::pmr::get_default_resource();
};

struct static_thread_pool_options {
    std::size_t thread_count = std::thread::hardware_concurrency();
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = std::pmr::get_default_resource();
};

struct bounded_channel_options {
    std::size_t capacity = 0;
    std::pmr::memory_resource* memory = std::pmr::get_default_resource();
};
```

实际命名可在 Taskbook B 落地前核对现有 API 后微调，但要求保持简单、聚合初始化友好。

## Implementation Order

1. Taskbook A: feature gates and test-gate scaffolding
2. Taskbook B: resource policy core
3. Taskbook C: adopt policy in runtime primitives
4. Taskbook D: examples, docs, verification

每个 taskbook 完成后提交一个小 commit。Taskbook C 可按 primitive 拆成多个 commit，
不要把 pool/channel/strand/scope 全混在一个大 diff 中。

## Verification Baseline

Focused:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target test_forge_thread_pool test_forge_channel test_forge_strand test_forge_async_scope
ctest --test-dir build/local -R '^forge_' --output-on-failure
```

Full before final approval:

```bash
scripts/verify-native.sh llvm zig local tsan asan
```

If CMake gates are touched, also verify configure behavior with key combinations:

```bash
cmake -S . -B build/gate-off -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_RESOURCE_POLICY=OFF
cmake -S . -B build/gate-on  -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_RESOURCE_POLICY=ON
```

## Acceptance Criteria

- Existing default constructors and examples continue to compile.
- Existing `^forge_` tests pass unchanged except where tests are intentionally extended.
- `scripts/verify-native.sh tsan asan` 的测试计数不得下降；当前基线是 31/31。
- New policy tests prove custom memory resources see allocations/deallocations on adopted paths.
- Sanitizers cover all touched lifecycle-sensitive primitives.
- Docs and examples describe both the useful path and the current caveats.
