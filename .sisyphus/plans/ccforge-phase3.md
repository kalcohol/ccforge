# CC Forge Phase 3 — 重命名 + simd Layer 1 + execution 补完 + linalg 全面 SIMD/OpenMP

## TL;DR

> **Quick Summary**: 项目重命名为 CC Forge；simd backport 用 GCC/Clang vector extension 替换标量循环（Layer 1）；execution 补完 domain dispatch + counting_scope；linalg 全面 SIMD 加速 + OpenMP 多核；linalg.hpp 拆分为 5 子文件；`any_sender` 移入 `forge::` 命名空间。
>
> **Deliverables**:
> - 项目重命名（README/CLAUDE.md/CMake/license headers）
> - simd backport vector extension 后端（存储+运算全替换，constexpr 双路径）
> - `forge::any_sender_of<Sigs...>`（forge 扩展，非 backport）
> - `default_domain` + `get_domain` CPO
> - `simple_counting_scope` + `counting_scope`（P3149R11）
> - linalg Level 1 全面 SIMD（copy/scale/add/swap/GEMV）
> - linalg OpenMP（GEMM/GEMV 外循环）
> - linalg.hpp 拆分为 5 子文件
> - 临时性能对比报告（不提交）
> - README/CLAUDE.md 更新
>
> **Estimated Effort**: XL（~30 个任务）
> **Parallel Execution**: YES - 5 waves
> **Critical Path**: 重命名 → simd Layer 1 存储 → simd Layer 1 运算 → linalg SIMD → linalg OpenMP

---

## Context

### Decisions Made
- M1: `any_sender` 放在 `include/forge/any_sender.hpp`，`forge::` 命名空间（扩展，非 backport）
- M2: simd Layer 1 存储+运算全替换（选项 b），`if !std::is_constant_evaluated()` 双路径
- M3: simd Layer 1 先做，linalg SIMD 后做（顺序依赖）
- P3149 目标 R11 命名（`associate`/`scope_token`/`simple_counting_scope`/`counting_scope`）
- domain-based 调度仅实现 `default_domain` + `get_domain` CPO，不改造 Phase 1-3 已有 sender
- OpenMP 仅 GEMM/GEMV 外循环，`#ifdef _OPENMP` 条件编译
- linalg.hpp 拆为 5 子文件，submdspan.hpp 不拆，concepts.hpp __forge_meta 不拆
- 变量名 `FORGE_*` 和 target `forge::forge` 保持不变

### Metis Risks Addressed
- R2 constexpr 不兼容：双路径 `is_constant_evaluated()`
- R3 counting_scope 并发：TDD + 引用计数排序测试
- R5 OpenMP+Zig：`#ifdef _OPENMP` 包裹 `<omp.h>` 和所有 `omp_*`
- R1 simd 回归：全量 simd 测试套件验证

---

## Work Objectives

### Must Have
- 全部源码 license header `Forge Project` → `CC Forge Project`
- README/CLAUDE.md 标题+文本 → CC Forge
- CMakeLists.txt `project(Forge ...)` → `project(CCForge ...)`
- forge.cmake 消息 `Forge:` → `CC Forge:`
- simd `types.hpp` vector extension 存储（`__attribute__((vector_size))` typedef + `is_constant_evaluated` 双路径）
- simd `types.hpp`/`operations.hpp`/`reductions.hpp` 运算替换（`+`/`-`/`*`/`abs`/`reduce` 向量化）
- `include/forge/any_sender.hpp`（SBO 64B + 堆回退，`forge::any_sender_of<Sigs...>`）
- `backport/cpp26/execution/domain.hpp`（`default_domain`+`get_domain` CPO）
- `backport/cpp26/execution/counting_scope.hpp`（P3149R11: `simple_counting_scope`/`counting_scope`/`associate`/`spawn`/`spawn_future`）
- linalg SIMD 扩展：copy/scale/add/swap(Level 1) + GEMV 内循环(Level 2)
- linalg OpenMP：GEMM/GEMV 外循环（`#ifdef _OPENMP`）
- linalg.hpp 拆分为 tags_layout.hpp / accessor.hpp / level1.hpp / level2.hpp / level3.hpp
- 临时性能报告（dot 1M + GEMM 1024²，标量 vs Layer 1，/tmp/ 不提交）
- README + CLAUDE.md 全面更新

### Must NOT Have
- ❌ 平台 intrinsics（`_mm_*`/`vld1q_*`/`__rvv_*`）— Layer 1 only
- ❌ linalg execution policy 重载
- ❌ 改造 Phase 1-3 已有 sender 的 domain dispatch
- ❌ 修改 `FORGE_*` CMake 变量名或 `forge::forge` target
- ❌ 拆分 submdspan.hpp 或 concepts.hpp
- ❌ 定义 `__cpp_lib_simd` feature-test macro
- ❌ 修改 unique_resource backport
- ❌ 添加外部依赖（Boost/TBB/system BLAS）
- ❌ 将性能报告提交到仓库
- ❌ Level 1 操作的 OpenMP 并行（开销大于收益）

---

## Verification Strategy

### Test Policy
- simd Layer 1：全部现有 ~90 个 simd 测试必须通过（constexpr + runtime）
- execution Phase 4：每个新组件有 runtime test + compile probe
- linalg SIMD：标量 vs SIMD 数值等价（double 1e-12, float 1e-5, int 精确）
- linalg OpenMP：`OMP_NUM_THREADS=1` 和 `OMP_NUM_THREADS=4` 均正确
- OpenMP 隔离：Zig 构建零 OpenMP 符号泄漏
- 跨架构：4 arch zig+qemu 验证

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 0 (Rename — unblocks everything):
├── T1: Forge → CC Forge rename (README/CLAUDE.md/CMake/license headers) [quick]

Wave 1 (simd Layer 1 — critical path):
├── T2: simd vector extension storage backend (types.hpp dual path) [ultrabrain]
├── T3: simd vector ops replacement (arithmetic/comparison/reductions) [deep]
├── T4: simd 4-arch cross-compile verification + constexpr probes [unspecified-high]

Wave 2 (execution Phase 4 + linalg split — parallel tracks):
Track A (execution):
├── T5: forge::any_sender_of<Sigs...> in include/forge/ [deep]
├── T6: default_domain + get_domain CPO [unspecified-high]
├── T7: simple_counting_scope + counting_scope (P3149R11) [ultrabrain]
Track B (linalg restructure):
├── T8: linalg.hpp split into 5 sub-files [unspecified-high]
├── T9: linalg SIMD extension — Level 1 (copy/scale/add/swap) [unspecified-high]
├── T10: linalg SIMD extension — GEMV inner loop [deep]
├── T11: linalg OpenMP — GEMM/GEMV outer loops [unspecified-high]

Wave 3 (Verification + perf):
├── T12: linalg 4-arch SIMD cross-compile verification [unspecified-high]
├── T13: OpenMP isolation test (Zig zero-leak) [quick]
├── T14: simd + linalg performance comparison report (/tmp/, not committed) [unspecified-high]

Wave 4 (Documentation):
├── T15: README.md comprehensive update [writing]
├── T16: CLAUDE.md update (rename + new components) [writing]

Wave FINAL:
├── F1: Plan compliance audit
├── F2: Code quality + 4-toolchain QA
├── F3: 4-architecture QA
├── F4: Scope fidelity check
```

### Dependency Matrix

| Task | Depends On | Blocks |
|------|-----------|--------|
| T1 | — | all |
| T2 | T1 | T3 |
| T3 | T2 | T4, T9, T10 |
| T4 | T3 | — |
| T5 | T1 | — |
| T6 | T1 | T7 |
| T7 | T6 | — |
| T8 | T1 | T9, T10, T11 |
| T9 | T3, T8 | T12 |
| T10 | T3, T8 | T12 |
| T11 | T8 | T13 |
| T12 | T9, T10 | T14 |
| T13 | T11 | — |
| T14 | T4, T12 | — |
| T15-T16 | all impl | — |

---

## TODOs

- [ ] 1. Forge → CC Forge rename

  **What to do**:
  - README.md: `# Forge` → `# CC Forge`; all "Forge" in Chinese text → "CC Forge"
  - CLAUDE.md: all "Forge" references → "CC Forge"
  - CMakeLists.txt: `project(Forge ...)` → `project(CCForge ...)`
  - forge.cmake: all `message(STATUS "Forge: ..."`) → `message(STATUS "CC Forge: ...")`
  - All backport/ + include/ license headers: `Copyright (c) 2026 Forge Project` → `Copyright (c) 2026 CC Forge Project`
  - Keep `FORGE_*` variable names and `forge::forge` target name unchanged
  - Keep `forge.cmake` filename unchanged
  - Keep `namespace forge` unchanged (it's the brand name, not "Forge" the old project name)

  **Commit**: `rename: Forge → CC Forge in user-visible text and license headers`

- [ ] 2. simd vector extension storage backend

  **What to do**:
  - In `backport/cpp26/simd/types.hpp`, add vector extension storage type:
    ```cpp
    template<class T, int N>
    using __vec_ext_t = T __attribute__((vector_size(sizeof(T) * N)));
    ```
  - Replace `std::array<T, N> data_` in `basic_vec` with dual storage:
    ```cpp
    union {
        std::array<T, N> scalar_data_;  // constexpr path
        __vec_ext_t<T, N> vec_data_;    // runtime path
    };
    ```
  - Use `if !std::is_constant_evaluated()` in constructors to route between paths
  - Keep ALL constexpr semantics intact (scalar path for compile-time)
  - All element access `data_[i]` becomes `scalar_data_[i]` (or helper method)
  - 51+ access sites to update across types.hpp and memory_common.hpp

  **Commit**: `feat(simd): add vector extension storage backend with constexpr dual path`

- [ ] 3. simd vector ops replacement

  **What to do**:
  - Replace scalar `for` loops in `operator+=`, `operator-=`, `operator*=`, etc. with direct vector arithmetic:
    ```cpp
    if (!std::is_constant_evaluated()) {
        vec_data_ += other.vec_data_;
    } else {
        for (int i = 0; i < N; ++i) scalar_data_[i] += other.scalar_data_[i];
    }
    ```
  - Replace scalar reduction in `reductions.hpp` with vector-aware reduce
  - Replace comparison ops with vector comparison + mask extraction
  - All operations in operations.hpp, reductions.hpp, types.hpp

  **Commit**: `feat(simd): replace scalar loops with vector extension arithmetic`

- [ ] 4. simd 4-arch verification + constexpr probes

  **What to do**:
  - Run full simd test suite on GCC (native x86_64)
  - Cross-compile + qemu: aarch64, riscv64, loongarch64
  - Add constexpr compile probes: `static_assert(std::simd::basic_vec<int, ...>{1,2,3}[0] == 1)`
  - Verify ALL existing simd tests pass

  **Commit**: `test(simd): add constexpr compile probes for vector extension backend`

- [ ] 5. forge::any_sender_of<Sigs...>

  **What to do**:
  - Create `include/forge/any_sender.hpp`
  - Implement `forge::any_sender_of<completion_signatures<Sigs...>>` with SBO (64B) + heap fallback
  - Uses virtual dispatch for connect/get_completion_signatures/get_env
  - The returned op-state is also type-erased
  - Lives in `namespace forge` (NOT `namespace std::execution`)
  - Test in `test/forge/test_any_sender.cpp`

  **Commit**: `feat(forge): add any_sender_of<Sigs...> type-erased sender`

- [ ] 6. default_domain + get_domain CPO

  **What to do**:
  - Create `backport/cpp26/execution/domain.hpp`
  - `struct default_domain {}` with `transform_sender`/`transform_env` identity functions
  - `get_domain` CPO: queries sender env for domain, defaults to `default_domain`
  - Include in execution.hpp coordinator
  - Test: compile probe + basic runtime test

  **Commit**: `feat(execution): add default_domain and get_domain CPO`

- [ ] 7. simple_counting_scope + counting_scope (P3149R11)

  **What to do**:
  - Create `backport/cpp26/execution/counting_scope.hpp`
  - `simple_counting_scope`: basic scope with `get_token()`→`scope_token`, `close()`, `join()`
  - `scope_token::associate(sndr)` wraps sender, increments count on start, decrements on complete
  - `scope_token::spawn(sndr)` = `start_detached(associate(sndr))`
  - `scope_token::spawn_future(sndr)` = `ensure_started(associate(sndr))`
  - `counting_scope`: adds nestable token support
  - Atomic refcount + mutex for join waiters
  - Include in execution.hpp coordinator
  - Tests: spawn lifecycle, concurrent spawn+join, close prevents new spawns

  **Commit**: `feat(execution): add simple_counting_scope and counting_scope (P3149R11)`

- [ ] 8. linalg.hpp split into 5 sub-files

  **What to do**:
  - Create `backport/cpp26/linalg/` directory
  - Split into: `tags_layout.hpp` (~200 lines: tags + layout_blas_packed)
  - `accessor.hpp` (~200 lines: scaled/conjugated/transposed accessors + utility functions)
  - `level1.hpp` (~300 lines: all BLAS L1 + SIMD detection traits)
  - `level2.hpp` (~200 lines: GEMV + triangular + symmetric/hermitian)
  - `level3.hpp` (~200 lines: GEMM + triangular matrix + rank-k)
  - `linalg.hpp` becomes thin coordinator (~40 lines: includes + namespace)
  - Preserve `#if defined(__cpp_lib_mdspan)` guard in each sub-file
  - ALL linalg tests must pass identically

  **Commit**: `refactor(linalg): split linalg.hpp into 5 modular sub-files`

- [ ] 9. linalg SIMD extension — Level 1 (copy/scale/add/swap)

  **What to do**:
  - Add SIMD paths to `copy()`, `scale()`, `add()`, `swap_elements()` in `level1.hpp`
  - Pattern: `#if __LINALG_HAS_SIMD` + `if constexpr (__detail::__can_simd_v<...>)` + batch kN load/op/store + tail scalar
  - For write-back ops (copy/scale/add/swap): use `basic_vec` element-by-element store or raw pointer write
  - Tests: numerical equivalence for all types (float/double/int)

  **Commit**: `feat(linalg): extend SIMD acceleration to copy, scale, add, swap_elements`

- [ ] 10. linalg SIMD extension — GEMV inner loop

  **What to do**:
  - In `matrix_vector_product()` (level2.hpp), add SIMD for inner dot-product loop
  - Each row of GEMV is a dot product — reuse the SIMD dot pattern
  - Only for default_accessor + contiguous layout
  - Test: GEMV numerical equivalence

  **Commit**: `feat(linalg): add SIMD acceleration for GEMV inner loop`

- [ ] 11. linalg OpenMP — GEMM/GEMV outer loops

  **What to do**:
  - Add `#ifdef _OPENMP` + `#include <omp.h>` (conditional) to level2.hpp and level3.hpp
  - GEMV: `#pragma omp parallel for` on row loop
  - GEMM: `#pragma omp parallel for` on outer row loop
  - `#ifdef _OPENMP` must wrap BOTH the pragma AND any omp API calls
  - Test: correct results with OMP_NUM_THREADS=1 and OMP_NUM_THREADS=4
  - Verify Zig build succeeds without OpenMP

  **Commit**: `feat(linalg): add OpenMP parallel for GEMM/GEMV outer loops`

- [ ] 12. linalg 4-arch SIMD cross-compile verification

  **What to do**:
  - zig cross-compile + qemu for aarch64/riscv64/loongarch64
  - Run linalg Level 1/2/3 tests on each arch
  - Verify numerical correctness

  **Commit**: NO (verification only)

- [ ] 13. OpenMP isolation test

  **What to do**:
  - Build with Zig (no OpenMP)
  - `nm` or `objdump` check: zero `omp_*` or `GOMP_*` symbols in any binary
  - If any leak found: fix the `#ifdef _OPENMP` guard

  **Commit**: YES (if fixes needed)

- [ ] 14. Performance comparison report

  **What to do**:
  - Write benchmark script that measures:
    - `dot(double, N=1M)`: scalar vs simd Layer 1
    - `GEMM(double, 1024×1024)`: scalar vs simd+OpenMP
    - Per-architecture (x86_64 native, aarch64/riscv64/loongarch64 via qemu)
  - Output markdown table to `/tmp/ccforge_perf_report.md`
  - NOT committed to repository

  **Commit**: NO (temp report only)

- [ ] 15. README.md comprehensive update

  **What to do**:
  - Title: `# CC Forge`
  - Update all execution/linalg/simd status descriptions
  - Add `forge::any_sender_of` mention in extensions section
  - Add OpenMP note + SIMD Layer 1 note
  - Add cross-architecture verification section

  **Commit**: `docs: comprehensive README update for Phase 3`

- [ ] 16. CLAUDE.md update

  **What to do**:
  - All "Forge" → "CC Forge" in project descriptions
  - Update backport list with new components
  - Keep coding standards unchanged

  **Commit**: grouped with T15

---

## Final Verification Wave

- [ ] F1. Plan compliance audit
- [ ] F2. Code quality + 4-toolchain QA
- [ ] F3. 4-architecture QA (GCC + Zig + aarch64 + riscv64 + loongarch64)
- [ ] F4. Scope fidelity check

---

## Commit Strategy

1. `rename: Forge → CC Forge in user-visible text and license headers` → **PUSH**
2. `feat(simd): add vector extension storage backend with constexpr dual path`
3. `feat(simd): replace scalar loops with vector extension arithmetic`
4. `test(simd): add constexpr probes for vector extension backend` → **PUSH** (simd milestone)
5. `feat(forge): add any_sender_of<Sigs...> type-erased sender`
6. `feat(execution): add default_domain and get_domain CPO`
7. `feat(execution): add simple_counting_scope and counting_scope (P3149R11)` → **PUSH** (execution milestone)
8. `refactor(linalg): split linalg.hpp into 5 modular sub-files`
9. `feat(linalg): extend SIMD acceleration to copy, scale, add, swap_elements`
10. `feat(linalg): add SIMD acceleration for GEMV inner loop`
11. `feat(linalg): add OpenMP parallel for GEMM/GEMV outer loops` → **PUSH** (linalg milestone)
12-14: verification + perf report (no commits or fix commits only)
15-16: `docs: comprehensive README/CLAUDE.md update for Phase 3` → **PUSH** (final)

---

## Success Criteria

### Verification Commands
```bash
# Rename verification
grep -r "Forge Project" backport/ include/ | grep -v "CC Forge" | wc -l  # Expect: 0

# GCC full test suite
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=23
cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure

# Zig full test suite
CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON
cmake --build build-zig -j && ctest --test-dir build-zig --output-on-failure

# OpenMP isolation
nm build-zig/test/linalg/test_linalg_level3 2>/dev/null | grep -i omp | wc -l  # Expect: 0

# Cross-arch
for arch in aarch64 riscv64 loongarch64; do
  CC="zig cc -target ${arch}-linux-musl" CXX="zig c++ -target ${arch}-linux-musl" \
    cmake -S . -B build-${arch} -DFORGE_BUILD_TESTS=ON
  cmake --build build-${arch} -j
  qemu-${arch}-static build-${arch}/test/linalg/test_linalg_level1
done

# Perf report
cat /tmp/ccforge_perf_report.md
```
