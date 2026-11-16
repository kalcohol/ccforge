# associate 协议模型

本模型对 [associate.hpp](../../backport/cpp26/execution/associate.hpp) 的有限实例做显式状态穷举，检验 sender、operation、receiver 与 scope association 的生命周期。重点是 conditional union holder、sender copy/move/const-connect，以及 receiver inline 销毁 operation 时的返回路径。结论仅限声明的有限脚本和库契约，不是任意 C++ 程序的证明。

## 代码、成员与状态

`model.py` 保存转移、ghost obligations、source references 和内存状态；`scenarios.py` 保存脚本；`tiers.py` 保存 quick/full 矩阵；`mutants.py` 保存单点源码变化及全部 killers；`sensitivity.txt` 是人工维护的逐配置失败名单。source lock 覆盖整个 header，包括 closure、sender constructors、get_env、completion forwarding、union declaration、注释和空白。

```bash
python3 -B formal/associate/check.py --steps
```

函数 body 中唯一 fragment 定位协议动作。sender 两个 constructor 的 assignment 相同、move constructor body 为空、closure 的部分 forwarding bodies 相同，因此相应 member initialization/隐式 teardown 明确标为 model-only，不能用重复 fragment 随意选 overload；全文件字节 lock 仍覆盖这些部分。self-tests 对全部可执行 action 检查 metadata 并实际 render。

sender construction 先构造 token、空 association 和 wrapped sender，再 try_associate；copy 新建 association，可能失败；move 转移原 identity，source 成为空。sender 无 connect 就析构时释放自己的 identity。`connect() &&` 把 sender association 移到 operation；`connect() const&` 另外 try_associate，不消耗 sender 的 association。模型保存每次成功取得的独立 identity、所有 holders 和 release history，重复 release 和漏 release 不仅在实现 release transition 检查，也在 state/terminal 独立检查。

operation 的 association 是第一个成员，receiver 第二，anonymous union holder 最后。union 起初 inactive；constructor body 只有 associated 时 placement-construct holder，其 inner operation 是普通成员。receiver 和 association 的 construction 在同一线程、child connect 前合并，因为它们尚未公开；不模拟 receiver constructor 任意 reentry。关联失败时 union 保持 inactive，start 直接 set_stopped，不构造或 start inner。inner connect 抛出时 holder 从未完成构造，先展开 receiver，再展开 association，最后 operation storage end；不会伪造 child destructor。

正常 destructor body 显式销毁 active holder，先注销两个 child registrations，再结束 child lifetime；随后隐式 receiver destructor，再 association destructor，最后 storage end。receiver destructor 有访问 scope-lifetime data 或不访问的有限选项。association-last mutant 同时移动 initializer 和隐式析构次序：body 仍先销毁 holder，之后 association 先于 receiver 析构；connect 抛出时的展开顺序也反转。

completion 仅转发给 receiver，不释放 association。完成后的 operation 仍关联 scope，直到 operation destruction。release_at_completion mutant 保留原 destructor 的 association guard，因此提前清空 association 后不再销毁 holder；早释放本身已在第一处性质违例被检出，直接 self-test 另核对这条后续路径。receiver 可以 inline 销毁 operation；forwarding、start 和 inner callback 的剩余返回只保留 stack continuation，不再访问 operation、receiver 或 callback storage。脚本不并发调用非法 start/destructor，不在 callback constructor 返回前销毁它，也不从 receiver 递归 start 同一 operation。

## 共享 stop、scope 和 child 契约

[共享库模块](../engine/library_contracts.py) 为本模型和 [spawn](../spawn/README.md) 提供同一个 protocol-independent 实现；[inner operation](../engine/inner_operation.py) 保存有限 child 脚本。模块自身的 self-tests 检查库边界，spawn_future 的本地实现保持独立。

registration 可以在 connect 或 start。fused logical precheck 对两个 inputs 依次做 acquire query，允许 HB-coherent stale 值；实际注册随后原子检查最新 stop。pre-stopped callback 在注册线程 inline 执行，从未链接到 source，以后 removal 不再访问 source。pending removal 取消尚未选择的调用；跨线程 removal 等待自己的 invocation return；self removal 不等待。已 removed 的 return 可以留在 stack 上，不能当作仍有 live registration。每个 input、每次 trampoline entry（包括 loser）和 Fn entry 都分别检查所在 child/operation storage。

scope/environment 是两个独立 borrowed stop sources。每个 source 至多一个 registration、一个 requester；注册后无独立 query observer，因此 stop publication 与唯一 pending registration 的选择共享一个抽象线性化步骤。不同 inputs 的所有交错仍保留，未省略多个 callback identity 的排列。fused winner 产生 commitment，下一协议动作必须进入 Fn；loser 不等待。precheck/selection 独立建立 obligation，Fn 才 discharge。该 committed-delivery 边界由 [counting_scope](../counting_scope/README.md) 检验；底层 stop 内部算法由 [stop_token](../stop_token/README.md) 检验，本模型不复制它们。

child 有五种有限模式：start 内完成、自主线程稍后完成、stop callback 内完成、收到 stop 后稍后完成、忽略 cancellation 后自主完成。start 前 notification 必须保存，直到 start 与 registration constructors 返回才 dispatch completion。child 内部普通状态的同步是抽象 child 契约，不冒充对任意 child 的内存证明。value/error/stopped forwarding 都有实例；not-associated 的 stopped expectation 独立于实现的 start branch。receiver 的 completion obligation 在 public start 入口记录；child completion obligation 在 inner start 入口记录。

scope association 抽象表示 try_associate、scope decrement、join handoff 及 scope/source tombstones。每次成功 association 都有独立 identity，所有 identity 的 acquired/released 多重集在 terminal 检查。scope 内部 count/list/mutex 由 counting_scope 覆盖。scope source 必须覆盖 request_stop 整个调用；environment source 由外部 fixture 覆盖运行。外部 fixture 的 scope 可以继续存活，不要求每个场景都销毁它。

## 场景与性质

| 家族 | 有限内容 |
|---|---|
| a | 同步 error forwarding、独立 value completion |
| c | construction association 失败、const-connect 新 association 失败、stopped |
| d | sync/later/ignore stop、双 input 同步 stop，connect/start 注册期间的请求，五种 mode 的 pre-start notification |
| e | 最后 operation release 后 join/destruction，正常 request-return handoff |
| f | inner connect 抛出及 receiver/association unwind |
| r | receiver inline destruction，start 内同步、自主或 stop-callback completion |
| s | unused sender、copy、新 association 失败、move、const-connect、connected-unstarted destruction 后 stop |
| x | starter、producer、两个 requester、destroyer，最多五线程 |

每配置一个 operation、一个 receiver、一个 child、最多三个 sender identities、两个独立 registrations。每个 enabled 线程及每次合法 stale choice 均分支；source 内只有一个 registration，不需要 identity 对称约简。coverage assertions 防止 pre-start preservation、两个 fused winners、loser、cross-thread wait、同步/延迟完成及 join 路径静默消失。


`d_register_connect_0/1` 与 `d_register_start_0/1` 的 requester 不等待 child readiness，因此请求可交错于 precheck 和 registration。两个 inputs 分别要求 true/false precheck、pending/inline registration、pending selection，以及同一路径上的 false→pending、false→inline、true→inline。`sc_stale` 还要求多选 load 和同一 input 的 stale-false→inline。每个 query 的 observation 以独立 coverage ghost state 保留至该 input 注册，不参与 enabled、completion 或 ownership 判断；配对断言不会把不同执行的 query 与 registration 拼成一个 witness。`d_dual_sync` 同时保留两个 input 的 winner 和 loser。quick 也运行这些场景的全部三种模式。
`r_start_inline` 在 start 内同步完成并由 receiver 当场销毁 operation，断言同时覆盖 completion 与 self-destruction。

- AS1：每个 engaged identity 恰好 release 一次，包括 unused sender、copy、move、const-connect。const_reuses_association 和 move_leaves_source 揭示重复释放，skip_association 保留 acquired obligation 并省略 discharge。
- AS2：operation association 晚于 inner/逐项 registration/receiver destruction 释放。association_last、release_at_completion、release_before_inner、skip_inner_destructor 及独立 skip_input_0/1 controls 提供反例。
- AS3：public start 独立产生一次 receiver completion 要求；not-associated 必须 stopped，不能 construct/start inner。construct_unassociated、start_unassociated、wrong_stopped、drop_receiver、drop_completion 分别破坏这些要求。drop_stop/drop_notice 检查 committed dispatch 与 pre-start preservation。
- AS4：operation、inner、receiver、scope、scope stop source 各有 tombstone；inline self-destruction 后不再访问它们。receiver_after_destroy 独立插入末尾访问，立即失败；strict_request_lifetime 检查 source 仍在 request 内使用的边界。
- AS5：terminal 无 sender、operation、receiver、child、registration 或 association leak，覆盖 connect exception 和三处 association failure。drop_completion 和 skip_association 保留独立 obligation，不能只靠 teardown transition 自己检查。
- AS6：plain storage 与每次 lifetime end 无 HB race；每次 destruction 必须 happens-after 其他线程的相关 access。no_receiver_handoff 仅删除 starter→producer handoff，HB checker 在 storage access 检出；它不改变任何性质 predicate。

强式“最后 release 后的 join continuation 可以无条件立即销毁 scope”不成立。若最后 release 发生于 stop callback 内，request_stop 仍要访问 source；strict_request_lifetime 故意执行此 continuation 而失败。baseline 要求 scope 覆盖 request return，此限制与是否 inline 销毁 associate operation 是两件独立的事。完成本身也不承诺 scope drain：association 保留至 operation destruction。

## 内存与敏感性

`sc` 穷举 SC 交错；`sc_hb` 用 [memory.py](../engine/memory.py) 检查 ordinary storage 与 lifetime；`sc_stale` 保留 atomic history，并允许抽象 fused precheck acquire query 的 HB-coherent 旧值。header 自身没有 atomic fields。abstract scope mutex、registration publication、invocation return、script signal/await、join 各自使用独立 handoff；每个 acquire 只导入对应 publication 的 HB clock 和 observed MO bounds。

不增加 store-buffer modes：associate 的所有权转移是单线程 move 或 scope/stop 库契约中的序列化操作；真实 publications、coherent stale precheck 与 storage races 已显式保留。底层库的 weak-memory 实现由独立模型覆盖。这不是完整 C++ axiomatic proof，不能外推任意平台映射。无环 full graph 中所有执行完成；completion reachability 不是任意 fair schedule 的 liveness 证明。

八个 mutants 全部 sweep 三十一个场景的 sc/sc_hb，每个 killer 都保存在 catalog 和 sensitivity。当前八个都有 killer，不存在需要覆盖 invariant/combined control 的 survivor。member-order mutant 的 initializer 和异常展开也随成员移动；move mutant 只保留错误的 source engagement，不创建一个实际新 association。没有因为 mutant 改变而顺便修改 expected result 或 completion obligation。

`sensitivity.txt` 人工维护，工具从不改写。expected TSV 保存逐配置 states、transitions、terminal、outcome 和完整 semantic hash；计数不包含 wall/RSS。baseline 全图探索，mutant/control 在首个反例停止，失败配置的 complete 仅表示完成规定的反例搜索。self-tests 独立检查 sender move/identity、const rejection oracle、每个 registration 的 lifetime/terminal、callback entry、析构顺序、plain storage conflict、metadata/render，以及直接 oracle assignment 的 AST regression。AST regression 拒绝直接依赖 `C.mut/control` 或 `c.mut/control` 的 branch/RHS 对 protected fields 写入，不分析 alias/helper 或间接数据/控制依赖，完整 oracle fidelity 仍须人工检查。

## 使用与维护

```bash
python3 -B formal/associate/check.py --check-source
python3 -B formal/associate/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/associate/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/associate/check.py --tier full --only 'r_inline' --jobs 1
python3 -B formal/associate/check.py --tier full --jobs 8 --update-expected
python3 -B formal/associate/check.py --tier full --jobs 8 --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

Python 3.9+、仅标准库，workers 自行 import/configure，支持 spawn。quick 包括所有 SC baselines、关键 HB/stale 代表与全部注册竞争场景、每个 mutant witness 和全部 controls。每配置 cap 为 250,000 states，incomplete 不得通过或更新资产。默认至多八 workers，每 worker 预算 4 GiB；worker peak 不等于 pool peak。`--out` 可指定 raw JSONL、summary 和最短反例的输出目录。

whole-header lock 只转换 CRLF→LF，所有注释/空白编辑均须资格运行。lock 是 drift detection，不是 fidelity proof。engine/checker/expected/sensitivity 变化要求 full；只有 C++ 变化时，经明确对应关系 assessment 可用 quick，但须保留上次 full 的 checker/expected fingerprints。先手工调整 sensitivity，再 full update-expected，最后独立 full refresh-lock。事务拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 和空 assessment；update/refresh 不可同时使用。

matcher 的 anonymous union 扩展只接受 nonlocal class body 内的匿名 data-member union，成员类型是 named/qualified/template type，每项只有一个 identifier 与分号。instance declarator、methods、nested types、initializers、空 union、namespace/function-local union 和一般匿名 class/struct 都拒绝。已有三个 headers 的完整 analysis structures 与所有 step resolutions 保持不变，由独立 hashes 对照检查。



## 最近一次完整检验

2026-11-16，Python 3.13.13，八 workers / spawn。baseline 全部通过；mutants 与 controls 的每项 outcome 均匹配人工 sensitivity。全部配置完成，未达到 state cap。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 601 | 7.400 | 27.02 | 48418 | 77616 |
| quick | 79 | 1.007 | 26.64 | 8061 | 13567 |

full 包含 93 个 baseline、496 个 mutant 配置和 12 个 controls；376 项通过、225 项预期反例。最大配置 1232 states。上述 wall 包含 pool 启动；单 worker peak 不能当作 pool 总峰值，测量不能外推更大边界。
