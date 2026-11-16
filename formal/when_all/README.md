# when_all 协议模型

本模型对 [when_all.hpp](../../backport/cpp26/execution/when_all.hpp) 做有界显式状态穷举，检查 completion credit、结果仲裁、取消传播和同步 receiver 自毁。结论仅限下面的有限脚本与库契约，不是完整 C++ weak-memory 证明。

## 源码与状态

整个 header 是唯一 locked source，包括类型计算、sender forwarding、成员声明、初始化与注释。`model.py` 镜像 operation 和 child receiver 的协议；`scenarios.py` 定义脚本；`tiers.py` 定义配置；`mutants.py` 保存单点变更与全部 killers；`sensitivity.txt` 人工声明逐配置失败。`expected/results.tsv` 保存 semantic records。

```bash
python3 -B formal/when_all/check.py --steps
```

可引用动作由 enclosing function 内唯一 fragment 定位。两个 error emplacement 路径具有相同 fragment，因此 storage step 明确标成 model-only；lambda body、隐式析构和库调用内部也不借用外层 fragment。全文件字节 lock 覆盖这些部分。fragment 存在不等于 fidelity proof。

状态包含 pending、disposition、result、逐 child partial、独立结果 oracle、completion events、每个 registration 的身份/status/owner/publications、对象 tombstones、脚本、atomic histories、vector clocks 和 plain accesses。每次 decrement、callback load、CAS、result lock/unlock 都独立执行。partial 写在各自 child 的独立槽中，不使用 result mutex；error/stopped 的 result 写由 mutex 保护。deliver 不持该 mutex，其 reads 的 HB 依赖 pending 的 RMW release chain。

## child 与 stop 库契约

每配置一至三个 children、一个 outer registration、一个内部 source 和一个 receiver。child 使用 [共享 inner operation](../engine/inner_operation.py)，只有一个 inplace token input，因此直接注册，不做 fused-token precheck。registration 可在 connect 或 start。五种模式为 start 内完成、自主线程完成、stop callback 内完成、通知后另线程完成、忽略 cancellation 后自主完成；payload 为 value/error/stopped。child 内部 readiness/notification 的同步属于合法 child 的抽象契约，各有独立 publication；不会代替 parent pending 的 HB。结果构造抛出的有限等价结果为 error，不模拟任意用户构造或析构代码、地址复用和分配器。

[共享 stop 契约](../engine/library_contracts.py) 提供原子注册、已停止时构造线程 inline invocation、pending removal 取消、跨线程注销等待和 self removal。内部 source 有多个 callbacks，stop publication、每次 selection、entry、return 和 request return 分开；selection 对全部 pending identities 分支。外部 token 只有一个 registration，采用共享契约的 publication/selection 线性化。其 source 由外部 fixture 覆盖整个调用。

outer callback 在任何 child start 前注册，仅当 stop_possible；构造抛出则 request_stop 内部 source。已停止 token 内联调用，取得 pending credit，转发 stop，再独立 child_done。pending 已归零时不能取得新 credit，否则 deliver 的注销等待会形成等待环。每次弱 CAS 可无写入地伪失败；相邻无进展的重复失败约简为一次，其他线程所有交错仍保留。不证明无界伪失败下的必然终止。

正常成员次序为 receiver、source、计数/结果、children、callback。析构先 reset callback，再逐 child 注销并结束 lifetime，然后 source、receiver、operation。tuple 子对象以逆身份顺序代表析构；本边界内各 child destructor 只触及自身和 source，互不借用 sibling，所以这些顺序可交换。member-order mutant 将空 optional 的初始化与析构一起移到 children 之前/之后；正常 delivery 已 reset 时，其空析构无共享访问。未 start operation 的注册也分别注销。receiver 可在 completion 内销毁整个 operation，之后只允许不访问 storage 的 stack return。child_fail/child_stop 的 lock_guard 先 unlock 再 child_done；callback 的 self/pending locals 和 values delivery 的引用 tuple 均按栈逆序销毁，其空析构无对象访问，合并为 stack continuation。

## 场景、性质与负对照

场景覆盖一至三个 children、同步/并发 values、两个 error 竞争、error 与 stopped、outer stop 在注册前和完成竞争中、callback 与最终 decrement/注销竞争、pre-start 通知、注册异常、不可停止 token、未 start 析构及三个 callback identities。内部 source 上的同步 child stop/completion 表示嵌套组合器的取消契约；不递归展开任意深度的 when_all。coverage 断言要求非零 credit、零 pending、伪失败、跨线程等待、inline registration、pre-start 通知和多个 identities 被选择。

- W1：started receiver 恰好一次，且在全部 child completion events 后；drop_done、drop_receiver、drop_completion 保留义务但省略履约。
- W2：首次 error 仲裁决定 error identity，后续 error 不替换；无 error 时 stopped 优先于 values。oracle 在 child 的 disposition arbitration 或 value event 独立记录，不读取实现 result。first_stored_error 另检查已存 error 永不更换；overwrite_error 提供反例。error storage 的完成时刻可以晚于 exchange，因此不能以函数入口的全序代替仲裁顺序。
- W3：error、stopped、注册异常要求内部 stop；outer callback 成功取得非零 credit 后必须转发。no_error_stop、no_stopped_stop、drop_throw_request、drop_outer_forward 各自省略履约。drop_stop/drop_notice 检查 child 的 committed notification 与 pre-start 保存。
- W4：delivery 前 outer registration 已注销；声明脚本内没有注销等待环。credit_without_check、no_deregister、deregister_after_deliver 提供反例。
- W5：operation、逐 child、source、receiver 和 callback 的 entry/lifetime 不越过 destruction；每个 registration 在其 storage end、source end 与 terminal 独立检查。逐 child skip controls 和逐 storage end controls 保留要求，分别失败。
- W6：result/partial 写 HB 于 deliver 读取；lifetime end 是 conflicting write。relaxed_pending 在 HB 模式失败，SC 的正确值不能代替 HB。

强式“公开 outer stop 发生时尚有 pending，必定到达内部 source”不成立：选择 callback 后，最后 child 可先归零，callback 随后必须退出；pending removal 也可以合法取消调用。`literal_outer_forward` 保留此强式为失败的规范负对照；baseline 使用成功取得非零 credit 的可实现保证。

十二个 mutants 都对全部场景的 sc/sc_hb 做 sweep，每个 killer 由 catalog 和 sensitivity 保存。三个 whole-sweep survivors 不能解释成可删除保护：

| Survivor | 独立检查的 covering protection | Combined control |
|---|---|---|
| stop_over_error | CAS 删除不产生 atomic write；mutex 下 result-index guard 保留已存 error | stop_without_result_guard 另删 result guard，W2 失败 |
| relaxed_credit | callback 的最终 seq_cst child_done 仍发布自己的 accesses，并续接既有 release sequence；W6 检查结果和 lifetime | credit_without_pending_release 另放松 decrement，HB 失败 |
| callback_before_children | delivery 已 reset callback，child destruction 的 covering invariant 要求 registration 已 removed/absent | member_without_deregister 另删 delivery reset，失败 |

上述 invariants 对 baseline、mutants 与 controls 使用同一 predicates。combined controls 有意删除第二层保护，不冒充单点源码变更。

## 内存与维护

`sc` 是 SC 交错；`sc_hb` 使用 [memory.py](../engine/memory.py) 检查 HB/plain races；`sc_stale` 允许 callback 的 acquire pending load 读取 HB-coherent 历史值。pending decrement 与 disposition RMW 是源码默认 seq_cst，在 SC 全序中按 acq_rel publication 表示；outer CAS 为 acq_rel，失败为 acquire，伪失败不写 MO。histories 分开保存 modification clock 与 release sequence；每个 handoff 只 acquire 自己的 publication，并传播 observed bounds。没有 synthetic atomic publications。

不增加 store-buffer modes：本边界的决定由 mutex/RMW 或 stop 库契约序列化，已保留真实 publications 与 coherent acquire stale choices；底层 stop 算法由独立模型覆盖。这不是完整 C++ axiomatic 或平台映射证明。baseline 全图探索；mutant/control 首反例停止。complete 对失败配置表示完成反例搜索。无环图的全部执行结束；completion reachability 不等于任意 fair schedule 的 liveness。

```bash
python3 -B formal/when_all/check.py --check-source
python3 -B formal/when_all/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/when_all/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/when_all/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/when_all/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

Python 3.9+、标准库、spawn workers 自行 configure。每配置 cap 250,000 states；incomplete 不通过。quick 包含关键 baseline、每个 mutant witness 和所有 controls。self-tests 检查逐对象 lifetime、无写入 CAS、spurious-zero 退出、faithful guard removal、覆盖矩阵、metadata/render 及共享 oracle AST regression。AST 只检查直接依赖 mut/control 的 protected-field assignments，不分析别名或间接依赖，仍需人工审查。

lock 是 drift detection。source digest 只规范 CRLF→LF，保留全部注释/空白。checker/engine/expected/sensitivity 改变须 full；只有 C++ 改变时，经明确 assessment 可 quick，并沿用上次 full 的 checker/expected digests。先手工编辑 sensitivity，再 update-expected，最后独立 full refresh-lock；事务拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 与空 assessment。semantic hash 包含配置、coverage、计数和 outcome，不包含 wall/RSS。资源统计随 source.lock 和 runner summary 维护；单 worker peak 不等于 pool peak。

## 最近一次完整检验

2026-11-16，Python 3.13.13，八 workers / spawn。baseline 全部通过；所有 mutant/control outcomes 匹配人工 sensitivity。全部配置完成且未达到 state cap。expected update 与独立 full refresh 的 semantic records 完全一致。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 480 | 8.606 | 55.76 | 416917 | 696272 |
| quick | 60 | 1.060 | 37.28 | 47389 | 78515 |

wall 包含 pool 启动，不含 CLI self-tests；worker peak 不是 pool peak，测量不能外推更大边界。
