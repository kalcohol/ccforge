# sync_wait 协议模型

本模型对 [sync_wait.hpp](../../backport/cpp26/execution/sync_wait.hpp) 的 receiver 和 driver 做有界显式状态穷举，检验 result publication、inline/跨线程 completion 与 stack teardown。结论仅限有限脚本与下面的库契约，不是任意 sender 或完整 C++ weak-memory 证明。

## 源码与状态

整个 header 是唯一 locked source，包括 environment queries、结果类型选择、Mandates、domain dispatch、forwarding 和注释。`model.py` 保存转移、独立 oracle 与 tombstones；`scenarios.py` 保存脚本；`tiers.py` 保存 quick/full；`mutants.py` 保存单点变更与全部 killers；`sensitivity.txt` 人工声明失败；`expected/results.tsv` 保存 semantic records。

```bash
python3 -B formal/sync_wait/check.py --steps
```

step table 以 enclosing function 内唯一 fragment 定位 receiver 的 value/error store 和三种 finish 和 driver connect/start/run/read。隐式 stack destruction、child 与 run_loop 库内部明确是 model-only。whole-file lock 覆盖无法独立引用的声明与成员；fragment 存在不能证明 fidelity。

状态有 loop/state/op/child tombstones、result、store count、独立 expected payload、public-start obligation、finish publication、run-return flag、抽象 queued task、脚本、vector clocks 和 plain access records。expected 在 public start 入口记录，与改变 store/read 顺序的 mutant branches 无关。

## 导入的库契约

[run_loop 模型](../run_loop/README.md) 检查的契约作为抽象边界导入：push/finish 受 mutex 保护；run 只在 finish 且队列为空后返回；每个合法 pushed task 恰好在 run thread 执行一次；push/finish 通过 mutex HB 到 run 的后续步骤；finish 最后的 loop access 是 unlock。这里不复制 queue、mutex 或 CV 算法。

自主 completion 在另线程执行；driver 的 run 在 finish_unlock 前阻塞，之后只 acquire 该 finish publication。scheduled_value 抽象提交一个任务，run acquire 该 push publication，恰好 dispatch 一次，再按同一 finish 契约返回。它不允许 finish 后的新提交，继承 run_loop 的 submission/lifetime 边界。等待期间的交错由 run 的 disabled 状态表示，不再展开底层 spurious wakeups。

child 使用 [共享 inner operation](../engine/inner_operation.py) 的 start/readiness/completion 脚本。sync_wait env 无 stoppable token，因此 registration 为 none；不构造虚假 callbacks。child 内部同步是合法 sender 的抽象契约，不模拟任意 sender 实现。start 可 inline 完成，或发布 readiness 后在另线程完成；producer 与 starter 的 handoff 仅覆盖 child startup，不能代替 receiver finish 的 result publication。

set_value 在 optional 中直接构造 tuple，set_error 存 exception_ptr；set_stopped 直接 finish，result 保持 disengaged。模型以单个结果状态表示这两个互斥的字段。driver 的返回值构造先于 teardown。receiver 经 state 指针调用其 loop 的 finish；finish_unlock 以后只执行 stack return。value 构造失败按 error outcome 约简，error 的 exception_ptr/error_code/其他 error 转换按相同最终 exception outcome 约简；不模拟任意用户构造、抛出 destructor 或重复完成的非法 sender。

driver 构造含 loop、error、result 的 state，再构造 op，执行 start/run/read，随后依次销毁 op、result、error、loop；error 的 rethrow 与 value/stopped 返回共享该 unwind 次序。construction 在尚未公开对象的 driver 上合并，loop_after_state mutant 同时改为 state/loop/op 初始化与 op/loop/state 析构。payload destructor 可借用 loop，value_destructor 保留这个有限合法行为，因此该声明次序 mutant 有实际 witness。op destructor 也可借用 state/loop。跨线程 receiver 的 finish call 还未从 stack 返回时，driver 已可完成全部析构。

## 性质、场景与敏感性

- SW1：result write HB 于 driver read，read 看到 public start 独立要求的 payload，且在 run-return 后。finish_before_store、read_before_run 与 no_finish_handoff 分别破坏顺序或 HB；正确 SC 值不能代替 HB。
- SW2：state、loop、op、child 在 return/unwind destruction 后不再访问，receiver 的最后对象访问是 finish。loop_after_state、after_finish_state/op/loop 及逐 storage-end controls 各自提供反例。self-tests 还直接检查每个 lifetime end 的 conflicting write。
- SW3：public start 要求 value/error 完成恰好一次 result store，stopped 无 store；drop_store 保留义务但跳过履约，double_store 重复写，独立 terminal/state predicates 均检测。drop_finish 保留 run 完成要求并导致 unfinished terminal。

八个场景覆盖 inline/另线程的 value、error、stopped，payload destructor 借用 loop，以及 run thread 上的抽象 scheduled completion。coverage 断言要求 inline/own completion、run-return 和 task dispatch。三种单点 mutants 全部对所有场景的 sc/sc_hb 做 sweep；每个都有 killer，没有 whole-sweep survivor。finish_before_store 在 inline completion 的配置仍可能通过，因为 driver 尚未进入 run；stopped 的两个场景没有 store 可重排。所有 passing 配置也必须匹配人工 sensitivity 和 expected。

## 内存与维护

`sc` 是 SC 交错；`sc_hb` 用 [memory.py](../engine/memory.py) 检查 result、逐 storage 与 lifetime end。`sc_stale` 保留同一库同步边界，但本 header 没有可选旧值的 atomic loads，因此没有额外 source-level stale choice。每个 script、push 与 finish handoff 分开携带 HB clocks 和 observed MO bounds，只有对应 acquire 能导入。没有 synthetic atomic publications。不增加 store-buffer modes：本协议靠 run_loop 的 mutex publication，底层算法由其独立模型覆盖。这不是完整 C++ axiomatic 或平台映射证明。

baseline 全图探索；mutant/control 在首反例停止，失败配置的 complete 表示完成规定的反例搜索。无环图全部执行结束；completion reachability 不等于任意 fair schedule 的 liveness。每配置 cap 为 250,000 states，incomplete 不通过。Python 3.9+、标准库，spawn workers 自行 configure。

```bash
python3 -B formal/sync_wait/check.py --check-source
python3 -B formal/sync_wait/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/sync_wait/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/sync_wait/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/sync_wait/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

quick 包含关键 baseline、每个 mutant witness 与全部 controls。self-tests 检查 independent obligations、逐对象 lifetime、finish stack return、全 sweep 矩阵、metadata/render 和共享 oracle AST regression；AST 只检查直接依赖 mut/control 的 protected-field assignments，不分析别名或间接依赖，仍须人工审查。

source digest 只转换 CRLF→LF，保留注释/空白。lock 是 drift detection；checker/engine/expected/sensitivity 改动须 full，只有 C++ 改动时经明确 assessment 可 quick，保持上次 full 的 checker/expected digests。先手工编辑 sensitivity，再 update-expected，最后独立 full refresh-lock；拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 与空 assessment。semantic hash 含配置、coverage、计数和 outcome，不含 wall/RSS。资源数据随 source.lock 与 summary 维护，单 worker peak 不等于 pool peak。

## 最近一次完整检验

2026-11-19，Python 3.13.13，四 workers / spawn，full 共 82 个配置、1,580 states 与
1,697 transitions。50 个配置通过，32 个配置给出预期反例。所有配置完成且未达到 state cap。
expected update 与独立 full refresh 的 semantic records 完全一致。
