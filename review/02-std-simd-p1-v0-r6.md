# `std::simd` 独立审查报告 (p1-v0-r6)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/simd/**/*.cpp`、`test/simd/CMakeLists.txt`

审查基线：

- 当前独立隔离 worktree：`fix/std-simd-p1-r5` @ `0c98e9e`
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本轮重点复核条文：
  - `[simd.flags.oper]`：`https://eel.is/c++draft/simd.flags.oper`
  - `[simd.iterator]`：`https://eel.is/c++draft/simd.iterator`
  - `[simd.mask.overview]`：`https://eel.is/c++draft/simd.mask.overview`

执行方式：

- 不特别复用既有 review 结论，只以当前工作树为对象重新独立核查
- 为避免与另一条 `std::execution` 修复线互相污染，本轮只在 `fix/std-simd-p1-r5` 隔离 worktree 上审查
- 结论基于代码直读与本地最小编译探针复现

---

## 执行摘要

在 `r5` 修复之后，当前 `std::simd` 仍至少还有三组不能放过的 P1 问题：

1. `[simd.flags.oper]` `flags` 的按位组合仍是普通 `constexpr`，运行期组合被错误接受，而当前草案要求这是 `consteval` 公开契约
2. `[simd.iterator]` `simd_iterator` 仍缺少标准要求的 `operator<=>`；同时现有手写 `< <= > >=` 只比较 `index_`，没有对齐标准的比较形状
3. `[simd.mask.overview]` `basic_mask(bitset)` 构造函数仍然不是 `constexpr`，导致标准要求的编译期 round-trip 构造无法成立

这三组问题的共同点是：**现有测试主要只覆盖了“相邻能力”或“返回类型”，没有把标准公开契约本身钉住**。因此当前实现依然可能在整套测试基本维持绿色时，继续对下游暴露非标准形状。

---

## 主要问题

### I-1 `[simd.flags.oper]` `flags` 的 `operator|` 仍错误地允许运行期求值

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp:323-343`
  - `test/simd/compile/test_simd_api_memory.cpp:71-95`
  - `test/simd/runtime/test_simd_memory.cpp:220-233`
- 相关条文：
  - 当前工作草案 `[simd.flags.oper]`
  - 其中 `template<class... Other> friend consteval auto operator|(flags a, flags<Other...> b);`

**问题描述：**

当前实现中：

```cpp
template<class... Left, class... Right>
constexpr flags<Left..., Right...> operator|(flags<Left...>, flags<Right...>) noexcept {
    return {};
}
```

也就是 `flags` 的组合仍只是普通 `constexpr` 函数，而不是当前草案要求的 `consteval`。

这不是“实现细节稍松”，而是公开契约已经发生漂移。按当前草案，`flags` 组合应当是立即函数；实现不应继续接受运行期 flags 组合。当前最小探针在本地 GCC 13 上可直接通过：

```cpp
#include <simd>
auto combine(std::simd::flags<> f) {
    return f | std::simd::flag_convert;
}
```

这说明当前 backport 允许了一类原生标准接口不应允许的调用形态。

与此同时，现有测试只覆盖了：

- `flag_convert` / `flag_aligned` / `flag_overaligned` 作为常量标志被正向使用
- `flag_convert` 能打开类型改变的 load/store / gather/scatter 重载

但没有任何负向 probe 去锁住“运行期 flags 组合应当被拒绝”这一公开契约。

**影响：**

- 下游代码可以在当前 backport 上写出运行期 `flags` 组合，并成功编译
- 当未来切到原生 `std::simd` 时，这类代码会直接撞上标准接口边界
- 这属于标准公开表面的形状漂移，不是普通实现松紧问题
- 当前测试体系没有能力及时拦住这类偏移

---

### I-2 `[simd.iterator]` `simd_iterator` 仍缺少 `operator<=>`，且现有顺序比较语义不对齐标准

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/iterator.hpp:91-137`
  - `test/simd/compile/test_simd_api_core.cpp:151-174`
- 相关条文：
  - 当前工作草案 `[simd.iterator]`
  - 其中 `friend constexpr auto operator<=>(simd-iterator a, simd-iterator b);`

**问题描述：**

当前 `simd_iterator` 公开表面中没有 `operator<=>`，而是手写了：

- `operator==`
- `operator!=`
- `operator<`
- `operator<=`
- `operator>`
- `operator>=`

并且顺序比较只比较 `index_`：

```cpp
friend constexpr bool operator<(const simd_iterator& left, const simd_iterator& right) noexcept {
    return left.index_ < right.index_;
}
```

这与当前草案要求的比较形状不一致。最小编译探针在本地 GCC 13 上可直接失败：

```cpp
#include <compare>
#include <simd>
using int4 = std::simd::vec<int, 4>;
static_assert(requires(int4::iterator it) { it <=> it; });
```

失败核心就是当前类型根本没有标准要求的 `operator<=>`。

此外，现有 `< <= > >=` 只看 `index_`、不带 `value_`，其行为也不再是标准默认比较形状的等价展开。也就是说，这里不是单纯“少一个语法糖”，而是比较接口整体没有按标准方式建模。

现有测试只检查了：

- `begin/cbegin/end/cend` 返回类型
- `++/--` 的返回类型
- iterator 与 sentinel 的差值类型

并没有任何编译期 probe 去锁定：

- `iterator <=> iterator` 必须存在
- 比较族应由该标准形状导出，而不是手写另一套语义

**影响：**

- 当前 backport 缺失标准要求的 `simd_iterator` 公开成员能力
- 依赖三路比较或 concept 检查的下游代码会直接编译失败
- 即便不使用 `<=>`，当前顺序比较也不是标准定义的同一套接口形状
- 现有 compile tests 仍不足以守住 iterator 合同

---

### I-3 `[simd.mask.overview]` `basic_mask(bitset)` 构造函数仍不是 `constexpr`

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/types.hpp:46-52`
  - `test/simd/compile/test_simd_api_core.cpp:18-22`
  - `test/simd/compile/test_simd_api_core.cpp:83`
  - `test/simd/compile/test_simd_api_core.cpp:121-124`
- 相关条文：
  - 当前工作草案 `[simd.mask.overview]`
  - 其中 `template<same_as<bitset<size()>> T> constexpr basic_mask(const T& b) noexcept;`

**问题描述：**

当前实现已经把 `to_bitset()` 修到 `constexpr`，但反向构造仍停在：

```cpp
template<size_t N,
         typename enable_if<N == abi_lane_count<Abi>::value, int>::type = 0>
basic_mask(const bitset<N>& bits) noexcept : data_{} {
    ...
}
```

也就是仍然缺少 `constexpr`。

本地最小编译探针可直接复现：

```cpp
#include <bitset>
#include <simd>
using mask4 = std::simd::mask<int, 4>;
constexpr bool check() {
    constexpr std::bitset<4> bits(0b0101u);
    constexpr mask4 value(bits);
    return value[0] && !value[1] && value[2] && !value[3];
}
static_assert(check());
```

失败核心是：调用了非 `constexpr` 的 `basic_mask(const bitset<N>&)`。

这里的问题很像上一轮 `to_bitset()`：表面上 `bitset` round-trip 已经“差一点就完整”，但真正的标准契约仍然没有闭环。现有测试只钉住了：

- `to_bitset()` 返回 `std::bitset<4>`
- `to_bitset()` 自身可常量求值

却没有任何测试去锁定：

- `basic_mask(bitset)` 反向构造本身也必须可常量求值

**影响：**

- `bitset -> basic_mask` 的标准编译期构造路径仍然缺口存在
- 下游若把 `std::bitset` 作为编译期掩码源，会在当前 backport 上直接退化失败
- 这类 round-trip 契约缺口很容易被现有“只测单向”的测试组织方式掩盖

---

## 结论

按当前工作草案 `[simd]` 与当前 `fix/std-simd-p1-r5` 工作树独立复核，当前 `std::simd` 至少仍有以下三组 P1 问题未收口：

1. `[simd.flags.oper]` `flags` 的 `operator|` 仍不是 `consteval`，运行期组合被错误接受
2. `[simd.iterator]` `simd_iterator` 仍缺少 `operator<=>`，且现有顺序比较接口没有按标准形状建模
3. `[simd.mask.overview]` `basic_mask(bitset)` 构造函数仍不是 `constexpr`

这三组问题再次说明：当前 `simd` 的主要剩余风险已经不只是“某个算法写错”，而是**一些标准公开契约仍未被源码和测试一起完整锁住**。下一轮如果要修，至少需要同时做两件事：

- 把上述公开契约修到与当前草案一致
- 把对应点补成长期 compile probe / negative probe / constexpr probe，避免再次靠临时探针才发现问题
