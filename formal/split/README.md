# split 协议模型

本模型对 [split.hpp](../../backport/cpp26/execution/split.hpp) 做有界显式状态穷举，检验惰性单次执行、缓存结果广播、共享引用计数和 receiver 内联自毁。结论限于声明的有限脚本与库契约，不是任意 sender 或完整 C++ weak-memory 证明。`forge::split` 共用该实现；模型只锁定此头文件。

## 源码、状态与定位

`model.py` 保存协议转移、独立义务和内存状态，`scenarios.py` 定义脚本，`tiers.py` 定义 quick/full，`mutants.py` 保存单点变更和完整 killer 清单。`sensitivity.txt` 人工维护逐配置失败声明，未列出的配置必须通过；工具从不改写它。`expected/results.tsv` 保存逐配置 semantic records。

```bash
python3 -B formal/split/check.py --steps
```

step 通过 enclosing function 内唯一 fragment 定位。三种 inner completion、sender 的两种 connect 和 allocator 选择另有 source references。constructor mem-initializers、成员声明、隐式析构及 `deliver_result` 的 visit/apply lambda 明确为 model-only，不能以附近 fragment 冒充这些动作的定位。whole-file lock 覆盖整个 header 的类型计算、forwarding、初始化、注释和空白；fragment 存在不能证明对应关系。

状态保存每个对象的引用数、具名引用图、allocation/storage tombstones、析构与 deallocate 计数；每个 operation 和 receiver 也有独立 lifetime 与结束计数。另有 phase/result、on_done/tail/next、mutex、线程栈脚本、独立 start/source-event oracle、每个 receiver 的 history、active histories、vector clocks 和普通访问记录。真实链表身份和每个 next 引用均保留，不以无身份的 callback 数量代替链表。

## 共享所有权、分配与析构

采用最弱的常见 `shared_ptr` 契约：copy 是 relaxed increment，release 是独立的 acq_rel decrement；最后一次 decrement 在自己的线程上先析构对象，再 deallocate。每个对象的 count 独立对照引用图；移动只转移引用并将来源置空，不写 atomic history。`shared_from_this` 也是 relaxed increment，要求已有非零 live strong count，不能 resurrect。未公开对象的初始引用在构造中建立，不伪造 release publication。

state 的引用来自 sender copies、每个 `__op::__shared`、start 的 sh、running keepalive、notify 的 pin 和 completion 的 st。node 的引用来自 `__op::__sub`、on_done/next、start 的 sub，以及 notify 的 callbacks/next。shared_ptr assignment 分离 increment、指针转移和旧引用 release；每个 decrement 可与其他线程交错。没有把两个释放合成一步。

`__op` 先构造 shared 成员，再用 state 的 allocator 分配 node；allocator read 必须由实际 state 引用覆盖。`connect() &&` 先将 sender 引用移入参数，再移入 shared 成员；node allocation 失败时展开该成员，sender 已空。`connect() const&` 复制引用，失败展开后 sender 仍持有原引用。source connect 失败不产生完整 state，私有 construction unwind 和 storage deallocation 合并，不伪造完整 state destructor。默认 allocator 与 source attributes allocator 采用同一所选 allocator 身份，state 和每个成功分配的 node 均要求恰好 deallocate 一次。

operation destructor 先 release-store active=false，再按成员逆序分别释放 sub、shared，最后结束 operation storage。start 先复制 sh 再复制 sub；返回时先结束 lock，再释放 sub、sh。即使 source 同步完成或 done path 内联完成并销毁自己的 operation，start 也不再访问 operation 成员。

notify 先移动 on_done 到 callbacks，再清 tail，再把 keepalive 移到 pin。遍历每个 node 时先将其 next 移到栈上，然后调用 deliver；callbacks assignment 先接入新引用，再单独释放旧 node。next 被移动后为空；notify 返回时 pin 先于空 callbacks 析构，最后 completion 释放 st。state 的反向成员析构先结束 child operation，再结束 allocator、空 keepalive/list 与 result/phase/mutex。node 先结束 receiver，再释放 base.next，最后结束 node storage 并 deallocate。每个 storage end 和 terminal 都有独立检查。

allocate_shared 的合并 control block 与对象用一个 allocation identity 表示。enable_shared_from_this 的内部 weak 引用在 state 析构时消失；有限脚本没有外部 weak_ptr，因此最后 strong release 完成析构后即可 deallocate，不存在可观察的额外 weak lifetime。模型不模拟 allocator 任意用户代码、ABI、地址复用、实际字节或抛出 destructor。

## mutex、source 与 receiver 契约

三个临界区分别是 start 的 done check/link/idle→running/keepalive、completion 的 result store/done，以及 notify 的 detach。lock acquire、各普通字段访问、引用操作和 unlock 均分开；去锁 mutant 不保留由锁才成立的合并。unlock 是对 mutex 的最后访问。`deliver_result` 在 mutex 外读取 result，HB 必须来自对应 mutex acquire 或同线程程序顺序，不额外导入 completion 的 publication。

active 在两个 delivery 路径中都是 acq_rel exchange，destructor 中只有 release store。没有普通 atomic loads。mutex、脚本 signal/await 与 refcount release sequences 各自携带 clock 和 observed modification-order bounds；每次 acquire 只导入自己的 publication，不增加 synthetic atomic writes。

inner receiver 的 environment 为空，source 没有 stop input。共享 inner helper 固定从 input 0 查询，所以这里使用本地零 stop 脚本，不修改 engine。有限 source 在 start 内同步完成，或 readiness handoff 后在自己的线程上完成；value/error/stopped 及 value caching 抛出均有实例。抛出时缓存 error。expected 在独立 source completion event 记录，不从实现 result 或 delivery branch 推导；drop_completion 仍保留该 event 和义务。source 调用 completion 后只允许 stack return，不能再访问其 operation storage；最后 state release 因而可以在 inner completion 内销毁 source operation。

receiver actions 包括在 notify、done replay 和同步 source 路径中内联销毁 operation；内联 connect/start 同一 state 的另一个 operation；丢弃最后 sender copy；或返回后由另一线程销毁 operation。completion call 的整个区间保留缓存 const 引用，并在入口、返回和 storage destruction 处检查 state/node/receiver 寿命。递归深度至多一层新订阅，不模拟任意 receiver 程序或重复 start/非法重复 source completion。

## 场景、性质与负对照

| 家族 | 有限内容 |
|---|---|
| a | 同步/自主完成的四种结果、未 start 析构 |
| b | 两个并发 starter 加一个顺序启动的 operation，三个节点在完成前全部入链 |
| c | start 与 completion store/detach 交错，完成前、两临界区之间和 detach 后 |
| d | done 后内联重放、两个并发 late starts |
| e | 三种完成路径上的自毁、再次订阅和最后 sender release |
| f | copy/move、sender/operation 跨线程释放、完成线程或其他线程最后析构 |
| g | source connect 失败、两种 connect 的 node allocation 失败 |
| h | 声明的 abandonment 扩展：keepalive 单独持有、析构与 exchange 竞争 |
| x | 五个线程身份：同步 source、两个 late starts、两个独立 sender releasers |

full 有 27 个场景，每个 baseline 和每个 mutant 均在 sc/sc_hb 运行。各场景内所有 enabled 线程都分支，没有固定 subscriber identity 顺序。b_three 的两条 start 线程中有一条依次启动两个 operation，b_before 在全部 start 返回后才完成，结合 b_two 检验三个不同的 source launcher 身份；两个并发 starter 加顺序第三个 operation 是明确上限，不是对任意并发的等价证明。x_five 在同步 source 返回后开放 late starts，sender releases 等待各自 operation 析构；这也是受 gate 限制的五线程边界，不是任意五线程交错的等价证明。

coverage 断言要求三个 launcher 身份、detach 三节点、start 在 source event 前后、late delivery 在 detach 前后、同步/自主 completion、keepalive-only entry、completing-thread state/receiver teardown，以及 destructor/exchange 竞争的 delivery 与 suppression。三个 e_subscribe 场景分别要求外层 notify、同步 source 或 late call 的自毁与再订阅，另要求 nested late call 的自毁、对应的 reentry 组合与外层 sender release。subscribe.after_store 仅在 result 已实际存储时记录。场景同步只表达 fixture 的公开顺序，不代替协议内部的 mutex publication。

- SP1：有 public start 当且仅当 source 恰好 start 一次，之前不能 start。no_running 和 drop_start 分别检验重复与遗漏；unused/failure scripts 保留零启动边界。
- SP2：每个 started receiver 恰好得到一次独立 source event 要求的缓存结果，合法 abandonment 抑制除外；不丢失与 detach 竞争的 start。link_after_unlock、phase_before_lock、done_unlocked、no_catch 及 drop_completion/store/receiver 检验顺序、结果和独立义务。
- SP3：result write HB 于每次读，phase/result/list/tail/keepalive 及 lifetime end 无 race。phase_before_lock 和 relaxed_release 检验无锁字段与逐对象 release 的同步。SC 结果正确不能替代 HB。
- SP4：state/source/allocator 恰好结束一次，最后引用才析构，source 运行、notify walk 或 receiver call 中不能提前结束；shared_from_this 总有 live strong 引用；析构时 list 为空。no_keepalive、member_start、pin_cover_removed 和逐 state/source/allocator end controls 提供反例。
- SP5：每个 node/receiver 在最后访问后恰好析构一次，包含内联 operation 自毁；每个身份独立核对。member_start、逐 node/receiver end controls 和各引用 release controls 提供反例。
- SP6：operation 自毁以后没有成员访问，storage 恰好结束一次。after_op_access 和逐 operation end controls 检验此性质；正常 start 的剩余返回只有 stack/local references。
- SP7：所有有限路径 terminal 无 allocation、引用、keepalive 或 storage leak，包括三种 connect failure 和 caching failure。retain_keepalive、逐 deallocate 和逐 release controls 省略履约，义务仍存在。
- SP8：同步 source、receiver 再订阅不重复获取本线程持有的 mutex，无等待环或 unfinished script。source_locked、notify_in_store、deliver_locked、late_locked 分别提供反例。

started operation 在 delivery 前析构超出标准 operation-state 契约，h 家族把它明确作为扩展检验。split 自己的存储由 list 和 keepalive 保持有效，source 继续运行到完成，然后回收；destructor store 在 modification order 中先于 exchange 时抑制调用，receiver 随 node 释放，可能在完成线程析构。独立 suppression 义务记录于 destructor 的 store 位置，exchange 检查它，不等待 destructor 返回，也不从实际 active 值推导期望。store_interval 将 exchange 限制在 store 之后、成员释放与 op_end 之前，正常 suppression 通过，省略 store 的配对 control 在该区间失败。若 exchange 已先赢得 active，destructor entry 之后仍可进入 receiver，不能把它解释成 destructor 与 receiver call 原子互斥。`literal_abandonment` 保留更强期限并失败；`no_active_store` 破坏可实现的 suppression。外部资源仍须满足 receiver 自身的并发契约。

never-completing source 不在有限脚本内：只要已启动且尚未完成，keepalive 会持续 pin state；abandonment 不请求 source stop，也不保证回收永不完成的 source。

十五个单点 mutants 全部对所有场景 sweep，完整 killers 在 catalog 与人工 sensitivity 中逐项保存。两个 whole-sweep survivors 不意味着保护可以删除：

| Survivor | 独立 covering invariant | Combined control |
|---|---|---|
| detach_unlocked | mutex 下的 done gate 保证 phase=done 时没有尚在入链的 start；此前写入通过 completion 的 store mutex acquire 导入 | detach_without_phase_guard 另外将 done check 移到 start lock 前：SC 必须到达 done-gate 不变量失败；SC-HB 首先发现 phase race，不是 detached-list race 的证据 |
| no_completion_pin | notify walk 和 receiver call 全程有实际 state owner，call 另有实际 node owner；operation、sender、local 或 keepalive 均可，只要该引用仍存活 | pin_cover_removed 另外在 notify detach 后提前释放 moved pin，keepalive-only 场景失去 state 寿命保护；late_external_destroy 的 receiver 保留 operation 至 start 返回，member_start 配对 control 通过 |

combined controls 有意删除第二层保护，不冒充单点源码变更。所有 covering predicates 对 baseline、mutants 与 controls 相同；两个 survivor 的源码保护均保留。source_locked 和 deliver_locked 只移动调用相对 unlock 的顺序；link_after_unlock 移动三行 link 到 start 解锁后、source start 前，idle/running/keepalive 决策仍在锁内；no_running 保留 shared_ptr assignment 替换旧 keepalive 的独立 decrement，不制造虚假重复引用。retain_keepalive 同时省略 move assignment 及其 keepalive 普通访问。done_unlocked、notify_in_store 和 no_completion_pin 各自只改动 `set_value`，error/stopped 的独立函数保持原样；这不是分别编写的 error/stopped completions 的 mutation coverage。no_completion_pin 使用 raw pointer 代替 st，并删除与该 local 对应的 increment/release。

## 内存与维护

`sc` 穷举 SC 交错，`sc_hb` 使用 [memory.py](../engine/memory.py) 检查 HB。这里的源码和抽象共享引用契约只有 RMW 与 store，没有可选旧值的 plain atomic loads，所以不增加 stale tier。mutex 决策和 active/refcount RMW 串行化，不增加 store-buffer modes；这不是完整 C++ axiomatic 或平台映射证明。初始构造在公开前；失败构造的无并发私有展开按上述边界合并。

baseline 探索全图；mutants/controls 在首反例停止，失败配置的 complete 表示完成规定的反例搜索。无环图的所有 execution 结束；completion reachability 不等于任意 fair schedule 的 liveness。每配置 cap 250,000 states，incomplete 不能通过或更新资产。Python 3.9+、仅标准库，入口禁止 bytecode，spawn workers 自行 import/configure。资源预算为八 workers、每 worker 至多 4 GiB，quick 约 60 s、full 约 15 min 以内。

```bash
python3 -B formal/split/check.py --check-source
python3 -B formal/split/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/split/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/split/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/split/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

self-tests 检查完整 sweep、quick witnesses、每个对象与引用、真实 memory orders、move 无写入、失败展开、member/local teardown 顺序、metadata/render 和共享 oracle AST regression。protected oracle fields 的直接 assignments 不能位于直接依赖 mut/control 的分支或 RHS；AST 不分析 alias/helper/setattr 或间接依赖，不能代替人工语义核对。obligation omission controls 不改变性质 predicates。

source digest 仅 CRLF→LF，保留所有注释/空白。lock 是 drift detection，不能替代对应关系核对。checker/engine/expected/sensitivity 改动要求 full；仅 C++ 改动经明确 assessment 可 quick，保留上次 full checker/expected digests。先人工维护 sensitivity，再独立 update-expected，最后独立 full refresh-lock；事务拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 和空 assessment。semantic hash 含配置、coverage、计数和 outcome，不含 wall/RSS。自动 discovery 将本模型纳入文档审计和 verification floor。

## 最近一次完整检验

2026-11-17，Python 3.13.13，八 workers / spawn。54 个 baseline 全部通过；810 个 mutant 配置中 171 个反例，两个 whole-sweep survivors 均有失败的组合 control。39 个 controls 中 34 个失败，正常 reentry、两种模式的 benign late delivery 和正常 store interval 共五个通过。全部 903 配置完成且未达到 cap，698 个通过、205 个按声明失败；expected update 与独立 full refresh 的 semantic records 完全一致。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 903 | 41.621 | 311.84 | 4820078 | 9664218 |
| quick | 83 | 6.952 | 302.25 | 822540 | 1468144 |

最大配置 124,647 states。wall 含 pool 启动，不含 CLI self-tests；worker peak 不等于 pool peak，测量不能外推更大边界。
