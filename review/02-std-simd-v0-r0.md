# `std::simd` 实现与测试审查报告 (v0-r0)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd.cpp`、`test/test_simd_api.cpp`、`test/test_simd_include.cpp`、`test/test_simd_operators.cpp`、`test/test_simd_math.cpp`、`test/test_simd_feature_macro.cpp`、`test/test_simd_abi.cpp`、`test/test_simd_mask.cpp`、`test/test_simd_memory.cpp`

参考规范：P1928R15 (C++26 `std::simd`)，C++26 草案 [simd] 章节

---

## 第一部分：实现问题

### I-1 缺少 `compress` 和 `expand` 函数

- 严重程度：高
- 位置：整个文件缺失

**问题描述：** 实现完全缺少 `compress` 和 `expand` 函数。`compress` 根据 mask 选取元素并紧凑排列，`expand` 执行反向操作。

**规范要求：** P1928R15 定义了 `compress(const basic_vec&, const basic_mask&)` 和 `expand(const basic_vec&, const basic_mask&)` 作为核心 mask 排列操作，属于 [simd.mask.permute] 章节。

**当前实现：** 完全缺失，grep 搜索无任何匹配。

---

### I-2 缺少 gather/scatter 内存操作

- 严重程度：高
- 位置：整个文件缺失

**问题描述：** 实现缺少 `unchecked_gather_from`、`partial_gather_from`、`unchecked_scatter_to`、`partial_scatter_to` 等间接内存访问函数。

**规范要求：** P1928R15 [simd.memory.permute] 定义了 gather/scatter 操作，允许通过索引向量从非连续内存位置加载/存储数据，是 SIMD 编程的核心操作之一。

**当前实现：** 完全缺失。仅实现了连续内存的 `partial_load`/`partial_store` 和 `unchecked_load`/`unchecked_store`。

---

### I-3 缺少 `iota` 函数

- 严重程度：中
- 位置：整个文件缺失

**问题描述：** 实现缺少 `iota` 生成函数。

**规范要求：** P1928R15 定义了 `iota<V>(T start = {})` 用于生成递增序列向量（如 `{0, 1, 2, 3}`），是常用的索引生成工具。

**当前实现：** 缺失。用户需手动使用 generator 构造函数模拟。

---

### I-4 缺少 `basic_vec` 的 `operator++` 和 `operator--`

- 严重程度：中
- 位置：`basic_vec` 类定义（第 870-1105 行）

**问题描述：** `basic_vec` 未实现前缀/后缀自增和自减运算符。

**规范要求：** P1928R15 [simd.overview] 明确列出 `basic_vec` 应提供 `operator++()`, `operator++(int)`, `operator--()`, `operator--(int)`，对每个 lane 执行自增/自减。

**当前实现：** 仅 `simd_iterator` 实现了这些运算符（第 1222-1242 行），`basic_vec` 本身缺失。

---

### I-5 缺少 `basic_vec` 的 `operator!` 运算符

- 严重程度：低
- 位置：`basic_vec` 类定义

**问题描述：** `basic_vec` 未实现逻辑非运算符 `operator!`，该运算符应返回 `basic_mask`。

**规范要求：** P1928R15 [simd.overview] 规定 `operator!` 应返回 `mask_type`，对每个 lane 执行逻辑非（非零为 false，零为 true）。

**当前实现：** `basic_mask` 实现了 `operator!`（第 750 行），但 `basic_vec` 缺失。

---

### I-6 `initializer_list` 构造函数静默截断而非编译错误

- 严重程度：中
- 位置：第 898-907 行

**问题描述：** 当 `initializer_list` 元素数量超过 lane 数时，构造函数静默截断多余元素；元素不足时静默零填充。

**规范要求：** P1928R15 中 `basic_vec` 的构造语义要求通过 range 构造函数处理，元素数量不匹配应有明确的前置条件或编译期检查。`initializer_list` 构造函数本身不在 P1928R15 的规范 API 中。

**当前实现：**
```cpp
constexpr basic_vec(initializer_list<T> values) noexcept : data_{} {
    simd_size_type i = 0;
    for (const T& value : values) {
        if (i >= size) {
            break;  // silent truncation
        }
        data_[i] = value;
        ++i;
    }
}
```
这是一个非标准扩展构造函数，且静默截断行为可能掩盖用户错误。

---

### I-7 `broadcast` 构造函数为 `explicit` 但规范要求非 `explicit`

- 严重程度：中
- 位置：第 885 行

**问题描述：** 广播构造函数 `basic_vec(T value)` 被标记为 `explicit`。

**规范要求：** P1928R15 中 `basic_vec` 的单值构造函数（broadcast）接受 `U&&` 参数，当类型转换是值保持的（value-preserving）时应为非 `explicit`，仅在需要 `flag_convert` 时才为 `explicit`。对于 `basic_vec<T>(T)` 这种同类型情况，应为隐式转换。

**当前实现：**
```cpp
constexpr explicit basic_vec(T value) noexcept
```
这导致 `vec<int, 4> v = 42;` 无法编译，而规范允许此用法。

---

### I-8 使用 `enable_if_t` SFINAE 而非 C++23 `requires` 子句

- 严重程度：低
- 位置：全文（第 891-893、922-923、991、1023、1031、1060 行等）

**问题描述：** 所有模板约束均使用 `enable_if<..., int>::type = 0` 形式的 SFINAE。

**规范要求：** P1928R15 使用 *Constraints:* 段落描述约束条件，现代实现应使用 `requires` 子句。项目要求 C++23（`CMAKE_CXX_STANDARD=23`），完全可以使用 concepts/requires。

**当前实现：** 例如第 891-893 行的 generator 构造函数：
```cpp
template<class G,
         typename enable_if<detail::is_simd_generator<G, T, simd_size<T, Abi>::value>::value &&
             !is_same<detail::remove_cvref_t<G>, basic_vec>::value, int>::type = 0>
constexpr explicit basic_vec(G&& gen)
```

---

### I-9 数学函数缺少 `constexpr` 标记

- 严重程度：低
- 位置：第 1932、1972、1980、1989、1998、2007、2016、2025、2034、2043、2053 行

**问题描述：** `abs`、`sqrt`、`floor`、`ceil`、`round`、`trunc`、`sin`、`cos`、`exp`、`log`、`pow` 等数学函数标记为 `inline` 而非 `constexpr`。

**规范要求：** P1928R15 中这些数学函数的 constexpr 性取决于底层标量函数的 constexpr 支持。C++23 中 `std::abs`（整数版本）、`std::floor`、`std::ceil`、`std::round`、`std::trunc` 已是 `constexpr`，但三角函数和指数函数尚未。

**当前实现：** 统一使用 `inline` 而非区分哪些可以是 `constexpr`。对于 `abs`（整数类型）、`floor`、`ceil`、`round`、`trunc` 等 C++23 已 constexpr 的函数，应标记为 `constexpr`。

---

### I-10 `end()` 返回 `default_sentinel_t` 但 `begin()` 非 `const` 重载返回可变迭代器

- 严重程度：低
- 位置：第 929-946 行

**问题描述：** `begin()` 的非 `const` 重载返回 `iterator`（可变迭代器），但 `operator[]` 仅返回值而非引用，因此通过迭代器修改 lane 值的语义不明确。

**规范要求：** P1928R15 中 `operator[]` 返回值（`value_type`），不返回引用。`begin()`/`end()` 提供只读遍历。迭代器的 `operator*` 应返回 `value_type`。

**当前实现：** `simd_iterator::operator*`（第 1214 行）返回 `value_type`（值语义），这与规范一致。但提供非 `const` 的 `begin()` 可能误导用户认为可以通过迭代器修改值。

---

### I-11 `select` 函数模板参数推导不够灵活

- 严重程度：低
- 位置：第 2062-2084 行

**问题描述：** `select` 函数的模板参数 `T` 和 `Abi` 需要从 `basic_vec` 参数推导，但当两个参数都是标量时无法使用。

**规范要求：** P1928R15 定义了 `select(bool, const T&, const U&)` 标量重载和 `select(const basic_mask&, const T&, const U&)` 向量重载，其中 `T` 和 `U` 可以是不同类型（包括标量与向量混合）。

**当前实现：** 缺少 `select(bool, const T&, const U&)` 标量重载。

---

### I-12 `forge.cmake` 检测探针使用了非标准 API

- 严重程度：低
- 位置：`forge.cmake` 第 53-61 行

**问题描述：** simd 特性检测探针使用 `std::simd::vec<float>` 和 `std::simd::reduce`，这些是正确的 P1928R15 API 名称。但如果未来标准库实现的命名空间结构略有不同，探针可能失效。

**当前实现：** 探针逻辑合理，此为低风险观察。

---

## 第二部分：测试问题

### T-1 缺少 `compress`/`expand` 测试

- 严重程度：高
- 涉及文件：所有测试文件

**问题描述：** 由于实现缺少 `compress` 和 `expand`（I-1），自然没有对应测试。

**期望行为：** 应有测试验证 `compress` 在各种 mask 模式下正确紧凑元素，`expand` 正确散布元素。

**当前测试：** 完全缺失。

---

### T-2 缺少 gather/scatter 测试

- 严重程度：高
- 涉及文件：所有测试文件

**问题描述：** 由于实现缺少 gather/scatter（I-2），没有对应测试。

**期望行为：** 应有测试验证通过索引向量从非连续内存加载/存储的正确性，包括越界索引处理。

**当前测试：** 完全缺失。

---

### T-3 缺少 `operator++`/`operator--` 测试

- 严重程度：中
- 涉及文件：`test/test_simd_operators.cpp`

**问题描述：** 由于实现缺少 `basic_vec` 的自增/自减运算符（I-4），没有对应测试。

**期望行为：** 应有测试验证前缀/后缀自增自减对每个 lane 的效果。

**当前测试：** `test_simd_operators.cpp` 测试了一元运算符（`+`、`-`）、复合赋值、比较运算符，但缺少 `++`/`--`。

---

### T-4 缺少位运算符的运行时测试

- 严重程度：中
- 涉及文件：`test/test_simd_operators.cpp`

**问题描述：** `basic_vec` 的位运算符（`&`、`|`、`^`、`~`、`<<`、`>>`、`%`）在实现中存在（第 991-1099 行），但运行时测试中完全未覆盖。

**期望行为：** 应有测试验证整数类型的位运算、移位运算和取模运算的正确性。

**当前测试：** `test_simd_operators.cpp` 仅测试了算术运算符（`+=`、`-=`、`*=`、`/=`）和比较运算符，位运算完全缺失。编译探测 `test_simd_api.cpp` 中也未涉及位运算。

---

### T-5 缺少 `select` 函数的运行时测试

- 严重程度：中
- 涉及文件：所有测试文件

**问题描述：** `select` 函数（第 2062-2084 行）实现了三个重载，但没有任何运行时测试。

**期望行为：** 应有测试验证 `select(mask, true_vec, false_vec)` 在各种 mask 模式下正确选择元素。

**当前测试：** 完全缺失。

---

### T-6 `chunk`/`cat`/`permute` 测试默认被跳过

- 严重程度：中
- 涉及文件：`test/test_simd.cpp`、`test/CMakeLists.txt`

**问题描述：** 虽然 `test_simd.cpp` 中包含 `chunk`/`cat`/`permute` 的运行时测试（第 46-109 行），但这些测试被 `#if defined(FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)` 守卫。CMakeLists.txt 第 49 行为 `test_simd` 定义了此宏，因此运行时测试实际上是启用的。

**当前状态：** CMakeLists.txt 正确启用了该宏。但编译探测 `test_simd_api.cpp` 中的对应部分（第 64-100 行）由 `FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES` 守卫，也在 CMakeLists.txt 第 76 行启用。此条为信息性说明，不构成问题。

---

### T-7 缺少 `minmax` 函数的运行时测试

- 严重程度：低
- 涉及文件：`test/test_simd_math.cpp`

**问题描述：** `minmax` 函数（第 1959-1962 行）返回 `pair<basic_vec, basic_vec>`，但测试中仅测试了 `min` 和 `max`，未测试 `minmax`。

**期望行为：** 应有测试验证 `minmax` 返回的 pair 中 first 为 min、second 为 max。

**当前测试：** `test_simd_math.cpp` 第 37-51 行测试了 `min` 和 `max`，但缺少 `minmax`。

---

### T-8 缺少浮点除法运算符测试

- 严重程度：低
- 涉及文件：`test/test_simd_operators.cpp`

**问题描述：** 虽然 `CompoundAssignmentsUpdateAllLanes` 测试了整数的 `/=`，但缺少浮点类型的除法测试，特别是除以零、NaN 传播等边界情况。

**期望行为：** 应有测试验证浮点除法的精度和特殊值处理。

**当前测试：** 仅有整数复合赋值测试。

---

### T-9 缺少不同 `value_type` 的覆盖

- 严重程度：中
- 涉及文件：所有测试文件

**问题描述：** 绝大多数测试仅使用 `int` 和 `float` 类型。缺少对 `short`、`long long`、`double`、`signed char`、`unsigned` 类型的系统性测试。

**期望行为：** 应有参数化测试覆盖所有支持的算术类型，验证 `detail::is_supported_value` 的类型范围。

**当前测试：** `test_simd_mask.cpp` 使用了 `signed char` 和 `long long`，`test_simd_abi.cpp` 使用了 `long long`，但覆盖不系统。

---

### T-10 缺少 `broadcast` 构造函数隐式转换的测试

- 严重程度：低
- 涉及文件：`test/test_simd.cpp`

**问题描述：** 未测试 broadcast 构造函数的 `explicit` 行为。由于 I-7 指出当前实现错误地将同类型 broadcast 标记为 `explicit`，应有测试验证 `vec<int, 4> v = 42;` 是否可编译。

**期望行为：** 应有编译探测验证值保持转换的隐式 broadcast 可用。

**当前测试：** 所有测试均使用显式构造 `vec<int, 4>(42)` 或列表初始化 `vec<int, 4>{1,2,3,4}`，未验证隐式转换。

---

### T-11 `unchecked_load`/`unchecked_store` 测试默认被跳过但已启用

- 严重程度：低
- 涉及文件：`test/test_simd_memory.cpp`、`test/CMakeLists.txt`

**问题描述：** `unchecked_load`/`unchecked_store` 测试由 `FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS` 守卫，CMakeLists.txt 第 62 行已启用。测试覆盖了指针和迭代器重载、masked 变体。

**当前状态：** 测试已启用且覆盖合理。但缺少 `flags` 参数（`flag_convert`、`flag_aligned`、`flag_overaligned`）的测试。

---

### T-12 缺少 `flag_convert` 在内存操作中的测试

- 严重程度：中
- 涉及文件：`test/test_simd_memory.cpp`

**问题描述：** 所有内存操作测试均使用同类型加载/存储（如 `int` 数组加载到 `vec<int, 4>`）。缺少类型转换加载（如从 `float` 数组加载到 `vec<int, 4>` 并使用 `flag_convert`）的测试。

**期望行为：** 应有测试验证 `partial_load<vec<int, 4>>(float_ptr, count, flag_convert)` 的正确性，以及缺少 `flag_convert` 时非值保持转换的编译失败。

**当前测试：** 完全缺失。`test_simd_operators.cpp` 第 38-46 行测试了 `basic_vec` 的转换构造函数，但内存操作的类型转换未覆盖。

---

### T-13 缺少 `alignment_v` 语义的运行时验证

- 严重程度：低
- 涉及文件：`test/test_simd_abi.cpp`

**问题描述：** `alignment_v` 仅在编译期 `static_assert` 中验证了下界（`>= alignof(int)`），但未验证其实际值是否合理（如 `vec<float, 4>` 应至少 16 字节对齐）。

**期望行为：** 应有更精确的对齐值验证，特别是对于 native ABI 宽度的向量。

**当前测试：** `test_simd_abi.cpp` 第 28-29 行仅有 `static_assert(alignment_v<int4> >= alignof(int))`。

---

### T-14 编译探测 `test_simd_api.cpp` 未覆盖 `select` 和数学函数

- 严重程度：低
- 涉及文件：`test/test_simd_api.cpp`

**问题描述：** 编译探测验证了构造函数、迭代器、内存操作、chunk/cat/permute 的 API 表面，但缺少 `select`、`reduce`、数学函数（`abs`、`sqrt`、`min`、`max` 等）的编译探测。

**期望行为：** 编译探测应覆盖所有公开 API 的可编译性。

**当前测试：** `test_simd_api.cpp` 约 318 行，覆盖了类型别名、构造函数、迭代器、内存操作，但缺少算法和数学函数。

---

## 第三部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 高       | 2 (I-1, I-2) | 2 (T-1, T-2) | 4 |
| 中       | 4 (I-3, I-4, I-6, I-7) | 6 (T-3, T-4, T-5, T-6, T-9, T-12) | 10 |
| 低       | 6 (I-5, I-8, I-9, I-10, I-11, I-12) | 6 (T-7, T-8, T-10, T-11, T-13, T-14) | 12 |
| 合计     | 12       | 14       | 26   |

### 优先修复建议

1. **最高优先级 — I-1、I-2：** 补充 `compress`/`expand` 和 gather/scatter 实现。这些是 P1928R15 的核心功能，缺失意味着 backport 的 API 覆盖率存在重大缺口。用户依赖这些操作进行非连续内存访问和 mask 压缩，是 SIMD 编程的基础操作。

2. **高优先级 — I-4、I-7：** 补充 `basic_vec` 的 `operator++`/`operator--`，修正 broadcast 构造函数的 `explicit` 标记。这些直接影响 API 兼容性，用户代码在标准实现上可能依赖隐式 broadcast 和自增运算符。

3. **中优先级 — I-3、I-6：** 补充 `iota` 函数，移除或修正非标准的 `initializer_list` 构造函数。`iota` 是常用工具函数；`initializer_list` 构造函数的静默截断行为可能掩盖用户错误。

4. **中优先级 — T-4、T-5、T-9、T-12：** 补充位运算符、`select` 函数、多类型覆盖和 `flag_convert` 内存操作的运行时测试。这些是已实现功能的测试缺口，存在回归风险。

5. **低优先级：** I-5、I-8、I-9、I-10、I-11 及 T-7、T-8、T-10、T-11、T-13、T-14 可在后续迭代中逐步改进。
