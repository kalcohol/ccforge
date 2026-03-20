# `std::unique_resource` Backport — Primary Review Report (p0-v1)

**日期：** 2026-03-20
**审查者：** Claude Opus 4.6 (主审，独立分析)
**审查对象：**
- `backport/cpp26/unique_resource.hpp`
- `test/test_unique_resource*.cpp` (全部 7 个文件)
- `example/unique_resource_example.cpp`

**参考规范：** P0052R15 (C++26 `std::unique_resource`)
**其他报告：** v1-r0、v2-r0、v3-r0（已阅，但本报告基于独立源码分析，仅在关键分歧处标注与其他报告的差异）

---

## 0. 执行摘要

**总体结论：生产可用，但存在一个真正的符合性缺陷（W-1）和若干代码质量问题。**

实现在异常安全性方面表现优秀，move-copy fallback 逻辑正确，资源所有权转移语义可靠。API 设计忠实于 P0052R15。非参考标准实现库独立调查发现了以下问题：

| 级别 | 问题数 | 说明 |
|------|--------|------|
| 符合性缺陷（Blocker） | 1 | `reset(RR&&)` `static_assert` 替代 `requires` |
| 规范措辞偏差 | 2 | move assign `noexcept`；`operator*` `noexcept` |
| 死代码 | 1 | `construct_resource` / `construct_deleter` 未被调用 |
| 缺失特性 | 2 | `constexpr`；default constructor 待确认 |
| 测试缺口 | 4 | self-move、self-swap、noexcept 编译期验证等 |

---

## 1. 符合性缺陷

### C-1：`reset(RR&&)` 使用 `static_assert` 代替 `requires` 约束

**位置：** `unique_resource.hpp:118-123`

**当前实现：**
```cpp
template<class RR>
void reset(RR&& r) {
    static_assert(
        is_assignable_v<resource_storage&, RR> ||
        is_assignable_v<resource_storage&, const remove_reference_t<RR>&>,
        "reset(RR&&) requires resource to be assignable from RR");
```

**P0052R15 要求（[unique.res.reset]/3）：**
> *Constraints:* The expression `(is_assignable_v<RS&, RR> || is_assignable_v<RS&, const remove_reference_t<RR>&>)` is true.

`Constraints` 使重载参与 overload resolution 的选入/排除逻辑：不满足时该重载被 *silently removed*，而非产生硬编译错误。`static_assert` 的语义完全不同：无论条件是否满足，重载都进入重载集合，不满足时给出 hard error。

**实际影响（非理论）：**

```cpp
// 以下代码在标准下合法（SFINAE 排除过约束的重载），在当前实现下 hard error
template<class T>
concept resettable = requires(T& t, int v) { t.reset(v); };

struct non_assignable_resource { /* operator= deleted */ };
// unique_resource<non_assignable_resource, D>::reset(x)
// 标准：concept 检查结果为 false（overload 被排除）
// 当前：concept 检查触发 static_assert → 编译失败
```

**严重性：中（符合性缺陷，影响 SFINAE/concepts 调用点）**

**修复：**
```cpp
template<class RR>
    requires (is_assignable_v<resource_storage&, RR> ||
              is_assignable_v<resource_storage&, const remove_reference_t<RR>&>)
void reset(RR&& r) { ... }
```

---

## 2. 规范措辞偏差

### W-1：move assignment `noexcept` 使用 `R` 而非 `resource_storage`

**位置：** `unique_resource.hpp:74-76`

**当前实现：**
```cpp
unique_resource& operator=(unique_resource&& other) noexcept(
    is_nothrow_move_assignable_v<R> &&
    is_nothrow_move_assignable_v<D>)
```

**P0052R15 §[unique.res.assign]：**
> `noexcept(is_nothrow_move_assignable_v<RS> && is_nothrow_move_assignable_v<D>)`
> 其中 `RS` 为 stored resource handle 的类型（即 `resource_storage`）。

**语义影响分析：**

| R 类型 | `is_nothrow_move_assignable_v<R>` | `is_nothrow_move_assignable_v<resource_storage>` | 是否有差异 |
|--------|----------------------------------|--------------------------------------------------|------------|
| `int` | `true` | `true` | 相同 |
| `int&` | `true`* | `true` (`reference_wrapper<int>` 是 nothrow MA) | 相同 |
| `T` with throwing MA | depends | depends | 相同 |

*注：`is_nothrow_move_assignable_v<int&>` — 引用类型 `T&` 的 `operator=` 是对目标 `T` 的赋值，对 `int` 是 noexcept，故为 true。`reference_wrapper<int>` 的 move assign 也是 noexcept。

结论：对所有标准使用场景，二者在实际 `noexcept` 值上无差异。此为纯措辞偏差，**无运行时/编译期语义影响**。但出于严格符合性，应修正。

**严重性：低（措辞偏差，无实际影响）**

---

### W-2：`operator*` 使用条件 `noexcept` 而非无条件 `noexcept`

**位置：** `unique_resource.hpp:154`

**当前实现：**
```cpp
add_lvalue_reference_t<remove_pointer_t<T>> operator*() const noexcept(noexcept(*declval<const T&>())) {
```

**P0052R15 §[unique.res.observers]：**
> `decltype(auto) operator*() const noexcept;`

标准指定 `operator*` 无条件 `noexcept`。对于实际使用的指针类型（`T*` 其中 `T` 非 void），解引用始终是 noexcept，故条件 `noexcept` 的结果也是 `true`。但形式上是偏差。

**严重性：低（措辞偏差，原始指针场景无差异）**

---

## 3. 代码质量问题

### Q-1：`construct_resource` 和 `construct_deleter` 是未被调用的死代码

**位置：** `unique_resource.hpp:190-208`

```cpp
template<class RR>
static resource_storage construct_resource(RR&& r) { ... }  // ← 未被调用

template<class DD>
static D construct_deleter(DD&& d) { ... }  // ← 未被调用
```

全文搜索确认：没有任何地方调用 `construct_resource` 或 `construct_deleter`。

实际初始化路径为：
- 公共构造函数通过私有 `construct_tag` 构造函数调用 `construct_resource_from_input` 和 `construct_deleter_from_input`
- move 构造函数调用 `construct_resource_from_other` 和 `construct_deleter_from_other`

这两个方法是早期重构遗留的残余代码，应删除。保留死代码会误导维护者，且在 header-only 库中会增加实例化负担（尽管未实际调用）。

**严重性：低（代码质量）**

---

### Q-2：free `swap` 函数手动重复 `resource_storage` 计算

**位置：** `unique_resource.hpp:294-301`

```cpp
template<class R, class D>
void swap(unique_resource<R, D>& lhs, unique_resource<R, D>& rhs)
    noexcept(is_nothrow_swappable_v<conditional_t<is_reference_v<R>,
        reference_wrapper<remove_reference_t<R>>, R>> && ...)
    requires (is_swappable_v<conditional_t<is_reference_v<R>,
        reference_wrapper<remove_reference_t<R>>, R>> && ...)
```

类外 free `swap` 必须手动复刻 `resource_storage` 的 `conditional_t` 计算（因为 `resource_storage` 是私有 type alias）。这在 `unique_resource<R, D>` 需要改变 `resource_storage` 定义时会导致不同步。

**注意：** 这是 free function 无法访问私有 alias 的必然结果，在不暴露 `resource_storage` 的情况下无法完全消除，但可通过 type trait helper 减少重复。

**严重性：信息项（设计约束导致，可接受）**

---

## 4. 缺失特性

### F-1：无 `constexpr` 支持

**P0052R15 要求：** 大多数成员函数标记 `constexpr`，包括构造函数、`get()`、`get_deleter()`、`reset()`、`release()`、移动操作和 `swap()`。

**当前状态：** 所有函数均无 `constexpr`。

**影响：** 无法在常量表达式中使用，例如：
```cpp
constexpr auto make_scoped() {
    return std::unique_resource(42, [](int){}); // 当前：编译失败
}
```

对于 C++ 标准库级别的**严格符合性**，这是一个实际缺口。对于 C++23 targeting 的 backport，实现 `constexpr` 支持需要所有路径（包括 `try-catch`）满足常量求值规则，在当前的 exception-fallback 架构下有一定复杂性。

**严重性：中（C++23/26 标准要求，运行时语义不受影响）**

---

### F-2：默认构造函数缺失

**P0052R15 §[unique.res.ctor]/1：**
```
unique_resource();
```
> Effects: Value-initializes the stored resource handle and the deleter. Does not own the resource.
> Constraints: `is_default_constructible_v<R> && is_default_constructible_v<D>` is true.

当前实现显式删除了默认构造函数（`= delete` 未出现但无定义，隐式删除）。

**不确定性：** 委员会后续 paper 可能已移除默认构造函数。如果 C++26 最终标准保留了它，当前实现不符合。

**建议：** 核查 C++26 最新草案确认。若保留，需添加：
```cpp
unique_resource()
    requires (is_default_constructible_v<R> && is_default_constructible_v<D>)
    : resource_(), deleter_(), execute_on_reset_(false) {}
```

**严重性：待确认（高→无，取决于 C++26 最终草案）**

---

## 5. 测试缺口

### T-1：self-move assignment 未测试

```cpp
std::unique_resource r(42, counting_deleter{&count});
r = std::move(r);  // this == &other，应为 no-op
```

实现在 line 77 有 `if (this != &other)` 保护。此保护正确，但无测试验证。

**优先级：低（保护代码已存在，测试为安全网）**

---

### T-2：self-swap 未测试

```cpp
r.swap(r);  // 标准容器语义：self-swap 应为 no-op 或明确定义行为
```

P0052R15 对 self-swap 无明确规定。当前实现通过 `std::swap` 分别 swap 三个成员，self-swap 时 `std::swap(x, x)` 是 no-op，故行为正确。无测试。

**优先级：低**

---

### T-3：`noexcept` 规范的编译期验证缺失

当前 `test_unique_resource_api.cpp` 验证了类型特征和 API 表面，但未用 `static_assert(noexcept(...))` 验证 `noexcept` 规范本身，例如：
```cpp
// 示例：应有但缺失的测试
static_assert(noexcept(std::declval<value_resource&>().release()));
static_assert(noexcept(std::declval<value_resource&>().get()));
static_assert(noexcept(std::declval<value_resource&>().get_deleter()));
static_assert(noexcept(std::declval<value_resource&>().reset()));
```

**优先级：中（W-1 `noexcept` 偏差若修正，需要回归验证）**

---

### T-4：move assignment 部分失败状态未测试

当 resource 赋值成功但 deleter 赋值抛出时，`*this` 应处于 released 状态。P0052R15 §[unique.res.assign] 明确规范此行为。

```
// 场景：move assign，resource 拷贝成功（copy fallback），deleter 拷贝抛出
// 期望：*this 处于 released 状态（execute_on_reset_ = false）
```

当前实现的 4-way `if constexpr` 分支：
```cpp
// 当 R 和 D 均需 copy fallback 时：
resource_ = other.resource_;   // copy，可能 noexcept
deleter_ = other.deleter_;     // copy，可能抛出
execute_on_reset_ = ...;       // 在 deleter 赋值之后
```

如果 `deleter_ = other.deleter_` 抛出，`execute_on_reset_` 未被更新。此时 `*this` 的 `execute_on_reset_` 仍为之前的值（已被 reset() 设为 false — line 78 调用了 reset()）。所以理论上 `execute_on_reset_` 在 deleter 赋值时应为 false，状态已经是 released。

**逻辑验证：** line 78 `reset()` → 将 `execute_on_reset_` 设为 false。之后的赋值无论是否成功，`*this` 的 `execute_on_reset_` 始终为 false，直到 line 101 `execute_on_reset_ = std::exchange(other.execute_on_reset_, false)` 被执行。若 deleter 赋值抛出，line 101 不会执行，`execute_on_reset_` 保持 false。

**结论：** 实现行为实际上是正确的，但缺乏测试来防止未来回归。

**优先级：中**

---

## 6. 异常路径详细分析

### 6.1 `construct_deleter_from_other` — 移动失败后 deleter 调用

**位置：** `unique_resource.hpp:257-271`

```cpp
catch (...) {
    if (other.execute_on_reset_) {
        std::invoke(other.deleter_, resource_value(resource));
        other.release();
    }
    throw;
}
```

进入 catch 的条件：`!is_nothrow_move_constructible_v<D> && !is_copy_constructible_v<D>` — 即 move-only 且 move 可能抛出。

**潜在问题：** 在此路径下，`other.deleter_` 的 move 操作已经开始（并失败）。`other.deleter_` 处于 moved-from state。随后调用 `std::invoke(other.deleter_, ...)` 对 moved-from deleter 进行调用。

**P0052R15 §[unique.res.move]/2：**
> "release the resource via `std::invoke(rhs.del, get())` if `rhs.owns_resource()`"

标准要求用 `rhs.del`（即 `other.deleter_`）调用，但 move 失败后 `other.deleter_` 可能处于 partial-moved state。这在技术上要求 deleter 的 move 操作在失败时将 source 留在 *可调用* 状态——这对 deleter 类型施加了额外的（标准并未明文要求的）约束。

**实际影响：** 极低。移动可能抛出但仍可调用的 deleter（且无 copy 构造）是极端边缘场景。标准的隐式期望是 deleter 在 move 失败后仍可调用。实现符合标准措辞，该限制来自标准本身。

**结论：** 实现正确，此为标准自身的设计局限，不需修改，但值得在代码中加一行注释说明此约束。

---

### 6.2 move 前已 `release()` 的资源 — v2-r0 报告 I-2 分析

v2-r0 提出了"release() 后 move 构造异常导致资源泄漏"的问题，经独立验证后**判定此问题不成立**：

场景：`other.release()` → `other.execute_on_reset_ = false`，然后 move 构造抛出。catch 块检查 `other.execute_on_reset_ == false`，不调用 deleter。

这是**正确行为**：`release()` 表示"不再拥有资源"，move 后的新对象也不应该拥有这个被 release 的资源。标准明确："if `rhs.owns_resource()`" — release 后不 owns_resource。无资源泄漏，此乃意图行为。

---

## 7. 正确性亮点

以下问题在三份参考报告中存在争议，本审查明确确认其为**正确**：

1. **copy-fallback 选择逻辑**（构造、move 构造、move 赋值）— 实现完全符合 P0052R15 的"nothrow move, else copy"语义
2. **`reset()` catch 块中的 `std::invoke(deleter_, r)`**（line 135）— 对原始 `r` 调用 deleter 是正确的（赋值失败，`r` 仍持有原始值）
3. **`construct_deleter_from_input` 中 `execute_on_reset = false` 时不调用 deleter** — 对应 `make_unique_resource_checked` 的 invalid resource 场景，正确
4. **`make_unique_resource_checked` noexcept 不依赖 `operator==` 的 noexcept** — line 312 只使用比较结果，noexcept 规范仅包含构造，正确（`test_unique_resource_checked_noexcept.cpp` 验证通过）
5. **self-move assignment 保护**（line 77 `this != &other`）— 正确

---

## 8. 修改优先级建议

### 立即修复（影响符合性）

1. **C-1：将 `reset(RR&&)` 从 `static_assert` 改为 `requires` 约束**
   - 影响 SFINAE/concepts 调用点的正确性

### 短期改进（规范对齐）

2. **W-1：move assignment `noexcept` 使用 `resource_storage` 替代 `R`**
3. **F-2：确认 C++26 default constructor 状态；按结果添加或文档化其缺失**
4. **Q-1：删除未使用的 `construct_resource` 和 `construct_deleter` 方法**

### 中期改进（测试完善）

5. **T-3：添加 `noexcept` 规范的 `static_assert` 验证**
6. **T-4：添加 move assignment 部分失败路径的运行时测试**
7. **T-1/T-2：添加 self-move / self-swap 测试**

### 长期（标准完整符合性）

8. **F-1：实现 `constexpr` 支持**（需重构 exception cleanup 路径以符合常量求值规则）

---

## 9. 总结

```
强项：
  - 异常安全性实现完整，涵盖构造/move构造/move赋值/reset的所有异常路径
  - copy-fallback 语义正确，与 P0052R15 完全一致
  - 测试覆盖运行时核心路径、规范符合性路径和边界情况
  - 代码结构清晰，helper 方法命名有意义

弱项：
  - reset(RR&&) 的 static_assert vs requires 是一个真正的符合性缺陷
  - 有两个未被调用的死代码方法
  - 缺少 constexpr 支持（C++23/26 要求）
  - noexcept 规范未经编译期验证

最终评级：A-（生产可用，修复 C-1 后可达 A）
```

---

## 附录：与参考报告差异对照

| 争议点 | v1-r0 | v2-r0 | v3-r0 | 本报告结论 |
|--------|-------|-------|-------|------------|
| `reset` `static_assert` vs `requires` | 缺陷 | 未提 | 未提 | **确认缺陷** |
| move assign `noexcept` 使用 `R` vs `RS` | 偏差 | `operator=` 缺少 requires | 合规 | **措辞偏差，无语义影响** |
| `release()` 后 move 构造异常 | 未提 | 认为是 bug | 认为是文档建议 | **实现正确，v2-r0 结论无效** |
| `constexpr` | 未提 | 未提 | 缺失 | **确认缺失，中等严重性** |
| `construct_resource/Deleter` 死代码 | 未提 | 未提 | 未提（提到重复但误判） | **新发现：确认为未使用死代码** |
| move-only deleter 失败后 moved-from 调用 | 未提 | 认为是 bug | 文档建议 | **标准隐式约束，实现正确** |
