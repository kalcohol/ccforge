# `std::simd` 独立审查报告 (p1-v0-r8)

审查对象：

- 实现文件：`backport/cpp26/simd/memory.hpp`
- 测试文件：`test/simd/compile/test_simd_api_memory.cpp`
- 测试文件：`test/simd/runtime/test_simd_memory.cpp`
- 探针与注册：`test/simd/configure_probes/*.cpp`、`test/simd/CMakeLists.txt`

审查基线：

- 隔离 worktree：`fix/std-simd-p1-r8`
- 基线提交：`39d03a7`
- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`

执行方式：

- 不特别复用既有 review 结论，只以 `39d03a7` 的 `std::simd` 现状为对象复核
- 以当前工作草案为唯一标准基线，同时核查实现内部“已声明支持类型集合”是否自洽
- 结合源码直读、最小编译探针和现有测试覆盖面检查给出结论

---

## 执行摘要

当前 `39d03a7` 基线上的 `std::simd::memory` 还存在一组不应放过的问题：

1. `partial_load` / `unchecked_load` 的 pointer/count、pointer/sentinel 默认 `V` 推导仍然套用了“仅 arithmetic 非 bool”的门槛，导致标准允许的 `std::complex<T>` 默认入口不可达
2. 同一处默认 `V` 推导还与库内已经声明支持的扩展整数类型集合不自洽；在启用 `__int128` 的平台上，显式 `basic_vec<__int128>` 可用，但默认 pointer/count 入口却推不出来
3. 现有 compile/runtime/configure tests 没有覆盖这组 supported-type 默认入口，因此只有额外探针才把问题挖出来，说明 memory 测试体系仍有明显缺口

这不是边缘实现细节，而是公开接口推导规则与标准/实现自身能力集合脱节。

---

## 主要问题

### I-1 `[simd.syn]` 默认 pointer/count load 入口对 supported type 的推导门槛错误

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/memory.hpp`
  - `test/simd/compile/test_simd_api_memory.cpp`
  - `test/simd/runtime/test_simd_memory.cpp`
  - `test/simd/configure_probes/*.cpp`

**问题描述：**

当前工作草案在 `[simd.syn]` 中给 `partial_load` / `unchecked_load` 的 pointer、sentinel、range 默认入口统一写成 `template<class V = see below, ...>`。而 `[simd.general]` 明确把 `complex<T>`（其中 `T` 为 vectorizable floating-point type）纳入 vectorizable type 集合。

但 `39d03a7` 基线实现里的默认 pointer load-vector 选择仍然沿用“arithmetic 非 bool”门槛，于是出现两个结果：

1. `std::complex<float>` / `std::complex<double>` 这类标准允许的 element type，在显式写 `V` 时可以工作，但默认 pointer/count、pointer/sentinel `partial_load` / `unchecked_load` 却无法推导
2. 库内 `is_supported_value` 已经把 `__int128` / `unsigned __int128` 视为支持类型；显式 `basic_vec<__int128>` 路径成立，但默认 pointer/count 入口仍被排除，内部能力集合不自洽

也就是说，问题不是“实现根本不支持这些类型”，而是**公开默认入口的类型推导使用了比真实支持集合更窄的门槛**。

**影响：**

- 标准允许的 `std::complex<T>` 默认 load 入口不可用，属于公开接口缺口
- 对启用扩展整数的平台，库内已支持能力与默认入口行为不一致，容易制造使用层误判
- 因为默认入口通常是用户最自然写法，这类问题会直接体现在 source compatibility 上

**为什么现有测试没有兜住：**

现有 memory 测试虽然覆盖了大量 load/store/gather/scatter 组合，但几乎都集中在 `int4` 等 arithmetic 样例；缺少以下三类约束：

1. compile 期：默认 pointer/count、pointer/sentinel `partial_load` / `unchecked_load` 对 `std::complex<T>` 的返回类型断言
2. configure probe：supported non-arithmetic type 的默认入口可编译性探测
3. runtime：默认入口 round-trip 行为验证

只要还需要临时探针才能确认这种 supported-type 缺口，就说明测试体系本身不完整。

---

## 整改要求

1. 将默认 pointer load-vector 推导统一收敛到实现真实支持集合，而不是只看 arithmetic 非 bool
2. 至少对 `std::complex<float>` 补齐 compile/configure/runtime 三层覆盖
3. 对 `__int128` 一类实现已声明支持的扩展整数，在平台可用时补齐一致性探测，防止“显式可用、默认不可用”的再回归
4. `memory` 源码和测试文件都已经开始膨胀，建议趁这一轮把 load/store 与 gather/scatter 拆解，避免后续继续把不同问题堆进单文件里，降低遗漏 supported-type 覆盖的概率

---

## 结论

按 `39d03a7` 基线独立复核，当前 `std::simd::memory` 至少仍有以下一组 P1 问题：

1. 默认 pointer/count 与 pointer/sentinel load 入口对 supported type 的推导门槛错误，导致标准允许的 `std::complex<T>` 默认入口不可达
2. 同一缺陷还让实现已声明支持的扩展整数类型集合与默认入口行为失去自洽
3. 现有测试体系未能覆盖这一层 supported-type 默认入口，需要补齐 compile/configure/runtime 三层约束

这组问题应与测试补完、文件拆解一起一次性收口，否则同类遗漏还会继续藏在单文件和单测大杂烩里。
