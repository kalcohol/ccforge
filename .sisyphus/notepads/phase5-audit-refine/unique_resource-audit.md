# std::unique_resource 审计报告

**审计日期：** 2026-03-22  
**标准参考：** P0052R10 (2019) / ISO/IEC TS 19568:2024  
**审计范围：** CC Forge backport/cpp26/unique_resource.hpp

---

## 🔴 CRITICAL FINDING

**`std::unique_resource` 不在 C++26 标准中！**

经过全面搜索：
- C++26 working draft (eel.is)
- WG21 2024-2025 论文列表
- cppreference C++26 特性列表
- Feature test macros

**结论：** P0052 (unique_resource) **未被接受进入 C++26**。它仍然在 **Library Fundamentals TS v3** 中作为 `std::experimental::unique_resource`。

---

## 规范基准

### 当前权威来源

1. **ISO/IEC TS 19568:2024** (Library Fundamentals v3) - 规范性文档
2. **P0052R10 (2019-02-19)** - 最新可用提案
3. **cppreference** - 仍标记为 `std::experimental::unique_resource`

### 重要事实

- ❌ 不在 C++26 working draft
- ❌ 无 feature test macro `__cpp_lib_unique_resource`
- ✅ 仅存在于 TS v3 作为 `std::experimental::unique_resource`
- ✅ 最新提案是 P0052R10 (2019)，未找到更新版本

---

## 标准 API 清单（来自 P0052R10）

### 类模板声明
```cpp
template<class R, class D>
class unique_resource;
```

### 成员函数

#### 构造函数
- `unique_resource()` - 默认构造（R10 新增）
- `unique_resource(RR&& r, DD&& d)` - 主构造函数
- `unique_resource(unique_resource&&)` - 移动构造

#### 析构函数
- `~unique_resource()` - 条件调用 deleter

#### 赋值
- `operator=(unique_resource&&)` - 移动赋值

#### 修改器
- `void reset()` - 重置资源
- `void reset(RR&& r)` - 重置为新资源
- `const R& release()` - 释放所有权

#### 观察器
- `const R& get() const` - 获取资源
- `const D& get_deleter() const` - 获取 deleter
- `operator*()` - 解引用（条件可用）
- `operator->()` - 成员访问（条件可用）

### 工厂函数
- `make_unique_resource(r, d)` - 创建 unique_resource
- `make_unique_resource_checked(r, invalid, d)` - 带无效值检查

### 推导指引
- `unique_resource(R, D) -> unique_resource<R, D>`

---

## CC Forge 实现状态

需要验证 CC Forge 的实现：
1. 是否实现了所有 P0052R10 要求的 API
2. 是否正确标记为 `std::` 而非 `std::experimental::`
3. 异常安全保证是否符合规范

---

## 建议

### 优先级 1：更正文档

README 中将 unique_resource 标记为"C++26 backport"是**不准确的**。应该：
- 明确说明这是 TS v3 的实现
- 或标记为"预期的 C++2x 特性"
- 或移除 C++26 的声称

### 优先级 2：验证实现完整性

对照 P0052R10 检查：
- 所有成员函数是否存在
- 异常安全保证是否正确
- 工厂函数是否完整

### 优先级 3：命名空间决策

决定是使用：
- `std::unique_resource` (当前做法，假设未来会进标准)
- `std::experimental::unique_resource` (符合当前 TS)

---

## 结论

**完整性评分：** 待验证

CC Forge 将 unique_resource 作为"C++26 backport"是基于错误假设。该特性：
- 未进入 C++26
- 仅在 TS v3 中
- 未来标准化状态不明

**建议行动：**
1. 更正 README 中的描述
2. 验证实现是否符合 P0052R10
3. 决定命名空间策略
