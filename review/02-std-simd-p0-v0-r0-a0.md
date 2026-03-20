# `std::simd` 审查答复 (v0-r0-a0)

对应报告：`report/02-std-simd-v0-r0.md`

本答复基于当前工作区代码状态（截至 2026-03-20）。其中多数条目已在本轮直接落地到实现与测试；仍有少数条目作为后续迭代项保留。

## 先确认本项目的判断原则

### 原则 1：以未来标准库无感切换为最高目标
Forge 的目标是让下游始终使用标准入口（例如 `#include <simd>`），并在未来标准库原生提供后做到“零改动切换”。因此我们优先保证：
- API 形状与命名尽量贴近标准
- 约束/重载参与规则尽量贴近标准
- 行为语义与失败路径尽量贴近标准

### 原则 2：非标准扩展不是中性选择
如果一个构造/函数在标准中不存在，即便“更方便”，也会形成未来不兼容依赖。本轮对 `initializer_list` 的处理，会同时考虑：
- 避免“静默截断/填充”掩盖错误
- 尽量降低额外 API 的长期兼容性风险

### 原则 3：测试必须覆盖“标准使用路径”
除运行时语义外，必须覆盖：
- 通过 `#include <simd>` 的注入路径
- 公开 API 的可编译性（compile probe）

---

## 对本次 review 的总体判断
结论：`report/02-std-simd-v0-r0.md` 对缺口的识别基本准确，且严重程度排序合理（尤其 I-1/I-2 属于核心能力缺失）。

本轮已完成的主要收敛点：
- 补齐 `compress/expand`（I-1）与对应测试（T-1）
- 补齐 gather/scatter（I-2）与对应测试（T-2）
- 补齐 `iota`（I-3）
- 补齐 `basic_vec` 的 `++/--` 与 `!`（I-4/I-5）
- 修正 broadcast 构造 `explicit`（I-7）并补编译探测（T-10）
- 处理 `initializer_list` 构造的“静默截断”问题（I-6）
- 扩充测试覆盖：位运算（T-4）、`select`（T-5）、`minmax`（T-7）、`flag_convert` 的 type-changing load（T-12）、compile-probe 覆盖补齐（T-14 部分）

仍保留为后续迭代项：
- I-8：从 `enable_if` 迁移到 `requires`（属于机械性改造，但改动面较大）
- I-10：`begin()` 的非常量重载是否需要收敛（目前语义仍为只读值迭代）
- T-8/T-9/T-11/T-13：部分测试补强项

---

## 对实现问题的逐条答复

### I-1 缺少 `compress` 和 `expand`
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 中新增 `std::simd::compress/expand`
- 测试：[`test/test_simd.cpp`](/home/i/projects/forward/ccforge/test/test_simd.cpp) 已新增运行时覆盖

### I-2 缺少 gather/scatter 内存操作
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 中新增：
  - `partial_gather_from` / `unchecked_gather_from`
  - `partial_scatter_to` / `unchecked_scatter_to`
- 测试：[`test/test_simd_memory.cpp`](/home/i/projects/forward/ccforge/test/test_simd_memory.cpp) 已新增 gather/scatter（含 mask/`flag_convert`）覆盖

### I-3 缺少 `iota`
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 中新增 `std::simd::iota<V>(start)`
- 测试：[`test/test_simd.cpp`](/home/i/projects/forward/ccforge/test/test_simd.cpp) 已新增运行时覆盖；[`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp) 补了 compile-probe

### I-4 缺少 `basic_vec` 的 `operator++` 和 `operator--`
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 中为 `basic_vec` 新增前缀/后缀 `++/--`
- 测试：[`test/test_simd_operators.cpp`](/home/i/projects/forward/ccforge/test/test_simd_operators.cpp) 已新增运行时覆盖；[`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp) 补了 compile-probe

### I-5 缺少 `basic_vec` 的 `operator!`
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 中为 `basic_vec` 新增 `operator!`，返回 `mask_type`
- 测试：[`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp) 已新增 `decltype(!vec)` 的编译探测

### I-6 `initializer_list` 构造函数静默截断
结论：接受，已修复“静默截断/填充”这一风险点。
- 处理方式：移除 `initializer_list` 构造，并改为“参数个数必须精确匹配 lane 数”的构造形式，避免超长截断或不足补零的静默行为。
- 说明：这一步优先消除了“静默错误”风险；关于“是否应继续暴露任何非标准便捷构造”（原则 2）仍需在后续迭代里进一步收敛与定规。

### I-7 `broadcast` 构造函数被标记为 `explicit`
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 的同类型 broadcast 构造不再 `explicit`
- 测试：[`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp) 增加 `std::is_convertible<int, int4>` 的编译探测（对应 T-10）

### I-8 使用 `enable_if_t` 而非 `requires`
结论：接受，但本轮暂不改（保留为后续机械性重构）。
- 原因：改动面大且容易引入边缘重载参与变化；本轮优先补齐核心缺口（I-1/I-2/I-3/I-4/I-7）并提升测试覆盖。
- 后续方向：优先从“公开入口模板”（构造、`permute/chunk/cat`、load/store、gather/scatter）开始迁移到 `requires`，并配合 compile-probe 防回归。

### I-9 数学函数缺少 `constexpr`
结论：接受，已修复。
- 处理方式：将报告点名的数学函数向量版本标记为 `constexpr`（依赖 C++23 `constexpr <cmath>` 的实现支持）。
- 说明：若未来需要兼容不具备 `constexpr <cmath>` 的标准库，实现上会再引入条件化策略（不在本轮展开）。

### I-10 `end()` 返回 `default_sentinel_t` 但存在非常量 `begin()`
结论：认可这是“API 可读性/误导风险”的观察项，但本轮不改。
- 当前状态：迭代器解引用为值语义，不存在通过迭代器写 lane 的通道；因此不会产生“看似可写但写不进去”的 UB，只是可能误导读者。
- 后续：若要更贴近标准表面，可考虑让非常量对象的 `begin()` 也返回只读迭代器类型或移除非常量重载，但这需要同步调整 compile-probe 与示例表面。

### I-11 `select` 缺少标量 `select(bool, ...)` 重载
结论：接受，已修复。
- 实现：[`backport/cpp26/simd.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/simd.hpp) 新增 `select(bool, T, U)`
- 测试：[`test/test_simd_math.cpp`](/home/i/projects/forward/ccforge/test/test_simd_math.cpp) 增加运行时覆盖；[`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp) 增加 compile-probe

### I-12 `forge.cmake` 检测探针使用 API 风险观察
结论：同意为低风险观察项，本轮不改。
- 现状：探针使用 `#include <simd>` + `std::simd::vec<float>` 与 `reduce/reduce_count`，作为“是否存在原生 std::simd”的编译检测入口。

---

## 对测试问题的逐条答复

### T-1 缺少 `compress/expand` 测试
结论：接受，已补齐。见 [`test/test_simd.cpp`](/home/i/projects/forward/ccforge/test/test_simd.cpp)。

### T-2 缺少 gather/scatter 测试
结论：接受，已补齐。见 [`test/test_simd_memory.cpp`](/home/i/projects/forward/ccforge/test/test_simd_memory.cpp)。

### T-3 缺少 `operator++/--` 测试
结论：接受，已补齐。见 [`test/test_simd_operators.cpp`](/home/i/projects/forward/ccforge/test/test_simd_operators.cpp)。

### T-4 缺少位运算符运行时测试
结论：接受，已补齐。见 [`test/test_simd_operators.cpp`](/home/i/projects/forward/ccforge/test/test_simd_operators.cpp)。

### T-5 缺少 `select` 运行时测试
结论：接受，已补齐。见 [`test/test_simd_math.cpp`](/home/i/projects/forward/ccforge/test/test_simd_math.cpp)。

### T-6 chunk/cat/permute 测试被宏守卫跳过
结论：本条属于信息性说明；当前 CMake 已启用对应宏，测试未被跳过，无需改动。

### T-7 缺少 `minmax` 运行时测试
结论：接受，已补齐。见 [`test/test_simd_math.cpp`](/home/i/projects/forward/ccforge/test/test_simd_math.cpp)。

### T-8 缺少浮点除法边界测试
结论：接受，但本轮未补（后续补强项）。

### T-9 缺少不同 `value_type` 的系统性覆盖
结论：接受，但本轮未系统补齐（后续补强项）。

### T-10 缺少 broadcast 隐式转换的测试
结论：接受，已补齐（compile-probe）。见 [`test/test_simd_api.cpp`](/home/i/projects/forward/ccforge/test/test_simd_api.cpp)。

### T-11 `unchecked_load/store` 的 flags 覆盖
结论：同意可补强，但本轮未扩充（后续补强项）。

### T-12 缺少 `flag_convert` 的类型转换 load 测试
结论：接受，已补齐。见 [`test/test_simd_memory.cpp`](/home/i/projects/forward/ccforge/test/test_simd_memory.cpp)。

### T-13 `alignment_v` 语义验证不足
结论：接受，但本轮未补（后续补强项）。

### T-14 compile-probe 未覆盖 `select` 和数学函数
结论：接受，已部分补齐（`select(bool, ...)`、`abs/sqrt` 以及新增 API 的编译可见性探测）。其余数学函数与算法类探测可在后续继续补全。

---

## 当前状态验证
本轮改动已在本机执行完整测试集验证通过（`ctest` 16/16）。

## 后续建议（按优先级）
1. 将公开模板入口从 `enable_if` 迁移到 `requires`（I-8），并用 compile-probe 锁定重载参与规则
2. 补齐浮点除法与特殊值行为测试（T-8）
3. 引入 typed tests 系统性覆盖 `value_type`（T-9）
4. 为 `alignment_v` 增加“弱不变式”断言（T-13），避免绑定某个 ABI 的固定值

