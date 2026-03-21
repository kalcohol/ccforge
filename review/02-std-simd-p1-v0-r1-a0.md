# `std::simd` 审查答复 (p1-v0-r1-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

---

## 逐条答复

### I-1 `simd-complex` 路径整体未打开

裁决：**已知，不在本轮范围内。**

此项在 r8 审查链中已作为 Fix-8 裁决（参见 `review/02-std-simd-p0-v0-r8-final.md`），明确归入后续阶段。complex 支持工作量大，需要独立的设计和实施周期。本轮不重复处理。

### I-2 标准算法表面仍是明显子集

裁决：**已知，不在本轮范围内。**

同 I-1，此项在 r8-final 中已作为 Fix-8 裁决。bit family、math family 扩展和 complex family 均归入后续阶段。

### I-3 integral `basic_vec` 位移运算符集合不完整

裁决：**接受。**

当前实现确实只有标量位移重载：

```cpp
template<class Shift> operator<<=(Shift shift)   // Shift 是标量
template<class Shift> operator<<(basic_vec, Shift) // Shift 是标量
```

现行草案 `[simd.binary]` 要求同时支持逐 lane 向量位移：

```cpp
basic_vec& operator<<=(const basic_vec& shift)
friend basic_vec operator<<(const basic_vec&, const basic_vec& shift)
```

`where_expression` 同理需要向量位移赋值重载。

修复计划：

1. `types.hpp`：为 `operator<<=`、`operator>>=`、`operator<<`、`operator>>` 各新增 `const basic_vec&` 重载
2. `where.hpp`：为 `operator<<=`、`operator>>=` 各新增 `const value_type&` 重载
3. 测试：新增向量位移运行时测试 + compile probe

### I-4 现有测试仍未把"标准表面完整性"锁住

裁决：**认同观察，但不作为独立修复项。**

这是一个元层面的观察，不是具体代码缺陷。每次补齐标准表面时同步补测试是正确做法，但"系统性锁住所有标准表面"需要一个完整的 surface audit，不适合作为单次修复项处理。I-3 的修复会同步补齐对应测试。

---

## 修复计划

仅 I-3 需要代码修改：

### Fix-9：补齐逐 lane 向量位移重载

**types.hpp 改动：**

- `operator<<=(const basic_vec&)` — 逐 lane 左移赋值
- `operator>>=(const basic_vec&)` — 逐 lane 右移赋值
- `operator<<(basic_vec, const basic_vec&)` — 逐 lane 左移
- `operator>>(basic_vec, const basic_vec&)` — 逐 lane 右移

约束：仅当 `is_integral<T>` 时参与重载决议。

**where.hpp 改动：**

- `operator<<=(const value_type&)` — masked 逐 lane 左移赋值
- `operator>>=(const value_type&)` — masked 逐 lane 右移赋值

约束：仅当 `is_integral<T>` 时参与重载决议。

**测试：**

- `test_simd.cpp`：运行时测试验证逐 lane 位移正确性
- `test_simd_memory.cpp`：`where` 向量位移赋值测试
- `test_simd_api.cpp`：compile probe 验证向量位移重载可见
