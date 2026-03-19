# `std::unique_resource` 实现与测试审查报告 (v0-r0)

审查对象：
- 实现文件：`backport/cpp26/unique_resource.hpp`
- 测试文件：`test/test_unique_resource.cpp`、`test/test_unique_resource_api.cpp`、`test/test_unique_resource_wording.cpp`、`test/test_unique_resource_exception.cpp`、`test/test_unique_resource_checked.cpp`

参考规范：P0052R15 (C++26 `std::unique_resource`)

---

## 第一部分：实现问题

### I-1 使用 C++17 `enable_if_t` 而非 C++23 concepts/requires

- 严重程度：低
- 位置：第 59、70-77、139-144、174、179 行

**问题描述：** 所有模板约束均使用 `std::enable_if_t<..., int> = 0` 形式的 SFINAE，而非 C++20/C++23 的 `requires` 子句或 concepts。

**规范要求：** P0052R15 的措辞使用 *Constraints:* 段落描述约束条件，现代实现应使用 `requires` 子句表达，语义更清晰，错误信息更友好。

**当前实现：** 例如第 70-77 行的双参数构造函数：
```cpp
template<
    class RR,
    class DD,
    enable_if_t<
        is_constructible_v<resource_storage, RR> &&
        is_constructible_v<D, DD> && ...,
        int> = 0>
unique_resource(RR&& r, DD&& d)
```
应改为 `requires` 子句形式，项目已要求 C++23 标准（`CMAKE_CXX_STANDARD=23`），完全可以使用。

---

### I-2 提供了规范中不存在的默认构造函数

- 严重程度：中
- 位置：第 56-65 行

**问题描述：** 实现提供了一个默认构造函数，当 `R` 和 `D` 均可默认构造时启用，且将 `execute_on_reset_` 设为 `false`。

**规范要求：** P0052R15 最终版中 `unique_resource` 没有默认构造函数。类的构造仅通过双参数构造函数、移动构造函数、`make_unique_resource_checked` 完成。

**当前实现：**
```cpp
template<
    class RR = R,
    class DD = D,
    enable_if_t<is_default_constructible_v<RR> && is_default_constructible_v<DD>, int> = 0>
unique_resource()
```
这是一个非标准扩展，可能导致用户代码依赖此构造函数后在标准实现上编译失败。

---

### I-3 提供了规范中不存在的 `make_unique_resource`

- 严重程度：中
- 位置：第 36-38 行（前向声明）、第 325-332 行（定义）

**问题描述：** 实现提供了 `make_unique_resource(R&&, D&&)` 工厂函数。

**规范要求：** P0052R15 最终版仅保留了 `make_unique_resource_checked(R&&, const S&, D&&)`。早期修订版（如 R6）中存在 `make_unique_resource`，但在最终版中被移除，因为双参数构造函数 + CTAD 已足够。

**当前实现：** 第 325-332 行定义了该函数，且测试中大量使用（如 `test_unique_resource.cpp` 第 20、29、39 行等）。此函数的存在会误导用户认为它是标准 API 的一部分。

---

### I-4 移动赋值 noexcept 使用 `resource_storage` 而非 `R`

- 严重程度：低
- 位置：第 95-96 行

**问题描述：** 移动赋值运算符的 `noexcept` 规范使用了内部类型 `resource_storage` 而非模板参数 `R`。

**规范要求：** P0052R15 规定移动赋值运算符的 noexcept 条件应基于 `R` 和 `D` 的移动赋值特性。

**当前实现：**
```cpp
unique_resource& operator=(unique_resource&& other) noexcept(
    is_nothrow_move_assignable_v<resource_storage> &&
    is_nothrow_move_assignable_v<D>)
```
当 `R` 是引用类型时，`resource_storage` 为 `reference_wrapper<remove_reference_t<R>>`，其 noexcept 特性可能与 `R` 本身不同。虽然 `reference_wrapper` 的赋值总是 noexcept，但语义上应与规范一致。

---

### I-5 移动构造函数 deleter 构造异常时清理有条件分支

- 严重程度：高
- 位置：第 268-286 行

**问题描述：** `construct_deleter_from_other` 在 deleter 构造抛出异常时，仅在资源是通过 move 构造（而非 copy 构造）的情况下才清理资源。

**规范要求：** P0052R15 [res.unique.ctor] 规定：移动构造函数中，如果 deleter 的构造抛出异常，应无条件使用 `other` 的 deleter 对已构造的资源调用清理，然后调用 `other.release()`。无论资源是 move 还是 copy 构造的，都必须清理。

**当前实现：**
```cpp
} catch (...) {
    if constexpr (is_nothrow_move_constructible_v<resource_storage> ||
                  !is_copy_constructible_v<resource_storage>) {
        if (other.execute_on_reset_) {
            std::invoke(other.deleter_, resource_value(resource));
            other.release();
        }
    }
    throw;
}
```
当资源通过 copy 构造（即 `is_nothrow_move_constructible_v<resource_storage>` 为 `false` 且 `is_copy_constructible_v<resource_storage>` 为 `true`）时，`if constexpr` 分支不会执行，导致资源泄漏。应无条件执行清理逻辑。

---

### I-6 `reset(RR&&)` 添加了规范未要求的 SFINAE 约束

- 严重程度：低
- 位置：第 139-144 行

**问题描述：** `reset(RR&&)` 模板添加了 `enable_if_t` 约束，要求 `resource_storage` 可从 `RR` 赋值或可从 `const remove_reference_t<RR>&` 赋值。

**规范要求：** P0052R15 中 `reset(RR&& r)` 的约束仅为 *Mandates:* 条件（硬错误），而非 SFINAE 友好的约束。规范使用 `is_assignable_v<R&, RR>` 作为 Mandates 条件，不参与重载决议。

**当前实现：** 将 Mandates 条件实现为 SFINAE 约束，虽然不会导致功能错误，但改变了重载决议行为——当条件不满足时，标准实现会产生硬编译错误，而当前实现会静默地从候选集中移除该函数。

---

### I-7 `swap` 缺少 `is_swappable_v` 约束

- 严重程度：低
- 位置：第 184-189 行

**问题描述：** 成员函数 `swap` 和自由函数 `swap`（第 316-320 行）均未检查 `R` 和 `D` 是否可交换。

**规范要求：** P0052R15 规定 `swap` 应具有 `is_swappable_v<R> && is_swappable_v<D>` 约束（对成员 swap 为 *Mandates*，对自由函数 swap 为参与重载决议的约束）。

**当前实现：** 仅在 `noexcept` 规范中使用了 `is_nothrow_swappable_v`，但未将 `is_swappable_v` 作为约束条件。当 `R` 或 `D` 不可交换时，会产生不友好的模板实例化错误而非清晰的约束失败。

---

### I-8 异常处理中多余的 `is_invocable_v` 检查可能导致资源泄漏

- 严重程度：中
- 位置：第 234 行、第 249 行

**问题描述：** `construct_resource_from_input`（第 234 行）和 `construct_deleter_from_input`（第 249 行）在异常处理中使用 `if constexpr (is_invocable_v<...>)` 包裹 deleter 调用。

**规范要求：** P0052R15 规定构造函数中资源构造或 deleter 构造失败时，应无条件调用 `d(r)` 进行清理。deleter 必须能够对资源调用是类的基本前提条件（*Preconditions*），不需要额外检查。

**当前实现：**
```cpp
} catch (...) {
    if constexpr (is_invocable_v<DD&, decltype((r))>) {
        if (execute_on_reset) {
            std::invoke(d, r);
        }
    }
    throw;
}
```
如果 `is_invocable_v` 为 `false`（例如由于 const 限定不匹配等边缘情况），资源将泄漏。规范假设 deleter 总是可调用的，此检查是多余且危险的。

---

### I-9 注入 `std` 命名空间

- 严重程度：低
- 位置：第 29 行

**问题描述：** 实现将 `unique_resource` 及相关函数直接定义在 `namespace std` 中。

**规范要求：** C++ 标准 [namespace.std] 规定，向 `namespace std` 添加声明或定义是未定义行为（除非是模板特化等特定情况）。

**当前实现：** 作为 backport 库，注入 `std` 命名空间是为了提供透明的替代实现，使用户代码无需修改即可使用。这是 backport 库的已知权衡，但应在文档中明确说明此行为及其风险。

---

## 第二部分：测试问题

### T-1 缺少 `release()` 后析构不调用 deleter 的测试

- 严重程度：高
- 涉及文件：`test/test_unique_resource.cpp`

**问题描述：** 现有测试未验证调用 `release()` 后，对象析构时不会调用 deleter。

**期望行为：** 应有测试创建 `unique_resource`，调用 `release()`，然后让对象析构，验证 deleter 调用次数为 0。

**当前测试：** `test_unique_resource.cpp` 中有 `BasicUsage`、`ResetRunsDeleterOnce`、`MoveTransfersOwnership`、`SwapExchangesResourceAndDeleter` 四个测试，均未涉及 `release()` 的行为验证。

---

### T-2 缺少移动赋值运算符的基本运行时测试

- 严重程度：高
- 涉及文件：`test/test_unique_resource.cpp`

**问题描述：** 基本运行时测试中没有移动赋值运算符（`operator=(unique_resource&&)`）的测试。

**期望行为：** 应有测试验证：(1) 移动赋值后目标持有源的资源；(2) 源被释放；(3) 目标原有资源的 deleter 被调用；(4) 最终析构时仅调用一次 deleter。

**当前测试：** `test_unique_resource_wording.cpp` 中有 `MoveAssignmentCopiesResourceWhenMoveMayThrow` 和 `MoveAssignmentCopiesDeleterWhenMoveMayThrow`，但这些仅测试 copy fallback 语义，不是基本的移动赋值正确性测试。

---

### T-3 缺少 `reset(R)` 值语义的运行时测试

- 严重程度：中
- 涉及文件：`test/test_unique_resource.cpp`

**问题描述：** 缺少对 `reset(R)` 基本值语义的运行时测试。

**期望行为：** 应有测试验证：(1) `reset(new_value)` 先清理旧资源；(2) 之后持有新资源；(3) 析构时清理新资源。

**当前测试：** `test_unique_resource_wording.cpp` 第 215-231 行有 `ResetRebindsReferenceResource` 测试引用语义的 reset，但缺少值类型资源的基本 `reset(R)` 测试。

---

### T-4 缺少 `operator*` 和 `operator->` 的运行时测试

- 严重程度：中
- 涉及文件：`test/test_unique_resource.cpp`

**问题描述：** 缺少 `operator*` 和 `operator->` 的运行时行为测试。

**期望行为：** 应有测试验证指针类型资源的 `operator*` 返回正确的解引用值，`operator->` 可用于访问成员。

**当前测试：** `test_unique_resource_api.cpp` 第 58-62 行仅通过编译探测验证这两个运算符的存在性，未验证运行时行为。

---

### T-5 编译探测使用 C++17 风格 SFINAE 而非 C++23 requires

- 严重程度：低
- 涉及文件：`test/test_unique_resource_api.cpp` 第 17-33 行

**问题描述：** 编译探测使用 `void_t` + 偏特化的 C++17 风格 SFINAE 检测模式。

**期望行为：** 项目要求 C++23，应使用 `requires` 表达式或 concepts 进行编译探测，代码更简洁易读。

**当前测试：**
```cpp
template<class T, class = void>
struct has_reset_with_value : std::false_type {};

template<class T>
struct has_reset_with_value<T, std::void_t<decltype(std::declval<T&>().reset(0))>> : std::true_type {};
```

---

### T-6 编译探测使用 `::value` 而非 `_v` 变量模板

- 严重程度：低
- 涉及文件：`test/test_unique_resource_api.cpp` 第 37-56 行

**问题描述：** `static_assert` 中使用 `std::is_xxx<T>::value` 而非 `std::is_xxx_v<T>`。

**期望行为：** C++17 起即可使用 `_v` 变量模板后缀，C++23 项目应统一使用简洁形式。

**当前测试：**
```cpp
static_assert(std::is_default_constructible<value_resource>::value, ...);
static_assert(std::is_same<void, decltype(...)>::value, ...);
```

---

### T-7 缺少推导指引（CTAD）的测试

- 严重程度：中
- 涉及文件：所有测试文件

**问题描述：** 实现在第 322-323 行提供了 CTAD 推导指引 `unique_resource(R, D) -> unique_resource<R, D>`，但没有任何测试验证其正确性。

**期望行为：** 应有测试验证 `std::unique_resource ur(resource, deleter);` 能正确推导模板参数，且推导结果类型正确。

**当前测试：** 所有测试均显式指定模板参数或使用 `auto` + 工厂函数，未直接测试 CTAD。

---

### T-8 缺少 ADL swap 自由函数的测试

- 严重程度：低
- 涉及文件：`test/test_unique_resource.cpp`

**问题描述：** 现有 swap 测试仅测试成员函数 `swap`，未测试通过 ADL 找到的自由函数 `swap`。

**期望行为：** 应有测试验证 `using std::swap; swap(a, b);` 能正确调用自由函数版本。

**当前测试：** `test/test_unique_resource.cpp` 第 46-64 行的 `SwapExchangesResourceAndDeleter` 仅调用 `resource1.swap(resource2)`。

---

### T-9 `MoveConstructorCopiesResourceWhenMoveMayThrow` 断言硬编码 copy 次数

- 严重程度：中
- 涉及文件：`test/test_unique_resource_wording.cpp` 第 133 行

**问题描述：** 测试断言 `copy_construct_count == 2`，与实现内部的 copy 次数耦合。

**期望行为：** 规范仅要求当 move 可能抛出异常时使用 copy 构造，不规定具体 copy 次数。测试应验证 `copy_construct_count >= 1` 且 `move_construct_count == 0`，而非精确匹配实现细节。

**当前测试：**
```cpp
EXPECT_EQ(resource_copy_fallback::copy_construct_count, 2);
```
如果实现优化减少了一次 copy（例如通过 RVO），此测试将失败，但实现仍然符合规范。

---

### T-10 缺少 `reset(R)` 赋值失败时的异常安全测试

- 严重程度：高
- 涉及文件：`test/test_unique_resource_exception.cpp`

**问题描述：** 异常测试文件仅测试构造函数和移动构造的异常场景，缺少 `reset(R)` 赋值失败时的异常安全测试。

**期望行为：** 规范要求 `reset(RR&& r)` 中，如果资源赋值抛出异常，应调用 `d(r)` 清理新资源并重新抛出。应有测试验证：(1) 赋值抛出异常时新资源被清理；(2) 旧资源已在 `reset()` 中被清理；(3) 异常被正确传播。

**当前测试：** `test_unique_resource_exception.cpp` 仅包含 `ConstructorFailureCleansUpResource` 和 `MoveFailureCleansUpTransferredResource` 两个测试。

---

### T-11 编译探测未验证 `unique_resource` 不可拷贝

- 严重程度：中
- 涉及文件：`test/test_unique_resource_api.cpp`

**问题描述：** 编译探测未验证 `unique_resource` 的拷贝构造和拷贝赋值被删除。

**期望行为：** 应有 `static_assert(!is_copy_constructible_v<...>)` 和 `static_assert(!is_copy_assignable_v<...>)` 验证不可拷贝语义。

**当前测试：** 实现中第 85-86 行确实 `= delete` 了拷贝操作，但测试未验证这一关键约束。

---

### T-12 `make_unique_resource_checked` 缺少无效值后重新激活的测试

- 严重程度：中
- 涉及文件：`test/test_unique_resource_checked.cpp`

**问题描述：** 测试验证了无效值不调用 deleter，但未测试对无效值对象调用 `reset(new_valid_value)` 重新激活后的行为。

**期望行为：** 应有测试验证：(1) 以无效值创建的 `unique_resource`（`execute_on_reset_ == false`）；(2) 调用 `reset(valid_value)` 后变为活跃状态；(3) 析构时调用 deleter 清理新值。

**当前测试：** `test_unique_resource_checked.cpp` 包含三个测试，均未涉及 `reset` 重新激活场景。

---

### T-13 编译探测未通过 `forge::forge` 目标验证注入机制

- 严重程度：中
- 涉及文件：`test/test_unique_resource_api.cpp`、`test/CMakeLists.txt`

**问题描述：** 编译探测直接 `#include "../backport/cpp26/unique_resource.hpp"` 引用实现文件，未通过 `#include <memory>` + `forge::forge` 目标验证 backport 注入机制。

**期望行为：** 作为 backport 库，核心价值在于透明注入。应有编译探测通过标准头文件路径 `#include <memory>` 验证 `std::unique_resource` 可用，确保 `forge.cmake` 的 include path 注入机制正常工作。

**当前测试：** `test_unique_resource_include.cpp` 可能涉及此场景，但 API 编译探测本身未使用注入路径。

---

## 第三部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 高       | 1 (I-5)  | 3 (T-1, T-2, T-10) | 4 |
| 中       | 3 (I-2, I-3, I-8) | 6 (T-3, T-4, T-7, T-9, T-11, T-12, T-13) | 9 |
| 低       | 5 (I-1, I-4, I-6, I-7, I-9) | 4 (T-5, T-6, T-8) | 9 |
| 合计     | 9        | 13       | 22   |

### 优先修复建议

1. **最高优先级 — I-5：** 移动构造函数 deleter 异常时的清理逻辑存在条件分支，可能导致资源泄漏。应移除 `if constexpr` 条件，无条件执行清理。这是唯一可能导致运行时资源泄漏的实现缺陷。

2. **高优先级 — T-1、T-2、T-10：** 补充 `release()` 后析构、移动赋值运算符、`reset(R)` 异常安全的运行时测试。这些是核心 API 的基本正确性验证，缺失会导致回归风险。

3. **中优先级 — I-2、I-3：** 移除非标准的默认构造函数和 `make_unique_resource`，或至少标记为扩展。这些偏离规范的 API 会误导用户。

4. **中优先级 — I-8：** 移除异常处理中的 `is_invocable_v` 检查，改为无条件调用 deleter。

5. **低优先级：** I-1、I-4、I-6、I-7、I-9 及 T-5、T-6、T-8 可在后续迭代中逐步改进。
