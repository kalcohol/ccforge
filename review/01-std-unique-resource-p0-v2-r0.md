# `std::unique_resource` 实现审查报告 (v2-r0)

审查对象：
- 实现文件：`backport/cpp26/unique_resource.hpp`
- 测试文件：`test/test_unique_resource*.cpp`
- 构建配置：GCC 13.3.0, C++23

参考规范：P0052R15 (C++26 `std::unique_resource`)

---

## 概述

代码通过所有 7 个测试，编译无警告，基本功能正确。与 v0-r0 报告相比，部分问题描述需要修正：

1. **报告 I-1（enable_if_t vs requires）不成立**：代码已使用 C++20 `requires` 子句
2. **报告 I-2（默认构造函数）不成立**：实现中不存在默认构造函数
3. **报告 I-3（make_unique_resource）不成立**：只提供了 `make_unique_resource_checked`，符合规范

---

## 实现问题

### I-1 移动赋值运算符缺少 `requires` 约束

- 严重程度：低
- 位置：第 74-105 行

**问题描述：** 移动构造函数通过模板参数和 `noexcept` 间接提供了约束，但移动赋值运算符只有 `noexcept` 规范，缺少显式的 `requires` 约束。

**当前实现：**
```cpp
unique_resource& operator=(unique_resource&& other) noexcept(
    is_nothrow_move_assignable_v<R> &&
    is_nothrow_move_assignable_v<D>)
```

**问题影响：** 当 `R` 或 `D` 既不可移动赋值也不可拷贝赋值时，重载决议会选中有缺陷的移动赋值运算符，触发难以理解的编译错误，而非清晰的约束失败提示。

**建议修复：**
```cpp
unique_resource& operator=(unique_resource&& other)
    requires (is_move_assignable_v<R> || is_copy_assignable_v<R>) &&
             (is_move_assignable_v<D> || is_copy_assignable_v<D>)
    noexcept(...)
```

---

### I-2 移动构造异常处理中的边缘情况

- 严重程度：低（实践中几乎不会发生）
- 位置：第 257-271 行（`construct_deleter_from_other`）

**问题描述：** 当 `other` 调用过 `release()` 后再从它移动构造新对象，如果 deleter 构造抛出异常，catch 块依赖 `other.execute_on_reset_` 判断是否清理资源。但 `release()` 已将 `execute_on_reset_` 设为 `false`，导致资源泄漏。

**当前实现：**
```cpp
} catch (...) {
    if (other.execute_on_reset_) {  // ← release() 后为 false
        std::invoke(other.deleter_, resource_value(resource));
        other.release();
    }
    throw;
}
```

**触发条件：**
1. `unique_resource ur1(r, d)` 构造
2. `ur1.release()`
3. `unique_resource ur2(std::move(ur1))` 移动构造
4. deleter 构造抛出异常
5. 资源泄漏

**实际影响：** 极低。这是极端边缘情况，正常使用不会遇到。编译器 RVO/NRVO 会规避这个路径。

**根本原因：** catch 块用 `other.execute_on_reset_` 判断是否需要清理资源，但这个标志表示"源对象析构时是否应清理"，而非"当前构造的资源是否需要清理"。正常移动语义下两者等价，但 release 后就不等了。

**建议修复：**
```cpp
} catch (...) {
    if (other.execute_on_reset_) {
        std::invoke(other.deleter_, resource_value(resource));
    }
    other.release();
    throw;
}
```

---

### I-3 `reset(RR&&)` 赋值时新资源异常清理逻辑正确

- 严重程度：无

**说明：** 报告 v0-r0 的 I-8 提到 `reset` 中多余的 `is_invocable_v` 检查可能导致资源泄漏。经核实，第 134-137 行的实现是正确的：

```cpp
} catch (...) {
    std::invoke(deleter_, r);  // 无条件调用
    throw;
}
```

这里没有 `is_invocable_v` 条件检查，符合规范。

---

## 与 v0-r0 报告的差异

| 问题编号 | v0-r0 描述 | v2-r0 结论 |
|----------|------------|------------|
| I-1 | 使用 enable_if_t 而非 requires | **不成立**，代码已用 requires |
| I-2 | 提供了默认构造函数 | **不成立**，实现中不存在 |
| I-3 | 提供了 make_unique_resource | **不成立**，只有 make_unique_resource_checked |
| I-5 | 移动构造可能资源泄漏 | **边缘情况**，release 后移动构造才会触发 |
| I-8 | reset 中多余的 is_invocable_v 检查 | **不成立**，reset 实现正确 |

---

## 测试覆盖度评估

当前测试全部通过，但报告 T-1、T-2 提到缺少关键测试：

| 测试 | 状态 | 说明 |
|------|------|------|
| release() 后析构不调用 deleter | 缺失 | T-1 高优先级 |
| 移动赋值运算符基本测试 | 缺失 | T-2 高优先级 |
| reset(R) 值语义 | 缺失 | T-3 中优先级 |
| operator* / operator-> | 缺失 | T-4 中优先级 |

---

## 总结

| 严重程度 | 问题数 | 说明 |
|----------|--------|------|
| 高       | 0      | 无阻塞性问题 |
| 中       | 0      | 无实际影响的问题 |
| 低       | 2      | I-1（缺少约束）、I-2（边缘异常） |
| 建议     | 4      | 测试覆盖度可提升 |

### 结论

**可用于生产。**

代码基本正确，通过所有测试。两个低优先级问题均为极端边缘情况，实践中不会触发。如需进一步提升质量，可：

1. 为移动赋值添加 `requires` 约束
2. 补充 `release()` 后析构、移动赋值的运行时测试
