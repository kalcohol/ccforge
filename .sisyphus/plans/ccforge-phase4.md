# CC Forge Phase 4 — simd 完整覆盖 + domain dispatch + forge:: 扩展

## TL;DR

> simd 补齐 range-based load/store/gather/scatter + flags + alignment → 定义 `__cpp_lib_simd`；domain dispatch 集成到 connect_t；forge:: 新增 static_thread_pool/system_context/task<T>/any_receiver_of；README 全面理顺。
>
> **Estimated Effort**: L（~18 个任务）
> **Critical Path**: simd audit → simd gaps → __cpp_lib_simd → domain dispatch → forge:: extensions → README

---

## Context

### Metis Critical Finding
旧命名 hmin/hmax/simd_split/simd_cat/where 在 current draft 中已不存在或已重命名。CC Forge 已实现 reduce_min/reduce_max/split/cat/select。真正缺失的是 range-based memory 操作和 flags API。

### Decisions
- simd: 按 [simd.syn] 实际缺口补齐，非旧 P1928 命名
- domain dispatch: connect_t 级别集成 transform_sender
- forge:: 本 Phase 实现 4 个（thread_pool/system_context/task/any_receiver），timed_scheduler/sequence_sender 延后
- README: 最后全面理顺

---

## Work Objectives

### Must Have
- simd [simd.syn] 缺口补齐（partial_load/store, gather/scatter range overloads, flags, alignment_v, zero_element/uninit_element）
- 定义 `__cpp_lib_simd` = 202411L
- connect_t 集成 domain::transform_sender
- forge::static_thread_pool（scheduler 接口 + 工作窃取队列）
- forge::system_context（全局线程池单例）
- forge::task<T>（协程返回类型 + sender 桥）
- forge::any_receiver_of<Sigs>（双向类型擦除）
- README 全面理顺

### Must NOT Have
- ❌ timed_scheduler（Phase 5）
- ❌ sequence_sender（Phase 5）
- ❌ 平台特定代码（仅 <thread>/<mutex>/<condition_variable>/<atomic>）
- ❌ 修改已有 sender 文件（domain 在 connect_t 层集成）

---

## Execution Strategy

```
Wave 0 (simd completion):
├── T1: [simd.syn] audit + compile probes for gaps [quick]
├── T2: simd range-based partial_load/unchecked_load/partial_store/unchecked_store [unspecified-high]
├── T3: simd range-based gather/scatter overloads + zero_element/uninit_element [unspecified-high]
├── T4: simd flags API + alignment_v [quick]
└── T5: define __cpp_lib_simd 202411L + flip feature macro probe [quick]

Wave 1 (domain dispatch + forge:: core, parallel):
Track A:
├── T6: domain dispatch — integrate transform_sender into connect_t [deep]
Track B:
├── T7: forge::static_thread_pool [deep]
├── T8: forge::system_context [unspecified-high]
├── T9: forge::task<T> [deep]
└── T10: forge::any_receiver_of [unspecified-high]

Wave 2 (tests + verification):
├── T11: 4-toolchain verification (GCC + Zig + cross-arch) [unspecified-high]
├── T12: README comprehensive cleanup [writing]

Wave FINAL:
├── F1-F4: Plan compliance + code quality + QA + scope fidelity
```

---

## TODOs

- [x] 1. simd [simd.syn] audit + compile probes
- [x] 2. simd range-based load/store overloads
- [x] 3. simd range-based gather/scatter + sentinel constants
- [x] 4. simd flags API + alignment_v
- [x] 5. define __cpp_lib_simd 202411L
- [x] 6. domain dispatch in connect_t
- [x] 7. forge::static_thread_pool (code exists, needs inline fix)
- [x] 8. forge::system_context (code exists)
- [x] 9. forge::task<T> (code exists, needs segfault fix)
- [x] 10. forge::any_receiver_of (code exists, tests pass)
- [ ] 11. Fix forge::static_thread_pool link error (add inline to tag_invoke)
- [ ] 12. Fix forge::task segfault (investigate and fix)
- [ ] 13. Verify all forge tests pass
- [ ] 14. 4-toolchain + cross-arch verification
- [ ] 15. README comprehensive cleanup

---

## Task Details (for remaining tasks)

### Task 11: Fix forge::static_thread_pool link error

**Problem**: `tag_invoke` functions in `__pool_detail` namespace cause undefined reference errors during linking.

**Root Cause**: Template friend functions need `inline` keyword when defined outside class in header-only library.

**What to do**:
1. Open `include/forge/static_thread_pool.hpp`
2. Find lines 182-192 (the `tag_invoke` function definitions in `__pool_detail` namespace)
3. Add `inline` keyword to both functions:
   - Line 183: Change `void tag_invoke` to `inline void tag_invoke`
   - Line 190: Change `auto tag_invoke` to `inline auto tag_invoke`

**Acceptance Criteria**:
- [ ] `cmake --build build-verify --target test_forge_thread_pool` succeeds
- [ ] `./build-verify/test/forge/test_forge_thread_pool` runs without errors

**Commit**: `fix(forge): add inline to static_thread_pool tag_invoke functions`

---

### Task 12: Fix forge::task segfault

**Problem**: `test_forge_task` crashes with segmentation fault.

**What to do**:
1. Read `test/forge/runtime/test_forge_task.cpp` to understand test expectations
2. Read `include/forge/task.hpp` to identify the issue
3. Common causes:
   - Coroutine promise lifetime issues
   - Missing `final_suspend` handling
   - Incorrect receiver state management
4. Debug with gdb if needed: `gdb ./build-verify/test/forge/test_forge_task`
5. Fix the identified issue

**Acceptance Criteria**:
- [ ] `./build-verify/test/forge/test_forge_task` runs without segfault
- [ ] All task tests pass

**Commit**: `fix(forge): resolve task<T> segfault in coroutine handling`

---

### Task 13: Verify all forge tests pass

**What to do**:
1. Rebuild: `cmake --build build-verify -j4`
2. Run forge tests: `ctest --test-dir build-verify -R forge_ --output-on-failure`
3. Verify all 3 tests pass:
   - `forge_thread_pool`
   - `forge_task`
   - `forge_any_receiver`

**Acceptance Criteria**:
- [ ] All forge tests pass (3/3)
- [ ] No segfaults, no link errors

---

### Task 14: 4-toolchain + cross-arch verification

**What to do**:
1. **GCC verification**:
   ```bash
   cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
   cmake --build build-gcc -j
   ctest --test-dir build-gcc --output-on-failure
   ```

2. **Zig verification**:
   ```bash
   CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
   cmake --build build-zig -j
   ctest --test-dir build-zig --output-on-failure
   ```

3. **Clang container**:
   ```bash
   podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc \
     'cmake -S . -B build-clang -DFORGE_BUILD_TESTS=ON && cmake --build build-clang -j && ctest --test-dir build-clang --output-on-failure'
   ```

4. **Cross-arch (if zig available)**:
   ```bash
   # aarch64
   CC="zig cc -target aarch64-linux" CXX="zig c++ -target aarch64-linux" cmake -S . -B build-aarch64 -DFORGE_BUILD_TESTS=ON
   cmake --build build-aarch64 -j
   ```

**Acceptance Criteria**:
- [ ] GCC: all tests pass
- [ ] Zig: all tests pass
- [ ] Clang: all tests pass
- [ ] Cross-compile succeeds (aarch64)

**Commit**: `test: verify Phase 4 on GCC/Zig/Clang + cross-arch` → PUSH

---

### Task 15: README comprehensive cleanup

**What to do**:
1. Update README.md to reflect Phase 4 completion:
   - Update `std::simd` section: mention `__cpp_lib_simd` is now defined
   - Update `std::execution` section: mention domain dispatch integration
   - Add `forge::` extensions section:
     - `forge::static_thread_pool` - thread pool with scheduler interface
     - `forge::system_context` - global thread pool singleton
     - `forge::task<T>` - coroutine return type for async operations
     - `forge::any_receiver_of<Sigs>` - type-erased receiver
     - `forge::any_sender_of<Sigs>` - type-erased sender (already exists)
2. Update feature table if present
3. Ensure all code examples are accurate

**Acceptance Criteria**:
- [ ] README accurately reflects all Phase 4 features
- [ ] All examples compile
- [ ] Documentation is clear and concise

**Commit**: `docs: update README for Phase 4 completion (simd __cpp_lib, domain dispatch, forge extensions)` → PUSH

---

## Final Verification Wave

- [ ] F1. Plan compliance audit
- [ ] F2. Code quality + 4-toolchain QA
- [ ] F3. 4-architecture QA
- [ ] F4. Scope fidelity check

---

## Commit Strategy

1. `simd: audit [simd.syn] gaps + add compile probes` → PUSH
2. `simd: add range-based load/store/gather/scatter overloads`
3. `simd: add flags API + alignment_v`
4. `simd: define __cpp_lib_simd 202411L` → PUSH (simd milestone)
5. `exec: integrate domain transform_sender into connect_t` → PUSH
6. `forge: add static_thread_pool with scheduler interface`
7. `forge: add system_context (global thread pool)`
8. `forge: add task<T> coroutine return type`
9. `forge: add any_receiver_of type-erased receiver` → PUSH (forge milestone)
10. `docs: comprehensive README cleanup` → PUSH (final)

---

## Success Criteria

```bash
# GCC
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON && cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure
# Zig
CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON && cmake --build build-zig -j && ctest --test-dir build-zig --output-on-failure
# __cpp_lib_simd probe
echo '#include <simd>
static_assert(__cpp_lib_simd >= 202411L);
int main() {}' | g++ -std=c++23 -I backport -x c++ - -o /dev/null
```
