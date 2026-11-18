# spawn 协议模型

本模型对 [spawn.hpp](../../backport/cpp26/execution/spawn.hpp) 的有限实例做显式状态穷举，检查 detached operation 的两个 reference、同步完成后的 launch continuation、allocator 与 association 的析构顺序。结论限于下面的库契约和有限脚本，不能代替 C++ 测试或完整 weak-memory 证明。

## 源码与状态

`model.py` 镜像 `__state` 的构造、`__run`、`__complete` 和 `__release`，并保留 `__launch` 的异常回收路径。whole-header lock 也覆盖 `__launch_with_env`、`__spawn_wrapped`、两个 public overload、receiver forwarding、成员声明及所有注释。`scenarios.py` 保存线程脚本，`tiers.py` 保存矩阵，`mutants.py` 保存单点变更与全部 killers，`sensitivity.txt` 保存人工逐配置失败声明。

state 包含 allocation/object/child tombstones、实际 reference count 与独立 owner 集、launch/completion release 计数、completion obligation、moved-out association、allocator local lifetime、两个独立 callback records、scope association identities、线程脚本、atomic histories、vector clocks 和 storage access records。每个 atomic decrement 独立执行；模型不把两个 releases 合并。

```bash
python3 -B formal/spawn/check.py --steps
```

函数内唯一 fragment 定位每个可引用的源码动作；空 constructor body 的初始化、隐式析构与库脚本明确标为 model-only。member declarations 和 initializer lists 由全文件字节指纹锁定，不能把附近的 fragment 当成这些声明的证明。每个可执行 action 均由 self-test 检查 metadata，并实际调用 trace renderer。

## 所有权与析构顺序

`__refs` 初始化为一，对应 launch reference。`__run` 首先做 relaxed fetch_add，建立 completion reference；有 association 才 start，否则直接 `__complete`。completion 和 launch 分别做 acq_rel fetch_sub。同步 completion 可以释放 completion reference，但 launch reference 覆盖 `start` 返回及 `__run` 剩余的最后一次 state access。完成和两个 release 的义务在各自入口独立记录，不依赖真正执行 teardown。

最后一次 decrement acquire 其他 release 的 publication，再移动 association 到 local，复制 rebound allocator，调用 state destroy，注销 child 的两个 registrations，结束 child 和 state lifetime，然后 deallocate。allocator local 析构先于 association local，最后释放 association，相当于 scope decrement。state 的空 association 成员在 child 前析构；移动以后它不再 decrement。失败的 state construction/connect 尚未取得 association，先展开已构造成员，再在 catch 中 deallocate 并 rethrow；没有完成构造的 state 不调用完整 state destructor，也不执行并发 reference-release 协议。模型单独记录 construction success、partial storage end 和 deallocation obligation。

成员 `__op` 在 `__association` 前构造，因此 connect-time callback 可以在 try_associate 前注册、收到 stop。closed scope 仍会构造和销毁 connected、unstarted operation，两份 registrations 也必须各自注销。正常的反向成员析构有空 association 成员、inner operation、atomic member 和 allocator member；atomic/空 association 的无效 destructor 没有共享访问，合并为本地 continuation。

默认 allocator、显式 environment allocator 和 sender attributes allocator 使用同一个已选 allocator abstraction。所有 state allocator copy、destroy/deallocate use 和 destructor local 都检查 allocator storage 及 association 覆盖。launch 在公开 state 前的 allocator conversions、参数移动和 construction temporaries 合并为单线程初始化；不模拟 allocator 的任意用户代码、实际地址/对齐、字节或抛出 destructor。返回给调用者后仍存活的外部 allocator 对象必须有独立有效 lifetime；association 不是这些外部对象的 owner。强式“launch stack 上每个 allocator destructor 也由 association 覆盖”不成立：同步 completion 可以在 `__launch` 返回前释放 association。`literal_launch_allocator_lifetime` 让外层 allocator destructor 访问 scope-owned storage，保留此强式的失败探针；baseline 要求这类用户 destructor 的资源独立存活。

## 共享库契约与 child 边界

[共享库模块](../engine/library_contracts.py) 只定义 borrowed stop sources、callback registration/invocation 与 scope association；[inner operation](../engine/inner_operation.py) 保存有限 child 脚本。两个新模型使用同一实现，spawn_future 保留自己的实现。公共模块有独立 self-tests，不依赖本协议的 reference count 或 source names。

registration 在 connect 或 start。逻辑 fused-token precheck 允许 HB-coherent stale acquire choices；随后每个 input 的 registration 原子检查最新 stop。pre-stopped registration 在构造线程 inline invocation，从未链接到 source，以后注销不再依赖 source。pending 注销取消未选择的 invocation。跨线程注销只等待该 input 的 invocation return，自注销不等待。removed invocation 的剩余 return 在 stack 上，不访问已销毁 storage。两个 input 入口（包括 loser）以及 Fn 入口分别检查 child/state storage。

scope 与 environment 是两个独立 borrowed inputs。每个 source 最多一个 registration、一个 requester；注册以后没有独立 source query observer，所以 stop publication 与唯一 pending 节点的 selection 采用同一抽象线性化步骤，不存在被省略的节点 identity 排列。两个 source 的请求仍完全交错。fused winner 产生 commitment，下一协议动作必须进入 Fn；loser 不等待 winner。precheck/selection 独立产生 stop obligation，Fn 才 discharge。该 committed-delivery 边界来自 [counting_scope](../counting_scope/README.md)，不是重新证明 fused token 内部实现。

child 可在 start 内同步完成、自主线程稍后完成、在 stop callback 中同步完成、收到 stop 后另线程完成，或忽略 stop 最终自主完成。它保存 start 前的 notification，直到 start 和 registration constructors 返回才允许 completion。其内部状态采用合法 child 自身同步的抽象，不声称为任意 child 实现验证 race freedom。value/stopped 对本协议都进入同一无参数 `__complete`，因此按同一 completion identity 约简。脚本不在 callback constructor 返回前销毁 callback，不并发非法 start/destructor，也不制造重复 child completion。

scope association 抽象提供 try_associate 成败、每个成功 identity 的独立 release、最后 release 的 join handoff 和 scope/source lifetime 检查。内部 mutex/count/list 算法由 counting_scope 单独检验。environment source 由外部 fixture 覆盖运行；scope source 必须覆盖 request_stop 的整个调用，包括 self-deregistration 以后返回时的最后访问。

## 场景、性质与负对照

| 家族 | 有限内容 |
|---|---|
| a / b | 同步/独立完成，completion 与 launch release 竞争 |
| c | closed、connect-time pending/inline registrations、未 start teardown 后 scope stop |
| d | 同步/延迟/忽略取消，双 input 同步 stop，connect/start 注册期间的请求；五种 mode 的 pre-start notification |
| e | 最后 release 后 join/destruction，等待 request 返回的正常 scope lifetime |
| f | construction/connect 抛出、deallocation/rethrow，无 association |
| x | launch、child、两个 requester、joiner，最多五线程 |

每配置一个 state、一个 child、一个 allocator identity、至多一个 engaged association、两个 registrations。所有 enabled 线程和每个合法 stale load choice 都分支。`required_coverage` 检查同步/延迟完成、pre-start delivery、两个 fused winners、loser、self/cross-thread deregistration wait、未关联路径与 join。更大边界需要新增场景。


`d_register_connect_0/1` 与 `d_register_start_0/1` 的 requester 不等待 child readiness，因此请求可交错于 precheck 和 registration。两个 inputs 分别要求 true/false precheck、pending/inline registration、pending selection，以及同一路径上的 false→pending、false→inline、true→inline。`sc_stale` 还要求多选 load 和同一 input 的 stale-false→inline。每个 query 的 observation 以独立 coverage ghost state 保留至该 input 注册，不参与 enabled、completion 或 ownership 判断；配对断言不会把不同执行的 query 与 registration 拼成一个 witness。`d_dual_sync` 同时保留两个 input 的 winner 和 loser。quick 也运行这些场景的全部三种模式。

- SP1：state 恰好销毁一次，两个 reference 均已各自 release，count 等于实际 owners，剩余 `__run` 不访问 dead state。drop_add_ref、double_completion_release、read_after_destroy 及两个 skip-release controls 检验这些独立要求。
- SP2：engaged association 恰好释放一次，晚于 state destroy/deallocate、两个 registrations 的逐项移除及所有 modeled allocator use。association_before_destroy、association_before_deallocate、associate_before_connect 和 skip_input_0/1 提供反例。每个 registration 在 storage end 与 terminal 都独立检查。
- SP3：completion 恰好一次，not-associated 不 start；stop commitment 不丢 dispatch，pre-start notification 不丢失。start_unassociated、drop_completion、drop_stop、drop_notice 分别破坏这些要求。
- SP4：terminal 无 state/child/allocation/reference/association leak，包含两种异常。skip_destroy、skip_deallocate、skip_association 保留独立 obligation、只省略 discharge。
- SP5：声明脚本的 invocation/deregistration 无等待环。wait_cycle 故意让 self-deregistration 等待自己，terminal 检查 unfinished scripts。
- SP6：所有其他 reference holder 的最后 access HB 于 destroy/deallocate；storage lifetime end 是普通 conflicting write。relaxed_release 暴露缺失的 acquire/release。release_before_start 在 connect/start callback race 中丢失 release 以后 child access 的 HB。

强式“最后 release 的 join continuation 总可以当场销毁 scope”不能成立：若 continuation 位于 request_stop 内，source 仍在使用。`strict_request_lifetime` 保留此强式并检测 scope destruction inside request_stop；baseline 要求 scope 覆盖 request 返回。这是 source lifetime 前置条件，不是 header baseline 缺陷。

## 内存、敏感性与维护

`sc` 穷举 SC interleavings；`sc_hb` 检查 plain storage 和 lifetime vector clocks；`sc_stale` 允许抽象 fused precheck 的 acquire 旧值。spawn 自身只有 refcount RMW，没有可 stale 的 refcount load。使用 [memory.py](../engine/memory.py) 的真实 atomic orders、release sequences 与独立 MO clocks；每个 handoff 仅 acquire 自己的 publication，并传播 observed bounds。move、失败 branch、query 和本地 continuation 不产生 synthetic atomic publication。

不增加 store-buffer modes：本协议的 reference 决定由 RMW 串行化，stop 与 scope 内部算法已经在独立模型中覆盖；当前边界显式保留真实 publication 和 coherent stale query。这不是完整 C++ axiomatic weak-memory 或某平台映射证明。无环 full graph 的所有执行完成；completion reachability 不能推广为任意 fair schedule 的 liveness。

九个 mutants 均对应单点源码变化，每个 full 对全部二十五个场景的 sc/sc_hb 做完整 sweep，记录每个 killer，不能从一个通过的配置宣布 survivor。全部九个都有 killer；当前无需 survivor combined control。read_after_destroy 移动原 read，state 中仍 engaged 的 association 随成员析构，不能偷偷保留原来的 moved-out owner。associate_before_connect 同时移动 initializer 和异常展开顺序，在 connect 抛出时先释放 association 再回到 catch deallocation。

`sensitivity.txt` 人工维护，未列出的配置必须通过；工具从不改写它。expected TSV 保存 states/transitions/terminal/outcome 和完整 semantic record 的 SHA-256，不含 wall/RSS。self-tests 包括逐对象 lifetime、独立 obligations、ownership、真实 refcount orders、无 atomic move、member-order unwind、metadata/render 与 AST oracle regression。AST 检查直接 `w` fields 的 assignments 是否处于直接引用 `C.mut/control` 或 `c.mut/control` 的 branch 或 RHS；它不分析 alias、helper 或间接数据依赖，不能代替人工 oracle 审查。

```bash
python3 -B formal/spawn/check.py --check-source
python3 -B formal/spawn/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/spawn/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/spawn/check.py --tier full --only 'd_connect_race' --jobs 1
python3 -B formal/spawn/check.py --tier full --jobs 8 --update-expected
python3 -B formal/spawn/check.py --tier full --jobs 8 --refresh-lock --assessment '说明源码对应关系和抽象边界'
```

Python 3.9+、仅标准库，workers 自行 configure，支持 spawn。quick 使用所有 SC baselines、关键 HB/stale 代表与全部注册竞争场景、每个 mutant 的便宜 witness 和全部 controls。每配置 cap 为 250,000 states；达到 cap 的 incomplete 不得通过或更新资产。默认至多八 workers，每 worker 预算 4 GiB。`--out` 指定结果目录，包含 raw JSONL、summary 与最短反例。

只有整个 spawn.hpp 是 locked source；digest 仅转换 CRLF→LF，保留所有注释/空白。lock 是 drift detection，不是 fidelity proof。任何 checker/engine/expected/sensitivity 改动要求 full；纯 C++ 改动在明确 assessment 后可用 quick，仍须沿用上次 full 的 checker/expected digests。先手工调整 sensitivity，再 `--update-expected`，最后独立 full `--refresh-lock`。事务拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 和空 assessment；update/refresh 不可合用。最近资源与资格统计随 source.lock 和 runner summary 维护，worker peak 不代表 pool 总峰值。



## 最近一次完整检验

2026-11-19，Python 3.13.13，四个 worker，`spawn` 启动方式。baseline 全部通过；mutants 与 controls 的每项 outcome 均匹配人工 sensitivity。全部配置完成，未达到 state cap。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 539 | 12.593 | 40.88 | 156109 | 348594 |

full 包含 75 个 baseline、450 个 mutant 配置和 14 个 controls；279 项通过、260 项预期反例。最大配置 11828 states。上述 wall 包含 pool 启动；单 worker peak 不能当作 pool 总峰值，测量不能外推更大边界。

历史 quick 测量于 2026-11-16 使用八个 worker 和 `spawn` 启动方式运行了 75 个配置，耗时 1.142 s，最大 worker RSS 为 39.98 MiB，共 29606 states 和 68837 transitions。
