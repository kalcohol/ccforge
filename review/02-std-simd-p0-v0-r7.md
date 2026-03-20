# `std::simd` 独立审查报告 (p0-v0-r7)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd*.cpp`、`test/CMakeLists.txt`
- 示例文件：`example/simd_example.cpp`、`example/simd_complete_example.cpp`

审查基线：
- 当前代码状态截至 `2026-03-21`
- 参考口径：当前草案 `[simd]` 与仓库“transparent backport”原则
- 本文为新的 review 文档，不复述既有 review 结论

---

## 执行摘要

当前 `std::simd` backport 在现有运行时测试下可用，但距离“标准委员会可接受、标准库可交付”的质量要求仍有明显差距。问题不在于纯标量回退本身，而在于：

1. 若干标准表面仍未补齐
2. 若干构造与约束仍然过宽或过窄
3. 当前测试没有把这些缺口升级为持续失败的质量门槛

本轮确认的高优先级问题是：

1. `basic_vec` 缺少值保持的 converting constructor
2. 标量广播构造和 generator 构造约束过松，错误接受非 value-preserving 转换
3. load/store/gather/scatter 对非指针迭代器仍按 random-access 放行，而不是收紧到 contiguous
4. range 构造及 range 版 load/store/gather/scatter 入口缺失
5. `compress` / `expand` / `select` 的标准表面仍不完整
6. 重新 configure 时的 `simd` 质量门槛与已有 build 产物状态分裂，说明验证闭环仍未收口

---

## 第一部分：已确认实现缺陷

### I-1 `basic_vec` 缺少值保持的 converting constructor

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前 `basic_vec` 只提供：

```cpp
template<class U, class OtherAbi>
constexpr explicit basic_vec(const basic_vec<U, OtherAbi>& other, flags<convert_flag>) noexcept;
```

这意味着即使是值保持转换，例如：

- `vec<int, 4> -> vec<long long, 4>`
- `vec<float, 4> -> vec<double, 4>`

也没有对应的普通 converting constructor。

**影响：**

- 当前公开表面仍偏离草案构造语义
- 用户无法使用标准期望的值保持宽化构造
- 下游代码会被迫走额外的 `flag_convert` 或显式 cast 路径

**修复要求：**

- 补齐值保持 converting constructor
- 保留现有 `flag_convert` 路径处理非值保持转换
- 为值保持 widening 增加 compile probe 与 runtime coverage

### I-2 标量广播构造和 generator 构造约束过松

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前：

- 标量广播构造只接受精确的 `T`
- generator 构造使用 `is_invocable_r<T, ...>`，本质上只要求能 `static_cast<T>`

这会错误接受非 value-preserving 的输入，例如：

- `int -> vec<float, 4>` 的隐式广播
- 生成 `int` 的 generator 构造 `vec<float, 4>`

**影响：**

- 公开构造表面过宽
- 与仓库“不得注入额外放宽接口”的原则冲突
- 会把 narrowing / lossy conversion 混入本应保守的构造路径

**修复要求：**

- 标量广播构造改为按 value-preserving 规则决定是否可隐式
- generator 构造改为按 lane value 到 `T` 的 value-preserving 规则约束
- 以 compile probe 锁定 `vec<float, 4>(vec<int, 4>{})` 仍需显式 `flag_convert`

---

## 第二部分：标准表面缺口

### I-3 非指针内存 API 对迭代器的要求仍然过宽

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前 `partial_load` / `partial_store` / `unchecked_load` / `unchecked_store` / gather / scatter
对非指针实参采用的是 random-access iterator 约束。

这会错误接受诸如 `std::deque<int>::iterator` 这类并非 contiguous 的迭代器。

**影响：**

- 内存接口契约比标准更宽
- 可能让下游错误地把非 contiguous 存储接入到 backport 上
- 与 range 版本接口未来的行为边界不一致

**修复要求：**

- 把非指针路径从 random-access 收紧到 contiguous
- 增加负向 compile probe，明确拒绝 `std::deque<int>::iterator`

### I-4 range 构造和 range 版内存入口缺失

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前实现只支持：

- pointer + count
- iterator + count
- iterator + sentinel

但仍缺少：

- `basic_vec(R&&)` / `basic_mask(R&&)` 的 contiguous range 构造
- `partial_load(R&&)` / `partial_store(R&&)`
- `unchecked_load(R&&)` / `unchecked_store(R&&)`
- `partial_gather_from(R&&, indices)` / `partial_scatter_to(value, R&&, indices)`
- `unchecked_gather_from(R&&, indices)` / `unchecked_scatter_to(value, R&&, indices)`

**影响：**

- 标准表面不完整
- 用户必须手工拆成 `data()` + `size()` 调用
- current tests 无法覆盖 range 语义路径

**修复要求：**

- 补齐 contiguous range 入口
- 统一复用现有 pointer 实现，不复制转换逻辑
- 为 `std::array` / `std::span` 风格输入增加 compile/runtime coverage

### I-5 `compress` / `expand` / `select` 的算法面未补齐

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前实现只有最基础的：

- `compress(vec, mask)`
- `expand(vec, mask)`
- 若干特化版 `select`

仍缺少：

- `compress(mask, mask)`
- `expand(mask, mask)`
- `compress(vec, mask, fill_value)`
- `expand(vec, mask, original)`
- mask 条件下的标量 `select`

**影响：**

- 算法表面仍未封口
- 部分标准用法只能靠手工展开绕开

**修复要求：**

- 补齐缺失重载
- 把新增入口纳入 compile probes
- 为 vector 和 mask 两条路径分别补最小 runtime coverage

---

## 第三部分：质量闭环问题

### Q-1 当前 `simd` 质量门槛与 build 产物状态分裂

- 严重程度：高
- 位置：`test/CMakeLists.txt`、已有 `build/`

**问题描述：**

本轮复核中，已有 `build/` 下的 `simd_*` CTest 可全部通过；但重新执行 configure/build 时，`test/CMakeLists.txt` 中的 `std::simd` configure-time probes 会在更早阶段失败。

这说明当前仓库存在：

- 旧 build 产物可绿
- 源码树重新配置不可绿

的分裂状态。

**影响：**

- 当前“测试通过”不代表当前源码树可交付
- 修复若不以 clean configure 为基准，仍可能保留假阳性

**修复要求：**

- 修复过程中每轮都以重新 configure 为准
- 最终验收必须包含 clean GCC / Zig / Podman 三套矩阵

### Q-2 当前 `simd` 测试尚未把新缺口升级为硬门槛

- 严重程度：中
- 位置：`test/test_simd_api.cpp`、`test/test_simd_memory.cpp`、`test/CMakeLists.txt`

**问题描述：**

当前测试已覆盖部分 `where`、mask unary、`flag_convert` 语义，但尚未对以下问题形成硬门槛：

- 值保持 vector converting constructor
- contiguous iterator 约束
- contiguous range 入口
- `compress` / `expand` / `select` 的缺失重载

**处理方式：**

- 修复完成后，把上述表面全部升级为 compile-time / configure-time 质量门槛
- 不再允许缺口只停留在 review 文档层面

---

## 修复顺序

### 第一轮：修构造与约束

范围：

- `I-1`
- `I-2`

目标：

- 补齐值保持 vector converting constructor
- 收紧标量广播和 generator 构造约束

验收：

- widening vector construction 正向通过
- 非值保持构造仍需 `flag_convert`

### 第二轮：补 contiguous / range 内存表面

范围：

- `I-3`
- `I-4`

目标：

- 非指针路径只接受 contiguous
- 补齐 range 版构造和内存 API

验收：

- `std::vector<int>::iterator` 正向通过
- `std::deque<int>::iterator` 负向拒绝
- `std::array` / `std::span` 风格入口可用

### 第三轮：补算法表面与测试闭环

范围：

- `I-5`
- `Q-1`
- `Q-2`

目标：

- 补齐 `compress` / `expand` / `select` 缺失重载
- 同步升级 compile probes / runtime tests
- 消除“旧 build 可绿、重配失败”的状态分裂

验收：

- clean configure 通过
- `simd_*` 编译和运行测试全绿
- GCC / Zig / Podman 全矩阵通过
