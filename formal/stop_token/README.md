# inplace stop 协议模型

本工具对列出的有限配置做 bounded exhaustive exploration，检查 source lock、waiter bit、callback 生命周期和 completion wait/notify。基线配置未发现协议缺陷；结论仅限于显式模型及边界，不构成对任意线程数、C++ abstract machine 或全部标准库实现的证明。

## 代码与状态

对应代码是 [stop_token.hpp](../../backport/cpp26/execution/stop_token.hpp)，协议说明见 [设计文档](../../docs/roadmap/inplace-stop-callback-design.md)。source word 包含 stop、lock、waiter bit。争用者先 yield，再用 CAS 发布 waiter bit；unlock 的 exchange 返回值决定是否 notify_all。request_stop 在锁外调用 callback，在锁内发布 completion 并通知；跨线程析构在 completion 后执行最终 lock handoff 才能回收节点。strict 模式以 tombstone 检测 source 生命周期前置条件违例。

每个线程是一组 frame，每个 frame 保存 routine、program counter 及局部值。step 是一次 atomic access、wait compare-and-block、notify、锁内 plain block 或 local continuation；local steps 与前一步合并。用户 callback 也是有限 script，可嵌套注册或析构。`model.py` 保存 world、step tables、内存和库行为、ghost clocks 与性质；`scenarios.py` 保存 scripts；`mutants.py` 保存 mutant catalog 和 controls；`sensitivity.txt` 保存逐配置敏感性声明；`tiers.py` 保存配置矩阵及显式 quick 名单。

```bash
python3 -B formal/stop_token/check.py --steps
```

每个源代码 step 引用 enclosing function 及短 fragment，fragment 必须在该函数内唯一。输出和 trace 按当前头文件解析行号。脚本、test-hook hold、库内部及 mutant-only 转移是 model-only；有调用点时同时引用源代码调用点。fragment 的存在不证明抽象正确，仍须人工审查。

## 抽象与边界

source 和节点身份用小整数表达；链表、invocation status、请求线程、completion、回收状态显式保存。锁内 plain accesses 合并，依据是同一 source lock 排斥其他正常访问；mutants 和 HB 模式检查对此假设的破坏。callback 内容由有限 script 代替，不模拟任意用户代码、异常、分配器或实际地址复用。strict source destruction 场景故意违反前置条件，以 abort 为成功检测。

a 家族覆盖纯锁争用；b 是 request/destructor；c 是 self destruction 和嵌套；d 是注册竞态；e 是多个 requester；f 是多 callback；g 是 strict tombstone；s 是 inline invocation 与嵌套生命周期；x 是组合场景。具体边界列于 scenarios：最多五个线程、三个 callback，纯锁最多每线程八次操作；Y 变体控制有限 yield 重读次数。更大边界须新增配置，不能从现有结果外推。

## wait/notify 库行为

abstract 模式采用 [atomics.wait] 的值比较、阻塞及 eligible notification 规则；支持 spurious returns，唤醒后仍须重读。libstdc++ ≤15 的 count 模式在等待期间登记 waiter，notify 检查 count 后决定是否 wake。

cnt16 模式的 provenance 是 x86-64 上的 GCC 16.2 libstdc++（libstdc++.so.6.0.36）。其可观察行为是：先不登记地 spin reload；每次进入睡眠前增加 waiter count，futex wait 返回后减少 count，再按需重试；notify 在 count 为零时跳过 wake。这一描述仅针对该版本与平台，不推广至每个 libstdc++ 16 build 或 AArch64。

libc++ 和 MSVC 仅由 abstract rule 覆盖；未建模其内部算法。libc++ 的 2 s futex timeout 可能让动态测试中的 missed notification 最终恢复，因此动态测试不能仅凭最终完成排除丢失通知。显式模型不依赖实际 timeout。

## 内存模型

SC 直接对共享状态排序。x86-TSO 使用每线程 FIFO store buffer，保留普通 store/load 的可见性差异，模型中的 seq_cst 操作提供相应 barrier。ARM-like 模式允许更弱的 store-buffer 行为，用于检验 waiter-count fast path；它不是完整 AArch64 axiomatic model，不覆盖所有重排序或 CPU/compiler mapping。

stale 模式允许 relaxed/acquire state load 返回不早于本线程已观察或写入值的历史值，保持 read-read/write-read coherence；RMW 和 compare-and-block 读最新值。HB 模式用 vector clocks 和 release sequences 检查 ghost plain locations，包括链表、节点 status、请求者 invocation record、函数对象、用户数据及 request 前发布的数据。HB 模式在 SC 交错上检查同步关系，不声称穷举全部 C++ weak-memory executions。

## 性质与结果解释

bounded safety 检查 lock-word invariants、重复调用、错误返回、节点回收后的访问、用户 callback 尚未完成就析构及 ghost data races。terminal state 不得留下 blocked 或 unfinished thread；strict precondition probe 的 abort 单独计数。

全图为 acyclic 时每条 execution 都完成。对 spurious wakeups 或 spinning 形成的 cyclic graph，检查每个 reachable state 仍可到达 completion。这比“每个 fair schedule 都完成”弱，不能作 universal liveness 结论。

expected 目录以逐配置 TSV 保存 states、transitions、terminal states、outcome 和完整 semantic record 的 SHA-256。semantic record 包含完整配置、coverage、abort、blocked、depth、buffer、complete、acyclic 和存在时的 liveness counters。所有 tiers 共享这些记录；完整原始结果、分类矩阵和资源数据由 runner 写入 summary 与 JSONL。

基线配置穷举全部可达状态；mutant 和 negative control 按 manifest 可在首个违例处停止，因此其 states 和 transitions 表示到反例为止的探索。每配置上限通常为 8,000,000 states，stale 子集使用 6,000,000；达到上限而未完成的运行不能通过或更新资产。`complete` 表示完成该配置规定的探索（包括首个反例停止），不表示失败配置也遍历了整个图。

引擎在 trace reconstruction 前快照 coverage，避免回放 successors 污染 coverage；这是有意的精度修正。successors 的顺序和 step ids 保持稳定，category records 排序以确保确定性。

## 工具敏感性

每次运行同时核对 expected record 和 catalog outcome。mutants 覆盖通知数量与时序、waiter arming、stop bit 更新、completion、status 发布、self destruction、最终 handoff 及 memory order。逐配置通过和失败声明保留有意的 survivors；完整 tier 还要求每个 killed mutant 至少有一个 killer。H4/H5 单独弱化 completion acquire/store 的 HB 变体因最终 source-lock handoff 已建立顺序而按设计存活。具体 catalog 及 control 配置见 [mutants.py](mutants.py)，逐配置预期失败名单见 [sensitivity.txt](sensitivity.txt)。该清单由人工审查，工具永不重写；未列出的配置必须通过。

negative controls 是 ARM-like plain count-load fast path 及带 RMW fence 的变体；前者可暴露 missed wake，后者在部分有限场景中通过。通过的 control 同样必须匹配声明，不一概要求失败。summary 提供按 model 的 mutant kill matrix。

## 使用与资源

```bash
python3 -B formal/stop_token/check.py --tier quick
python3 -B formal/stop_token/check.py --tier full --jobs 8
python3 -B formal/stop_token/check.py --tier quick --jobs 1
python3 -B formal/stop_token/check.py --tier quick --start-method spawn
python3 -B formal/stop_token/check.py --tier quick --only 'b_req_dtor'
```

默认在 ignored 输出目录中按 tier 保存 results.jsonl、summary.md 和 traces。`--out` 可替换输出目录。默认 worker 数不超过 8、cpu_count 及可用内存的一半（每 worker 预算 4 GiB）；最重配置需要额外内存，较小机器建议 `--jobs 1`，先运行 quick。RSS 是 worker 的进程 peak，统一以 KiB 报告；无法测量时显示 unavailable；worker peak 不能直接视为 pool 总峰值；启动时间计入 wall。精确运行统计随 source.lock 和实际 summary 一起审阅。

## 维护

lock 是 change detection，**不是 fidelity proof**。Python 未改动也可能在 C++ 语义变化后继续通过；刷新 lock 必须人工审查改变的代码与模型，并记录 assessment。lock 保存 source、checker、expected-data digests，source digest mode、Python 版本、tier、数量、结果、日期和 assessment，同时以 last_full_qualification 保存上次 full 的日期、配置数、结果摘要和指纹。quick 只能刷新 source digest 及最近资格运行信息，checker 和 expected-data digests 必须等于上次 full 的记录。source digest mode 为 `sha256-crlf-to-lf-bytes-v4`：仅将 CRLF 转成 LF 后对头文件字节计算 SHA-256，不删除注释或空白。头文件的每次编辑（包括注释和空白）都要求刷新 lock；CRLF/LF 转换是唯一等价处理。

`--check-source` 同时运行快速 self-tests：字节 digest、tokenizer、函数作用域 fragment、lock qualification、输出路径保护及合成记录上的维护事务拒绝规则。self-tests 不探索状态，所有测试写入只发生在临时目录。

reference 不包含 namespace；定位器从 token stream 计算完整 namespace、class 及 function/block 祖先。显式 out-of-class qualification 与 in-class nesting 必须给出完全相同的 class 路径：`A::f` 不匹配 `B::A::f`，function/block 内的 local class 也不提供候选。不同 namespace 中存在相同 class 路径的多个 definition 时，无论 fragment 是否能区分，都拒绝并列出候选名称及行号；同一 namespace 内的 overload 仍可由唯一 fragment 区分。

定位使用完整 declarator name：constructor `X::X` 与 destructor `X::~X` 互不匹配，operator-function-id 和 conversion-function-id 必须完整一致（包括 conversion type）。普通 nonmember function 使用 `::f`，仍不包含 namespace，多个 namespace 候选按相同规则拒绝。inline friend definition 是 nonmember，不能作为 `X::f`；当前子集不支持 friend reference，`::f` 也不引用 friend。defaulted/deleted function 没有可引用的 body；空 body 没有 fragment，nested lambda body 不能独立引用。

fragment 定位器仅支持保守的 C++ 子集：backslash、可选水平空白（space/tab）、newline 按 C++23 phase 2 视为 splice，预处理指令内一致处理，指令行以外一律拒绝（包括注释、普通及 raw literals）；`#include` 的 header-name 作为一个 token 保留内容。声明和 class head 的 template angles 必须可明确配对，比较和移位 operands 须用括号消除歧义。class head 接受 class、struct 或 union、可选已验证 attributes、普通 ASCII identifier、可选 final、可选 allowlist base list，随后为 `{`；也接受 namespace block 内以已定义 class 为根的 qualified nested class head。引入 class body 的任何其他形式（包括其他 qualified name、UCN、非 ASCII 名称及匿名 class）均拒绝并给出行号。class base list 内 template arguments 的花括号不算 class body，template arguments 内的函数名不算 declarator。scope 判断采用 allowlist：declarator 前只接受 type/specifier words、qualified/template types、attributes 和 balanced decltype/explicit/alignas operands；constructor mem-initializer 和 expression prefix 不建立 definition。向前查找声明 specifiers 时跳过完整 balanced operands，因此含 braced operand 的 decltype 不会隐藏 friend。候选 declarator 的右括号后，只接受 cv/ref qualifiers、noexcept 或 noexcept(...)、override/final、balanced trailing return、支持的 requires-clause 和已验证的 attribute-specifier，然后必须是 body、constructor initializer list 或 try。任何其他 token 都不能建立 definition；被引用的函数没有有效 definition 时，检查失败。trailing return 接受 type names、pointer/ref punctuation、balanced operands 和 template arguments，不接受顶层的花括号、分号或等号。

requires-clause 只接受未加外层括号的 qualified concept name（可带 template arguments）或 requires-expression，可用 &&/|| 连接；允许声明或 namespace 层的 requires-expression。规则检查整个源文件的每个声明，包括未引用的声明和 qualifiers 后的 constraints；另允许以 `!Concept` 开始、后接 `&& Concept` 的括号 constraint，concept 路径拒绝 keyword operands；另接受 concept-id 的括号 unary fold（`&&` 或 `||`）及精确的 `sizeof...(identifier) > 0`；其他括号式 trailing requires 拒绝处理。函数体内禁止 coroutine keywords 和 requires-expression。

attribute-specifier 接受逗号分隔的 identifier 或 identifier::identifier 列表，每项可附加一组 arguments；arguments 只允许 identifiers、literals、逗号和配对圆括号。因此 arguments 内的方括号、花括号、分号、lambda 和 coroutine keywords 均不接受。code（包括 preprocessing directives）内的六种 digraphs 全部拒绝，遵守 `<::` 后不是 `:` 或 `>` 时分成 `<` 和 `::` 的标准词法例外；注释和 literals 的内容不受此限制。

lambda 仅支持 capture list、可选 parameter list、依次可选的 mutable、constexpr 或 consteval、noexcept 或 noexcept(...)、trailing return，随后为 body。attributes、template lambda、constraints 和其他 specifier 位置不在子集内。上述 allowlist 以外的形式一律使检查和 refresh 失败，诊断给出构造名与行号；需要新语法时必须有意扩展 matcher 及 self-tests。当前头文件作为正向 fixture，nested class 和 lambda body 不提供外层函数的 fragment。整个文件的字节 digest 负责检测每次编辑，fragment 只是附加定位检查。

当 `--check-source` 失败时，先审查 source/model fidelity，更新 steps 和 fragments。C++ 变化不需要修改模型时可用 quick；纯注释或纯空白改动同样可用 quick，assessment 必须明确是纯注释或纯空白改动，并说明模型为何不受影响。任何 checker 文件（包括 CLI）或 expected 数据变化都要求 full。Python 不变时 full 会重现相同结果，因此对 C++ 单独变化，真正保障是 assessment 中的对应关系审查，tier 本身不能证明 fidelity。先完成整 tier 并更新 expected，再独立刷新 lock：

```bash
python3 -B formal/stop_token/check.py --tier full --update-expected
python3 -B formal/stop_token/check.py --tier full --refresh-lock --assessment '说明源代码变化与模型对应关系'
python3 -B formal/stop_token/check.py --check-source
```

事务不接受 `--only`、空集、不完整运行或 catalog outcome 违例；update 与 refresh 不能合用。update 只重写该 tier 的 expected records，绝不改写 sensitivity manifest；运行结果与清单矛盾时，普通运行、update 和 refresh 均失败。refresh 要求与 expected 完全一致及非空 assessment。写入使用同目录临时文件与 atomic rename，仅改写 expected 或 lock。

修改 sensitivity manifest 必须手动编辑，并在 commit message 中说明依据。添加 scenario 时定义有限 scripts，再加入 full matrix，选择合适 quick 代表并审查声明结果。添加 mutant 时定义单点转移变化、catalog outcome 与原因，选择最便宜 killer 或明确 survivor，加入 quick。checker 重构必须保持 expected 不变；不得用自动 update 掩盖不明差异。

## 限制与待办

AArch64 上 libstdc++ 16 waiter-count load 的实际 memory order 尚未验证；MSVC internals 尚未检查。所有结论受场景和次数边界限制。checker 自身仅通过 mutants、controls、coverage 以及与原始实现的逐配置等价比较获得信心；这些验证仍可能共享同一抽象错误。

## 最近一次完整检验

2026-11-16 的 full qualification（字节 digest mode v4，含 declaration prefix 检查、balanced operand 扫描及快速 self-tests）覆盖 7,577 配置，quick manifest 为 99 配置。完整矩阵累计 236994777 states、847295392 transitions；全部逐配置结果匹配 expected、sensitivity manifest 及 catalog。

| Tier | Workers | 启动计入 wall (s) | 最大配置 RSS (MiB) | Pool peak RSS (MiB) |
|---|---:|---:|---:|---:|
| full | 8 | 969.11 | 3529.5 | 20224.3 |
| quick | 8 | 24.23 | 496.6 | 1507.6 |

测量使用默认及上述建议的 8 workers。pool peak 是每 50 ms 采样的父进程与所有存活子进程 RSS 总和，包含共享页，作为保守的进程池内存指标。实际硬件与负载会影响 wall；较小机器可按前述命令减少 workers。

| Model | 配置数 | States | Transitions |
|---|---:|---:|---:|
| arm_cnt16 | 452 | 23218249 | 80663501 |
| arm_cnt16_plain | 7 | 89730 | 223848 |
| arm_cnt16_plain_rmwfence | 7 | 106636 | 282770 |
| arm_cnt_plain | 7 | 57918 | 137561 |
| arm_cnt_plain_rmwfence | 7 | 82815 | 216320 |
| arm_cnt_sc | 454 | 21220793 | 73628107 |
| sc_abs | 1676 | 14170078 | 43420477 |
| sc_abs_hb | 331 | 15345798 | 49710453 |
| sc_abs_spur | 84 | 6523752 | 24159672 |
| sc_abs_spur_stale | 58 | 2352916 | 14246046 |
| sc_abs_stale | 438 | 6777757 | 35008077 |
| sc_cnt | 452 | 16486177 | 56362645 |
| sc_cnt16 | 902 | 27707926 | 92180817 |
| sc_cnt16_hb | 48 | 1397585 | 3721910 |
| sc_cnt16_stale | 64 | 9719165 | 54706102 |
| x86_abs | 452 | 5909065 | 18970723 |
| x86_cnt16 | 456 | 37054994 | 134066895 |
| x86_cnt_plain | 452 | 16471322 | 56313270 |
| x86_cnt_sc | 1230 | 32302101 | 109276198 |
