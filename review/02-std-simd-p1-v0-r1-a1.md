# `std::simd` 审查答复 (p1-v0-r1-a1)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

应答对象：

- `review/02-std-simd-p1-v0-r1.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 不以更早 review 结论抵消本轮问题判断
- 本文件对应的是**实际已落地并已验证**的修复状态，而不是计划状态

---

## 总结

`p1-v0-r1` 提出的四项问题，本轮均已正面处理：

1. `simd-complex` 类型入口已打开，complex `simd` 不再在值类型闸门处整体失效
2. bit / math / complex 三组缺失算法面已补齐到本轮审查涉及的公开表面
3. integral `basic_vec` 的逐 lane 向量位移及 `where(mask, v)` 的对应赋值路径已可用
4. 上述标准表面已补充 configure probe、compile probe、runtime test 三层防线，不再需要靠临时探针人工确认

与此同时，本轮还按此前计划对实现结构做了拆分：算法层从原先继续膨胀的单个 `operations.hpp` 中拆为 bit / math / complex 三个子头文件，并保留 `operations.hpp` 作为统一入口和兼容层。

---

## 逐条答复

### I-1 `simd-complex` 路径整体未打开

裁决：**已修复。**

本轮已在 `backport/cpp26/simd/base.hpp` 中扩展值类型判定与转换规则：

- `is_supported_value` 现在接受 `complex<float>` / `complex<double>` 这类当前草案允许的 complex lane 类型
- 新增 complex 相关辅助 trait，用于区分 complex lane、提取底层 real 类型以及控制 value-preserving conversion

`backport/cpp26/simd/types.hpp` 中也已同步打开 complex 类型面：

- `basic_vec<complex<T>, Abi>` 可以实例化
- 新增 `real_type`
- 新增由实部向量和虚部向量构造 complex `simd` 的构造函数
- 新增成员 `real()` / `imag()` 读写接口

本轮 configure probe 与 compile probe 已直接覆盖：

- `vec<complex<float>, 4>` 实例化
- `real` / `imag`
- complex `abs` / `arg` / `norm` / `conj` / `proj` / `log10` / `pow` / `polar`

### I-2 标准算法表面仍是明显子集

裁决：**已修复到本轮审查范围。**

本轮补齐了 `p1-v0-r1` 直接点名的三组算法面，并把实现从单文件拆开：

1. bit family
   - `byteswap`
   - `popcount`
   - `countl_zero` / `countl_one`
   - `countr_zero` / `countr_one`
   - `bit_width`
   - `has_single_bit`
   - `bit_floor` / `bit_ceil`
   - `rotl` / `rotr`
2. math family
   - `log10`
   - `frexp`
   - `modf`
   - `remquo`
   - 以及同一轮一起补齐的扩展浮点数学表面
3. complex family
   - `real` / `imag`
   - `abs` / `arg` / `norm`
   - `conj` / `proj`
   - `polar`
   - complex `exp` / `log` / `log10` / `sqrt` / `sin` / `cos` / `tan` / `pow` 等

入口兼容性保持不变：

- 用户仍从 `backport/cpp26/simd/operations.hpp` 进入
- `simd.hpp` 的总入口未改变

### I-3 integral `basic_vec` 位移运算符集合不完整

裁决：**已修复。**

本项在前置修复中已补齐，本轮继续纳入全量验证并由新增测试锁住：

- `basic_vec` 的 `vec <<= vec` / `vec >>= vec`
- `vec << vec` / `vec >> vec`
- `where(mask, vec) <<= vec`
- `where(mask, vec) >>= vec`

对应运行时与编译期测试均已保留并继续通过。

### I-4 现有测试仍未把“标准表面完整性”锁住

裁决：**已修复到本轮范围。**

这一条本质上是测试体系问题，本轮已补成体系的防线，而不再是零散样例：

1. configure-time probes
   - `bit_surface.cpp`
   - `math_surface.cpp`
   - `math_rejects_integral.cpp`
   - `complex_surface.cpp`
2. compile probes
   - `test_simd_api_math.cpp`
   - `test_simd_api_complex.cpp`
3. runtime tests
   - `test_simd_bit.cpp`
   - `test_simd_math_ext.cpp`
   - `test_simd_complex.cpp`

其中负向约束也已锁住：

- 浮点数学算法不再对 integral `vec` 错误开放
- configure probe 以“应当编译失败”的方式稳定验证该约束

---

## 本轮修复落地

### 实现侧

- `backport/cpp26/simd/base.hpp`
  - 打开 complex lane 类型支持
  - 补 complex 相关 trait 与转换约束
- `backport/cpp26/simd/types.hpp`
  - 补 complex `basic_vec` 构造与 `real` / `imag` 访问面
  - 收紧多组运算符参与条件，避免错误候选进入公开 API
- `backport/cpp26/simd/operations.hpp`
  - 保留统一入口，移除继续膨胀的单文件实现
- `backport/cpp26/simd/operations_bit.hpp`
  - 承载 bit family
- `backport/cpp26/simd/operations_math.hpp`
  - 承载浮点数学与分类函数
- `backport/cpp26/simd/operations_complex.hpp`
  - 承载 complex `simd` 公开算法

### 测试侧

- `test/CMakeLists.txt`
  - 新增 configure probe、compile probe 与 runtime test 挂接
- `test/simd_test_common.hpp`
  - 新增 `complex4f` / `complex4d` 等公共测试别名
- 新增 bit / math / complex 对应 probe 与测试文件，形成分功能测试拆分

---

## 验证

本轮已完成以下验证：

- `cmake --build build-p1 -j`
- `ctest --test-dir build-p1 --output-on-failure`
- `cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
- `cmake --build build-gcc -j`
- `ctest --test-dir build-gcc --output-on-failure`
- `CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
- `cmake --build build-zig -j`
- `ctest --test-dir build-zig --output-on-failure`
- `podman` clang latest 全量构建与测试
- `podman` gcc latest 全量构建与测试

结果：

- 本机 `build-p1`：33/33 测试通过
- 本机 GCC：35/35 测试通过（含 examples）
- 本机 Zig：35/35 测试通过（含 examples）
- Podman Clang：35/35 测试通过
- Podman GCC：35/35 测试通过

结论：`p1-v0-r1` 所指出的问题，在本轮修复范围内已完成实现、补测和跨工具链验证。
