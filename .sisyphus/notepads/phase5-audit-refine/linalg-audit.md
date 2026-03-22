# std::linalg 审计报告

**审计日期：** 2026-03-22  
**标准参考：** C++26 Working Draft [linalg] (eel.is)  
**审计范围：** CC Forge backport/cpp26/linalg/

---

## 执行摘要

CC Forge 的 std::linalg 实现已模块化拆分为 6 个文件。本审计对比 C++26 working draft 验证完整性。

---

## 规范基准

**权威标准参考**:
- **C++26 Working Draft**: https://eel.is/c++draft/linalg
- **Section**: 29.9 Basic linear algebra algorithms [[linalg]]
- **Feature Test Macro**: `__cpp_lib_linalg` = 202311L
- **历史背景**: P1673R13 (已投票进入工作草案)

---

## CC Forge 实现结构

### 文件组织
```
backport/cpp26/linalg.hpp          # 主入口
backport/cpp26/linalg/
  ├── detail.hpp                   # 内部辅助
  ├── tags_layout.hpp              # 标签和布局类
  ├── accessor.hpp                 # 访问器
  ├── level1.hpp                   # BLAS Level 1
  ├── level2.hpp                   # BLAS Level 2
  └── level3.hpp                   # BLAS Level 3
```

---

## 标准 API 清单

### 1. Tags (标签类型)

#### Storage Order Tags
- `column_major_t` + `column_major`
- `row_major_t` + `row_major`

#### Triangle Tags
- `upper_triangle_t` + `upper_triangle`
- `lower_triangle_t` + `lower_triangle`

#### Diagonal Tags
- `implicit_unit_diagonal_t` + `implicit_unit_diagonal`
- `explicit_diagonal_t` + `explicit_diagonal`

### 2. Layout Classes

- `layout_blas_packed<Triangle, StorageOrder>` - packed matrix layout

### 3. In-place Transformations

#### Scaled
- `scaled_accessor<ScalingFactor, NestedAccessor>`
- `scaled(ScalingFactor, mdspan)`

#### Conjugated
- `conjugated_accessor<NestedAccessor>`
- `conjugated(mdspan)`

#### Transposed
- `layout_transpose<Layout>`
- `transposed(mdspan)`
- `conjugate_transposed(mdspan)`

---

## BLAS Level 1 Functions (13 组)

1. `setup_givens_rotation` - Givens 旋转设置
2. `apply_givens_rotation` - 应用 Givens 旋转
3. `swap_elements` - 交换元素
4. `scale` - 缩放
5. `copy` - 复制
6. `add` - 加法
7. `dot` - 点积
8. `dotc` - 共轭点积
9. `vector_two_norm` - 欧几里得范数
10. `vector_abs_sum` - 绝对值和
11. `vector_idx_abs_max` - 最大绝对值索引
12. `matrix_frob_norm` - Frobenius 范数
13. `matrix_one_norm` - 1-范数
14. `matrix_inf_norm` - 无穷范数


## BLAS Level 2 Functions (8 组)

1. `matrix_vector_product` - 通用矩阵-向量乘法 (GEMV)
2. `symmetric_matrix_vector_product` - 对称矩阵-向量乘法
3. `hermitian_matrix_vector_product` - Hermitian 矩阵-向量乘法
4. `triangular_matrix_vector_product` - 三角矩阵-向量乘法
5. `triangular_matrix_vector_solve` - 三角矩阵-向量求解
6. `matrix_rank_1_update` - 秩-1 更新
7. `symmetric_matrix_rank_1_update` - 对称秩-1 更新
8. `hermitian_matrix_rank_1_update` - Hermitian 秩-1 更新
9. `symmetric_matrix_rank_2_update` - 对称秩-2 更新
10. `hermitian_matrix_rank_2_update` - Hermitian 秩-2 更新

## BLAS Level 3 Functions (10 组)

1. `matrix_product` - 通用矩阵乘法 (GEMM)
2. `symmetric_matrix_product` - 对称矩阵乘法
3. `hermitian_matrix_product` - Hermitian 矩阵乘法
4. `triangular_matrix_product` - 三角矩阵乘法
5. `triangular_matrix_left_product` - 三角矩阵左乘
6. `triangular_matrix_right_product` - 三角矩阵右乘
7. `symmetric_matrix_rank_k_update` - 对称秩-k 更新
8. `hermitian_matrix_rank_k_update` - Hermitian 秩-k 更新
9. `symmetric_matrix_rank_2k_update` - 对称秩-2k 更新
10. `hermitian_matrix_rank_2k_update` - Hermitian 秩-2k 更新
11. `triangular_matrix_matrix_left_solve` - 三角矩阵左求解
12. `triangular_matrix_matrix_right_solve` - 三角矩阵右求解


---

## 审计方法

需要逐个检查 CC Forge 实现：

1. **Tags & Layout** - 检查 `tags_layout.hpp`
2. **Accessors** - 检查 `accessor.hpp`
3. **BLAS 1** - 检查 `level1.hpp` 中的 14 个函数
4. **BLAS 2** - 检查 `level2.hpp` 中的 10 个函数
5. **BLAS 3** - 检查 `level3.hpp` 中的 12 个函数
6. **ExecutionPolicy** - 验证所有函数的并行重载
7. **Feature Macro** - 验证 `__cpp_lib_linalg = 202311L`

---

## 初步发现

✅ **已确认**:
- 文件结构清晰，按 BLAS Level 组织
- `level1.hpp` 包含 `copy`, `scale` 等函数
- 支持 SIMD 加速（通过 `__LINALG_HAS_SIMD`）

❓ **待验证**:
- 所有标准要求的函数是否都已实现
- ExecutionPolicy 重载是否完整
- 命名是否与标准完全一致

---

## 下一步

1. 逐文件检查函数列表
2. 对比标准 API 清单
3. 标记缺失或不符合项
4. 生成完整的缺口报告

