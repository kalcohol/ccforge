# run_loop 协议模型

本模型对 [run_loop.hpp](../../backport/cpp26/execution/run_loop.hpp) 的有限实例做显式状态穷举，检验 queue、predicate wait、任务计数和最后 unlock 后的 teardown。结论只适用于声明的提交与 lifetime 前置条件，不是完整 C++ weak-memory 证明。

## 源码与状态

整个 header 是唯一 locked source，包含 scheduler/env forwarding、operation 的 execute lambda、成员、注释与空白。`model.py` 保存转移与独立性质；`scenarios.py` 保存脚本；`tiers.py` 保存 quick/full；`mutants.py` 保存单点变化及全部 killers；`sensitivity.txt` 为人工逐配置声明；`expected/results.tsv` 保存 semantic records。

```bash
python3 -B formal/run_loop/check.py --steps
```

可引用动作以函数内唯一 fragment 定位。execute lambda body 不作为 constructor 的 fragment，token query 与 completion 明确是 model-only 源码对应；隐式析构、mutex/CV 契约和脚本同样明确标记。whole-file lock 覆盖这些部分；fragment 存在不能证明 fidelity。

状态有 loop/task tombstones、逻辑 FIFO queue、count、finishing、mutex owner/publication、waiter/notified、独立 pushed/executed/completed identities、active execution、run-return flag、stop histories、vector clocks 与 plain accesses。多个 pushers、一个 run thread、任意线程 finish；每个有限任务仅提交一次。

## mutex、CV 与 task 契约

push 的 link、enqueue、count increment、notify_one 依次在 mutex 内，随后独立 unlock。pop 持同一 mutex 检查 head/finishing；predicate 与 wait 的 unlock/arming 之间没有可让 notifier 越过的空隙。wait 允许 spurious wake，重新加锁后重检 predicate。finish 在锁内设 finishing 并 notify_all，最后独立 unlock。notify_outside mutants 只把对应 notify 移到 unlock 后，不删除其他步骤。finish_unlocked 只删除该锁；finishing 写、notify 与返回仍分开，未保留靠锁才成立的整体合并。

相邻无进展 spurious returns 约简为一次；每次新 pop 重新开放该选择。任意重复无效唤醒不改变 queue/finish，其他线程可在保留的等待前后交错，所以该约简保留有限 safety witnesses；不据此宣称无界 spurious 行为必然终止。一个 run waiter 下 notify_one/all 的目标没有 identity 歧义。

pop 摘出任务后 unlock，再执行任务。execute 首先 acquire 查询 receiver token，按观察结果发送 value 或 stopped；coherent stale 模式保留未被 HB 排除的旧值。completion 可以 inline 销毁 operation，或 finish loop；execute 返回仅使用 stack identity，之后不访问 task。run 再通过独立 count_lock/decrement/unlock 扣减 count，最后继续 pop。所有 decrements 独立执行，不能与 completion 合并。

queue 采用逻辑 FIFO 抽象，不保存或检验 head/tail/next 指针表示。link 步骤保留节点访问与 queue 写入的 plain-access 检查；enqueue/take 用序列追加与摘首表示源码锁内的链表操作。这些访问由 mutex 排斥，不合并 atomic token loads。初始 operation 与 loop 在启动脚本前已经构造。没有任意用户代码、重复 start、多个 run threads、地址复用或抛出 completion。

## 提交边界、性质与负对照

baseline 在 run 最终观察 finishing 且空队列之前关闭提交：所有预定 pushes 已完成 enqueue，且 mutex 使其剩余 count/notify/unlock 先于 run 的后续临界区。run 返回后可以销毁 loop，此时其他 push/finish 调用仅可剩余不访问 loop 的 stack return。coverage 显式要求 push 和 finish 的这些 tails 在 loop destruction 后运行。j_finish_push_before_run 只让 run 等待两次 enqueue，不限制 finish 与第二次 push 的锁顺序；coverage 同时断言 `enqueue.1.before_finish` 与 `enqueue.1.after_finish`，允许 finish 先于尚未提交的任务，又保持最终空 pop 前关闭提交。

header 的 push 不拒绝 finish 以后的提交。强式“任意晚到 push 都执行，run 返回后仍可无条件销毁 loop”不成立。`literal_late_push` 在 finish/run-return 后才开始 push，故意违反上述前置条件并失败。不能把这个规范负对照登记成 baseline failure。

- R1：每个 pushed identity 在指定 run thread 恰好 execute/complete 一次；terminal 对照初始独立提交义务，不依赖 push 或 execute 自己产生要求。drop_push 完全省略 link、enqueue、pushed identity 与 count increment，保持初始义务，由 lost push obligation 检出；drop_complete、wrong_kind 与 skip_task_end 分别省略 completion 或破坏 token outcome/lifetime。
- R2：run 仅在 finishing 且 queue 为空时返回，正常 terminal 必须返回；wait_no_predicate 与 drop_return 提供反例。
- R3：waiting 后的 push/finish 允许继续，无丢失唤醒；unfinished terminal 独立检查，pop_ignore_finish 暴露永远等待。c_wait 的 coverage 同时要求 spurious 与 notified wake；d_stop 还要求 value/stopped 两种结果及 stale 模式中同一次 query 的多个合法历史选择。
- R4：最后 unlock 是 push/finish 对 loop 的最后访问；run-return 后、允许的 stack tails 期间销毁 loop 无越界。两个 notify_outside mutants 及 finish_unlocked 被 teardown 路径杀死，skip_loop_end 则保留 terminal lifetime 要求。
- R5：destructor 在源码 count 非零时 terminate；独立 outstanding/active identities 检查不能过早把 count 清零。h_early 在未执行任务时、i_early_execute 在 completion 已发送但 execute 尚未返回时探测；strict abort 是成功检测。decrement_before_execute 与 push_no_count 提供反例，正常空 loop 不得 terminate。
- R6：逻辑 queue、节点访问、finishing、count 和 lifetime end 的 plain accesses 无 HB race；finish_unlocked 与 notify_outside 的 HB 配置提供反例。此性质不检验链表指针表示不变量。

十个场景包括双 pushers、finish/push/pop 竞争、提交关闭前 finish 与 push 的两种锁顺序、空 loop、spurious wake、独立线程的 stop 竞争、completion 内 finish，以及两种 strict destruction。七个 mutants 全部对每个场景的 sc/sc_hb 做 sweep；每个都有 killer，没有 whole-sweep survivor。全部 killers 由 catalog 与 sensitivity 保留，passing strict probes 不当作 mutant kill。

## 内存、组合契约与维护

`sc` 穷举 SC 交错，`sc_hb` 使用 [memory.py](../engine/memory.py) 检查 plain fields 和 lifetime；`sc_stale` 为任务 token 的 acquire load 保留 HB-coherent histories。mutex unlock publication 与每个脚本 signal 分开，acquire 仅导入相应 HB clock 和 observed MO bounds。任务计数是源码 mutex 下的普通字段，不伪装成 atomic。不增加 store-buffer modes：queue/finish/count 已由 mutex 序列化，token 的 coherent stale choice 保留；底层 stop 由独立模型覆盖。这不是完整 C++ axiomatic 或平台映射证明。

本模型为 sync_wait 提供抽象契约：push/finish 受 mutex 保护；run 只在 finish 且空队列后返回；合法提交任务恰好在 run thread 执行一次；push/finish 经 mutex HB 到 run 的后续步骤；finish 的最后 loop access 为 unlock。此契约继承提交与 lifetime 前置条件，不允许任意 run-return 后的新提交。

```bash
python3 -B formal/run_loop/check.py --check-source
python3 -B formal/run_loop/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/run_loop/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/run_loop/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/run_loop/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

Python 3.9+、标准库、spawn workers 自行 configure；每配置 cap 250,000 states，incomplete 不通过。baseline 全图探索，mutant/control 首反例停止；失败配置的 complete 只表示完成反例搜索。无环图的全部执行结束；completion reachability 不是任意 fair schedule 的 liveness。quick 包含关键 baseline、每个 mutant witness 与所有 controls。self-tests 检查独立 obligations、逐 task lifetime、stack tail、metadata/render、全 sweep 矩阵与共享 oracle AST regression；后者不分析别名或间接依赖。

source digest 仅转换 CRLF→LF，所有编辑均须资格运行。lock 是 drift detection，不能替代人工对应关系审查。checker/engine/expected/sensitivity 改动须 full；只有 C++ 改动时经 assessment 可 quick，并保持上次 full checker/expected digests。先手工维护 sensitivity，再 update-expected，最后独立 full refresh-lock；拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 和空 assessment。semantic hash 含配置、coverage、计数和 outcome，不含 wall/RSS。资源数据随 lock 与 summary 维护；worker peak 不等于 pool peak。

## 最近一次完整检验

2026-11-16，Python 3.13.13，八 workers / spawn。baseline 全部通过；所有 mutant/control outcomes 匹配人工 sensitivity。全部配置完成且未达到 state cap。expected update 与独立 full refresh 的 semantic records 完全一致。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 177 | 3.842 | 98.13 | 409938 | 852606 |
| quick | 42 | 1.967 | 99.11 | 121135 | 254841 |

wall 包含 pool 启动，不含 CLI self-tests；worker peak 不是 pool peak，测量不能外推更大边界。
