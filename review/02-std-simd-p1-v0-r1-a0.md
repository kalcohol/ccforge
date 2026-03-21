# `std::simd` 审查答复 (p1-v0-r1-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

答复口径：

- 以 `p1-v0-r1` 这份独立审查为准，不以更早 review 结论作为驳回依据
- 本轮实际修复范围收敛为：先完整收口 I-3，以及本轮应答实现里暴露出的提交污染问题
- I-1 / I-2 仍然成立，但不在这次窄修复轮次内完成

---

## 逐条答复

### I-1 `simd-complex` 路径整体未打开

裁决：**接受问题判断，延期处理。**

当前工作树下，这个问题确实仍然存在：`is_supported_value` 仍把 `basic_vec` 限定在 arithmetic 非 `bool` + 扩展整数子集，因此 complex lane 路径整体不可达。

本轮不实现 complex `simd`，原因不是“旧 review 已覆盖”，而是：

1. complex 支持不是单点补丁，而是类型面、构造面、访问器和算法面的成组工作
2. 本轮的目标是先把 I-3 从“应答承诺”收口到“实现、测试、提交卫生一致”

因此，I-1 在本轮中被**明确保留为未解决问题**，后续需要独立修复轮处理。

### I-2 标准算法表面仍是明显子集

裁决：**接受问题判断，延期处理。**

当前实现的公开算法面仍然只是现行草案的一个子集，bit family / math family / complex family 的成组缺口判断成立。

本轮同样不试图一次性补完整个算法表面，原因是：

1. 这会把当前修复轮从“收口 I-3”扩大成新的大轮次实现
2. 在没有先完成完整 surface audit 和测试分层设计前，直接补算法族容易继续产生接口漏测

因此，I-2 在本轮中也被**明确保留为未解决问题**，不是被驳回。

### I-3 integral `basic_vec` 位移运算符集合不完整

裁决：**已修复。**

本轮已补齐逐 lane 向量位移重载，并把对应测试补到现有拆分后的测试结构中。

已落地内容：

1. `backport/cpp26/simd/types.hpp`
   - 新增 `operator<<=(const basic_vec&)`
   - 新增 `operator>>=(const basic_vec&)`
   - 新增 `operator<<(basic_vec, const basic_vec&)`
   - 新增 `operator>>(basic_vec, const basic_vec&)`
2. `backport/cpp26/simd/where.hpp`
   - 新增 `where_expression::operator<<=(const value_type&)`
   - 新增 `where_expression::operator>>=(const value_type&)`
3. 运行时测试
   - `test/test_simd_operators.cpp` 补齐 `vec << vec` / `vec >> vec` / `vec <<= vec` / `vec >>= vec`
   - `test/test_simd_memory.cpp` 补齐 `where(mask, vec) <<= vec` / `>>= vec`
4. 编译期测试
   - `test/test_simd_api_core.cpp` 补齐 integral `vec` 的逐 lane 位移重载可见性
   - `test/test_simd_api_where.cpp` 补齐 `where_expression` 的逐 lane 位移赋值可见性
   - 同时增加 floating `vec` / floating `where_expression` 的负向约束检查，防止错误放宽

### I-4 现有测试仍未把"标准表面完整性"锁住

裁决：**部分接受，本轮仅收口 I-3 对应测试。**

这条观察本身成立。本轮修复后，I-3 已经从“只靠人工探针发现”变成了有 compile probe 和 runtime tests 双重锁定的状态。

但 I-4 并没有因此整体关闭，因为：

- complex `simd` 路径仍缺测试
- bit / math / complex 算法表面仍缺系统性 surface audit

因此，本轮对 I-4 的处理范围是：

1. 对 I-3 补齐对应测试
2. 不把“锁住全部标准表面”伪装成已经完成

---

## 本轮实际修复

### Fix-9：补齐逐 lane 向量位移重载并补测

- `basic_vec` 的逐 lane 左移/右移及赋值重载已补齐
- `where_expression` 的逐 lane 左移/右移赋值重载已补齐
- 运行时与编译期覆盖已同步补齐

### 附带收口：清理本轮应答实现引入的提交污染

本轮还额外修正了 Fix-9 提交链里的无关追踪内容：

- `.claude/settings.local.json` 已从版本库移除
- `3rdparty/googletest/` 已恢复为本地存在但不入库的状态，不再把整份 vendor 内容带入本轮修复提交

这项属于提交卫生修正，不改变 `std::simd` 的公开语义。
