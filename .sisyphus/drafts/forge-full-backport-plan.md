# Draft: Forge 全量 Backport 工作计划

## 需求（已确认）

用户希望 Forge 项目实现四个主力 C++26 backport：
1. `std::unique_resource` — ✅ 已完成
2. `std::simd` — ✅ 已完成（p0+p1，current draft 对齐）
3. `std::execution` (P2300) — ⚠️ MVP 阶段，需推进到 Phase 1+
4. `std::linalg` (P1673) — ❌ 未开始，需全新实现

`std::submdspan` 是 `linalg` 的内部依赖，不需要作为独立的公开 backport 目标——但 Forge 已有 `backport/mdspan` wrapper 和 `backport/cpp26/submdspan.hpp` 实现（659 行），且 forge.cmake 已有检测。可以保持现状作为基础设施使用。

## 技术决策（已确认）

### submdspan 处理策略
- submdspan 已在 `backport/cpp26/submdspan.hpp` 中实现（Phase 1 scope，659 行）
- 已通过 `backport/mdspan` wrapper 注入 `namespace std`
- forge.cmake 已有 `HAS_STD_SUBMDSPAN` 检测
- **决策**：保持现状，作为 linalg 的内部基础设施。linalg 直接 `#include <mdspan>` 使用 `std::submdspan`，无需额外隔离
- submdspan 的测试已在 `test/submdspan/` 中存在

### execution 优先级
- 用户确认 execution 优先级高于 linalg
- 需要先完成 execution Phase 1（run_loop、let_*、enable_sender、tag_invoke 迁移等遗留项）
- linalg Phase 0/1 可以与 execution Phase 1 后半段并行

### linalg 策略
- 可以按 Phase 0 → Phase 1 → Phase 2 → Phase 3 顺序推进
- Phase 0（辅助组件）+ Phase 1（BLAS Level 1）不依赖 submdspan
- Phase 2/3 需要 submdspan，已有实现可用

### 自动化执行
- 用户希望计划制定后能自行迭代完成全部任务
- 中间需要细粒度提交
- 恰当时机推送

## 当前 execution 缺口分析

### 已完成
- inplace_stop_token / inplace_stop_source / inplace_stop_callback
- never_stop_token
- stoppable_token / stoppable_token_for / unstoppable_token concepts
- completion_signatures + meta utilities (value_types_of, error_types_of, sends_stopped_v)
- queryable concept
- receiver concept (已对齐 nothrow_move + non-final)
- sender concept (sender_concept marker)
- operation_state concept (operation_state_concept marker)
- scheduler concept (scheduler_concept marker)
- get_env / get_scheduler / get_stop_token / get_allocator CPOs
- set_value / set_error / set_stopped CPOs
- connect / start CPOs
- schedule CPO
- just / just_error / just_stopped
- then (含 pipe operator)
- sync_wait (已迁移到 std::this_thread)
- inline_scheduler
- env / prop / make_env

### 遗留问题（来自 review r1/r2）
- tag_invoke → 成员函数优先 + domain-based 分发迁移 (r1 I-1)
- run_loop 实现 + sync_wait receiver env 对齐 (r1 I-10)
- just sender env 补充 completion scheduler (r1 I-12)
- sender_adaptor_closure CRTP 基类迁移 (r1 I-13)
- enable_sender trait + awaitable sender 支持 (r2 I-6)
- get_completion_scheduler CPO shell (r2 I-8)

### Phase 1 还需要实现
- upon_error, upon_stopped（then 的兄弟适配器）
- let_value, let_error, let_stopped
- read_env
- starts_on, continues_on
- stopped_as_optional, stopped_as_error
- run_loop（关键：sync_wait 的正确驱动）
- get_completion_scheduler CPO
- enable_sender trait
- tag_invoke → member function 迁移

## linalg 实现规模估算

### Phase 0（辅助组件）~800-1200 行
- scaled_accessor, conjugated_accessor
- layout_transpose, layout_blas_packed
- 标记类型（column_major_t 等）
- scaled(), conjugated(), transposed(), conjugate_transposed()
- `<linalg>` wrapper header + forge.cmake 检测

### Phase 1（BLAS Level 1）~600-900 行
- copy, scale, swap_elements
- dot, dotc, add
- vector_two_norm, vector_abs_sum, vector_idx_abs_max
- vector_sum_of_squares
- givens_rotation_setup, givens_rotation_apply

### Phase 2（BLAS Level 2）~1500-2500 行 [依赖 submdspan]
- matrix_vector_product
- triangular_matrix_vector_product, triangular_matrix_vector_solve
- symmetric/hermitian_matrix_vector_product
- 秩更新操作

### Phase 3（BLAS Level 3）~2500-4000 行 [依赖 submdspan]
- matrix_product (GEMM)
- triangular_matrix_left/right_product
- symmetric/hermitian_matrix_product
- 秩-k/2k 更新
- 三角求解

## 参考实现来源

- **execution**: NVIDIA stdexec (stdexec 使用成员函数优先 CPO，已弃用 tag_invoke)
- **linalg**: kokkos/stdBLAS (三重载模式: inline_exec / policy / default)
- **submdspan**: kokkos/mdspan (已作为 Forge 实现参考)

## 开放问题

无——用户意图和技术策略已明确。
