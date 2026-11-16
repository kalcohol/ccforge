# spawn_future 协议模型

本工具对 [spawn_future.hpp](../../backport/cpp26/execution/spawn_future.hpp) 做有界显式状态穷举，检查 eager producer、single-consumer future、取消、所有权、allocator resource 和 scope association 的协议。结果只适用于下述有限脚本、库契约和抽象边界，不构成完整 C++ weak-memory 证明。

## 代码与状态

`model.py` 保存状态、source fragments、转移、独立义务及性质；`scenarios.py` 保存脚本与覆盖要求；`tiers.py` 保存矩阵；`mutants.py` 保存单点源码变更与 controls；`sensitivity.txt` 是人工维护的失败名单。`expected/results.tsv` 保存逐配置 semantic records，`source.lock` 锁定整个头文件及 checker/expected digests。

```bash
python3 -B formal/spawn_future/check.py --steps
```

step table 引用 launch、三个 inner receiver completion、state 的完成/消费/取消/脱离、consumer 的 active 仲裁、owner 链、operation 和 sender 的析构、allocator retain/release 及分配调用点。嵌套 lambda 和 local class 的 body 不充当外层函数 fragment。隐式成员析构、shared-pointer 库动作、script handoff 和 ghost 义务明确为 model-only；fragment 存在不证明对应关系正确。

状态包含 phase、consumer_taken、result、state-consumer strong cycle、future/operation/stack edges、两个 control blocks 的 strong/weak counts、四处分配、allocator copies、association、三个 mutex 的 owner/publication、callback registration/invocation、producer completion、operation destruction、receiver histories、脚本及 HB 信息。没有地址复用。

## 临界区和可观察边界

三个非递归 mutex 分别为 state mutex、consumer owner mutex 和 operation consumer mutex。acquire、每次可被锁外 observer 看见的 atomic RMW 和 unlock 都是独立步骤。unlock 是对该 mutex 的最后访问，没有额外的返回等待。refcount decrement 导致的析构与后续 unlock 之间仍可交错。

| 路径 | 顺序 |
|---|---|
| `__complete_with` | state lock；检查 phase、存 result、转 done、移动 consumer edge；unlock；consumer delivery；再次 lock；移动 keepalive；unlock；释放 local keepalive；最后释放 local consumer_cb |
| `__try_cancel` | 请求 state stop；state lock；done 检查、存 stopped、转 done、移动 consumer edge；unlock；delivery；保留 producer keepalive |
| `__consume` | consumer 参数 strong copy；state lock；taken 检查、标记 taken、选择 registered/deliver_now/already_consumed；unlock；销毁参数；返回 caller 后才 delivery |
| `__abandon_unconsumed` | state lock；taken 检查、标记、保存是否取消；unlock；必要时请求 stop |
| delivery | active exchange acq_rel；移动 state 到 local；注销 stop callback；release owner；调用 receiver；释放 local |
| abandonment | release owner；active exchange acq_rel；移动 state；state lock 下 detach consumer；unlock；注销 callback；请求 stop；释放 local |
| `__release_owner` | owner lock；exchange 普通 owner pointer；嵌套 operation consumer lock；清空 operation strong edge（独立 decrement）；operation unlock；owner unlock |
| operation destructor | 尚未 start 时 abandon state；operation consumer lock；移动 consumer 到 local；unlock；local consumer abandon；释放两个 local/member owners |

合并的普通访问仅为同一 state lock 下的 phase/result/taken 判断及更新、registered consumer edge 的移动/比较，以及 owner lock 下 owner 的读取和清空、operation lock 下 consumer shared_ptr 的移动。其他正常访问方都由同一 mutex 或 active winner 排斥。普通局部变量及 ghost 更新附着在对应步骤，不增加 atomic publication。strong copy/decrement 不与 unlock 合并。

owner 去锁 mutant 将所有 release-owner 参与者的 owner read/write 分开；operation 去锁 mutant 将 destructor 的 consumer read/move 分开，另一方的 reset/decrement 仍是独立步骤。去锁不保留原锁所支持的不可分割读写。nested owner → operation acquisition 始终保留，检测声明脚本内的等待环。

## shared_ptr 与 allocation resource 契约

采用抽象标准库契约，不绑定某个实现的 control-block 内部布局。每个 shared_ptr/weak_ptr copy 和 destruction point 分别执行 atomic RMW；move 只移动 edge，没有 atomic step。increment relaxed，strong/weak decrement acq_rel；最后 release 因而继承每个 owner 的先前访问。weak lock 和 shared_from_this 为 increment-if-nonzero，后者要求 live owner；不允许 resurrect。最后 strong release 运行 deleter、销毁 object；最后 weak release 才销毁/deallocate control block 及其 allocator copy。state 的 enable_shared_from_this 有独立 weak edge，另有 object 存活期间的隐式 weak edge。

state 和其 control block 是两次分配。consumer 使用 allocate_shared，object 在最后 strong release 析构；逆序 member destruction 先注销 callback，再释放 `__state`，然后记录 object lifetime end。合并 storage 在整个 object destruction 期间可用，最后 weak release 才回收。state 也保留 member-destructor access 至其 object lifetime end。模型分别检查 object 和 control-block tombstones，且每个 count 与显式 ownership graph 独立核对。

resource retain relaxed，release acq_rel。其并发存活的 references 包括 launch `resource_owner`、state 的 `__state_allocator`、state deleter 的 allocator、state control block 的 allocator、consumer combined control block 的 allocator，以及 deleter 调用期间的 local allocator。最后 release 先销毁并 deallocate resource，再销毁 moved-out association。association 是抽象 scope counter；最后 decrement 后脚本可完成 join 并销毁 scope，此后 allocator/resource/scope access 都违规。

launch construction 的 allocator conversion、control-block 构造及 enable_shared_from_this 初始化在发布对象及 start 前合并为单线程初始化；仍明确建立每个跨并发边界存活的 allocator owner。consumer construction 同理：future/start 的 state owner 覆盖 construction，consumer 尚未向其他线程公开。deleter 的并发 local allocator 不能并入这些 temporaries，保留独立 retain/release。allocator 选择抽象为同一个已选 allocator identity；没有模拟用户 allocator 的任意代码、字节、对齐、实际申请失败的全部组合或 ABI。

try_associate 可以因 closed scope 失败；state/operation 仍已构造，producer 不 start，以 stopped 完成 state。成功时先建立 self keepalive 再 start。producer child 可在 state construction 的 connect 阶段注册，或推迟至 start 注册；两种注册时机分别建模。launch-time owner 在 `__spawn_future` 返回边界释放。scope token 的 wrap 将 scope stop 送达 producer。scope request 的最后 source access 与 join/destruction 的 script handoff 分开；scope 必须覆盖整个 request 调用。

## stop、producer 与 receiver 脚本

使用 [counting_scope 模型](../counting_scope/README.md) 的 stop 契约：registration 相对 request 原子化；已停止时在 constructor 线程 inline 调用，inline-only registration 以后不依赖 source；requesting 线程至多调用一次；pending deregistration 取消调用；跨线程 deregistration 等进行中的 invocation 返回；self deregistration 不等待。consumer token 至少包括 unstoppable 与 borrowed inplace token；后者的 source 由脚本环境覆盖整个运行。

state source 是 borrowed inplace source。producer registrations 属于 state 的 operation，逆序成员析构先注销两个 inputs，再销毁 state source。两个 registrations 都必须在其所在 state storage lifetime end 前成为 removed（未构造的 absent 除外）；state_end 与 terminal 独立检查每个 input，不能用 input 0 的 source 检查代替 input 1 的 storage 检查。每个 input 独立保存 absent/pending/executing/completed/inline/removed、invocation thread、registration publication 和 invocation-return publication。跨线程注销只等待自己的 invocation；self 注销不等待，removed 后 invocation return 可以留在调用线程的 stack 上，不能把这个返回本身误判为 live registration。inline-only 注销不访问输入 source。每个 input callback entry（包括输家）与 Fn entry 都检查并访问所在 state storage，参与 tombstone 和 HB lifetime 检查。consumer registration 另有独立状态，不能替代 producer 的注销等待。

producer fused registration 使用 counting_scope 的抽象 committed-delivery 契约，本地实现该契约边界，不复制 trampoline 内部协议：逻辑 pre-stopped token 在构造线程先 delivery，之后依次注册 state/scope inputs；每个 input 原子注册或选择，赢家的下一步必须为 Fn，输家不等待赢家。每个 source 只有一个 producer registration，且注册后没有该 source 的独立 query observer；抽象 request 将 stop publication 与唯一 pending callback selection 归为同一线性化动作，pending 注销可在该动作之前取消。这里没有省略多个候选 callback 的排列，source 内部算法仍由独立 stop 模型负责。独立 stop obligation 在 pre-stop/selection 时建立，Fn entry 才 discharge。start-time variant 在 start 中建立 registrations；connect-time variant 在 try_associate 之前建立 registrations，即使 closed launch 不 start，析构也必须注销两个 inputs。Fn 在 start 前可合法通知：独立记录 pre-start notification，child 保存 stop 状态；通知本身不声称 completion，真正的 completion claim/dispatch 只能发生在 start 之后。sync mode 在 start 时处理保存的通知；later/own/ignore mode 由已 start 的 producer 线程完成；inline mode 在 start 内完成。scope stop 在 launch 之前或 start 边界发生也会保留。scope 与 state stop histories 分开，scope request 不写 state source。start 后到达的同步通知在 Fn 中完成，later mode 等实际 Fn stop effect 后在 producer 线程完成，ignore mode 最后自主完成。库内部节点、source lock 和 fused arbitration 的弱内存实现由 counting_scope/stop_token 模型负责。

producer 可自主 value/error completion、在 start 中同步 completion、在 stop callback 中同步 completion、等待 stop 后独立完成，或忽略取消最终独立完成。独立 producer claim 防止库脚本本身重复 completion；协议仍独立检查 result 只写一次、phase 只转一次。inner receiver 的每次入口 strong pin、request_stop 的 strong pin 和 consumer callback 的 weak lock 都分别表示。

receiver 脚本包括 inline 销毁 consumer operation 和销毁无关对象。`c_robust`、`x_destroy` 允许 start 返回后销毁仍未完成的 operation，这是工作草案之外的 robustness 检查。脚本不并发调用 start 和 destructor，也不在 callback constructor 返回前销毁它。completion 内 inline 析构可发生在 start 返回前，因为 start 已把其所需 owners 移到 local，后续不再访问 operation members。

failure injection 分别令 connect consumer allocation、callback installation、consume 或 result emplacement 失败。前三者分别 abandon/rethrow、deliver_error/abandon、deliver_error/request_stop；emplacement 失败改存 exception error。没有把 failure 当作成功 abort，每条 terminal 仍检查 completion 与回收。用户 throwing destructor、再次失败的 exception storage、非法 producer 重复 completion 和任意递归 receiver 不在边界内。

## 场景和边界

| 家族 | 内容 |
|---|---|
| a | registered/deliver_now，value/error，start 中同步完成，producer/consumer 竞争 |
| b | future destructor/move assignment，running/done，honour/ignore cancellation |
| c | operation 未 start 析构，以及 started pending robustness 析构 |
| d | stop 在 start 前、安装中、注册后，以及与 producer completion 竞争 |
| e | stop callback 内同步完成，或请求后在独立线程完成 |
| f | closed scope，没有 association，不 start producer；覆盖 connect-time pending/inline registrations 的析构及 state 销毁后的 scope stop |
| g | 五种 producer mode 的 connect-time start 前通知、已注册 callback 的 start 边界通知、最后 release 后 join/destruction，scope stop 在 launch 前或 start 边界、scope/consumer stop 并发，以及 request 返回后 join |
| h | 四种 failure injections |
| r | receiver inline 销毁 operation、销毁无关对象 |
| x | 最多五线程：spawner、producer、consumer、stop requester、destroyer |

每配置一个 state、一个 consumer、一个 operation、一个 resource、两个 control blocks、一个 engaged association 和一个 consumer registration。producer 的两个 fused inputs 分别保存 registration/invocation 状态，以已验证的库契约为边界。所有回调/脚本有限，不建模任意 thread count。public sender move-only，脚本不重复 connect/start；`a_defensive` 与 self-tests 内部设置 taken，检验 `already_consumed` 防御分支，不声称合法 public script 可以创建第二个 consumer。

每个 baseline 全图探索，场景自己的 required coverage 防止 registered/deliver_now、inline/pending、cross-thread wait/self deregistration、同步/延迟/忽略 stop、pre-launch/pre-start stop、五种 mode 的 start 前通知与 start 后完成、未 start 的两个 registrations 注销、storage end 和随后 scope stop、两个 fused winner、loser、producer 注销等待、四种异常、join 和五线程交错静默消失。所有 enabled 线程及 stop query 的每个合法 stale choice 都分支；只有一个 consumer registration，无可省略的 callback identity 排列。失败 polling 没有重复无效步骤，成功等待仍通过单独 signal/await handoff。

## 内存模型

`sc` 穷举 SC 交错；`sc_hb` 额外检查普通字段和 storage lifetime 的 vector-clock races；`sc_stale` 保留 history，允许 stop_requested acquire query 读取 HB-coherent 旧值。三个模式都使用真实 atomic memory orders，不合成发布。

公共 `engine/memory.py` 分开保存 modification clock、release-sequence publication 和已观察 MO position。relaxed RMW 延续前一个 release sequence，不能发布自己的先前 actions 或观察下界；acq_rel RMW 同时 acquire 和 release。每个 mutex、registration、invocation return 和 script handoff 只导入自己的 publication，携带 HB clock 和 observed bounds。stale load 下界包括本线程已见 position、HB 传入的下界及 HB modifications；query 读到 false 后 registration 仍原子检查当前 stop 并可 inline invocation。

不增加 store-buffer modes：协议决定由 mutex、refcount/active RMW 或抽象 stop 库契约串行化，可观察的真实 publications 与 acquire stale loads 已显式表示。stop 库内部的弱内存问题由独立模型负责。这不是 C++ axiomatic weak-memory 或某平台映射的完整证明。

## 性质与规范负对照

- C1：started receiver 有独立 completion obligation；恰好完成一次，除非 destruction 赢得 active exchange。active winner 至多一个。更强的“destructor 一进入就禁止以后 receiver completion”不成立：delivery 可先赢 exchange，随后 destructor 开始，delivery 再调用 receiver。`literal_C1` 保留该强式并失败；baseline 只要求 destructor 赢得 exchange 后不再 delivery。不能把 robustness 检查宣传为 destructor entry 与 receiver call 原子化。
- C2：producer 先完成时消费 stored result；successful cancellation、not-started 和 already-consumed 为 stopped；phase/result 写入最多一次。同步 stop 导致 producer 先完成时，try_cancel 的 done check 保留 producer result。expected outcome 在独立 contract events 记录：无 association 为 stopped、成功 cancel 为 stopped、producer 首先完成为 payload（emplacement 失败则 error）、already-consumed 为 stopped；defensive_take 和 consume entry 在实现 taken guard 之外记录这一要求，consume_taken 不能同时删除预期。install/consume failure 在故障事件记录 error；delivery 只快照这些独立 oracle，不能按自己的 deliver/deliver_stopped/deliver_error 分支选择预期，也绝不从 stored result 推导。cancel 的 oracle 仅在契约允许 running→done 时更新，与 cancel_done 的变更条件无关。`wrong_closed`、`wrong_cancel`、`wrong_producer`、`wrong_consumed` 各自只破坏对应 outcome，均失败 C2；wrong_install、wrong_consume_failure 单独破坏 error delivery，也失败 C2；`cancel_done` 检查重复发布。
- C3：object、operation、control block、resource 和 scope tombstones；count 不下溢，count 等于 ownership graph；zero 不 resurrect。去锁、member-order mutants 及直接计数 self-test 检测这些错误。
- C4：terminal 无 allocation、strong/weak edge、resource reference、keepalive 或 state-consumer cycle；engaged association 恰好释放一次。`retain_keepalive` 独立省略 discharge 后失败。
- C5：association release 时 state、consumer、所有 control block 和 resource 都已 deallocate。`association_first` 失败。
- C6：receiver 前注销 consumer callback，invocation 不访问 dead consumer；成功 cancellation 请求 state stop；两个 producer registrations 分别先于其所在 state storage lifetime end 注销，且 input 0 先于 state source 析构；callback entry 不访问 dead state storage。`callback_after`、`consume_first`、`member_order` 为反例；`skip_stop_request` 和 `skip_registration_teardown` 独立省略 stop/registration 义务的 discharge，必须失败。pre-start notification 的保存、completion start gate、start 时处理已保存 sync 通知、未 start 析构的注销分别由 drop_prestart_notice、complete_before_start、skip_start_stop、skip_unstarted_teardown 独立负对照检验，依次失败 C6、C6、C7、C6。skip_state_teardown 与 skip_scope_teardown 各自仅省略一个 input 的注销，closed launch 不 start 仍分别失败 C6；其 _late_stop controls 使用析构后 scope stop 场景，也在最早的 lifetime 违规处失败。原 skip_unstarted_teardown 仍同时省略两者并失败。direct self-tests 另验证 terminal 的双 input 检查和晚到 callback 的 entry 检查，不依赖搜索在最早反例之后继续执行。
- C7：consumer stopped 不提前销毁 producer operation；state destruction 在 producer completion 之后，并通过 reference releases 建立 HB。producer started 时独立产生 completion obligation，`skip_producer` 省略 dispatch 后失败。
- C8：三个 mutex 与 callback deregistration waits 无等待环；terminal 没有 unfinished thread。`wait_cycle` 是故意重复获取非递归 mutex 的脚本负对照，失败。
- C9：producer result write HB 于 `__deliver_to` read；phase、taken、result、consumer_cb、keepalive、owner、consumer_state、state member 和 callback optional 的普通访问无 race；全部此前访问 HB 于 storage destruction。operation member/锁访问及 scope association/stop/return 访问均记录对应 storage，object end 与 storage free 分开。`operation_no_handoff`、`scope_no_handoff` 分别删除寿命 handoff，均暴露 storage race。`no_mutex_release` 独立取消 state mutex publication，由 phase/consumer_cb race 杀死。

`drop_receiver` 保留 started obligation、跳过 receiver discharge，由 terminal 独立检查失败；正常 receiver-reentry control 通过。controls 不改变对应性质检查来制造通过。全图无环时所有 execution 结束；公共引擎的 completion reachability 不构成任意 fair schedule 的 liveness 证明。mutant/control 默认首反例停止；失败配置的 complete 表示已完成规定的反例搜索。

## Mutants 与敏感性边界

catalog 的 17 个变更都对应一个源码点。full 对每个 mutant 穷举全部 44 个场景的 sc/sc_hb，共 1,496 个 mutant 配置；baseline 对每个场景检验三个模式。每个 killer 都列入 catalog 的 `killers` 及人工 sensitivity，不只选择单个 witness。quick 使用廉价代表，同时保留 `deliver_locked.r_inline` passing witness 与 `d_registered` killer。

十个 mutants 有 killer；consume_taken 在 a_defensive 的两种模式失败 C2：首次调用前 taken 已为 true，必须得到 stopped。I2 只约束 consume 调用次数，不能替代这一 outcome 要求。`deliver_locked` 在 d_install、d_registered、d_race、e_later、x_five、x_destroy、g_both_stop 的两种模式失败：持 state lock delivery，等待 consumer callback 注销，而 callback 等待同一 state lock。其余七个在全部 88 个配置中存活；这不是删除源码保护的依据。

| Survivor | 检查中的 covering protection | Combined control |
|---|---|---|
| keepalive_first | I8：receiver delivery 仍有 consumer/caller state owner；registered delivery 将 consumer 的 `__state` 移入 delivery local，deliver_now 另有 start local | keep_cover_removed：同时删除 inner entry pin 与 consumer delivery state owner，a_registered 中 start caller 已返回，C3/I8 失败 |
| drop_inner_pin | keepalive/local keep 覆盖 completion 最后 state access；有 delivery 时 I8 另检查 consumer/caller owner | inner_cover_removed：同时提前释放 keepalive、删除 consumer delivery owner，C3/I8 失败 |
| drop_request_pin | I1：future、unstarted op_state、start_state、weak-lock pin、abandon/delivery local 中实际 caller owner 在 request 的整个展开期间保留 | request_cover_removed：另释放 caller owner，I1 失败 |
| owner_after | I5：owner 仅在 owner mutex 下清空；operation end 必须没有 owner link，inline destructor 的 abandonment 先清空它 | owner_cover_removed：另省略 abandonment release_owner，I5 失败 |
| abandon_stop_first | I6：abandon 请求 stop 时 active 已为 false；local state 覆盖 source | abandon_cover_removed：另省略 abandonment exchange，I6 失败 |
| abandon_taken | I3：consume 后不进入 abandon_unconsumed；started op 的 member state 已移动 | post_consume_abandon：另从 receiver 强行进入该防御路径，I3 失败 |
| strong_callback | I4/C6：completion、abandon 在释放 consumer 前 reset callback，打断额外 strong edge | callback_cover_removed 与 callback_abandon_cover_removed：分别省略两条路径 reset，C6/I4 失败 |

I2 独立约束 consume invocation 最多一次，repeat_consume 是超出此边界的负对照，不再作为 consume_taken 的存活论据。combined controls 有意同时移除 covering protection，不冒充单点源码 mutants 或合法 public API 场景。keepalive 与 entry pin 同时移除仍不足以暴露 delivery 寿命错误：consumer 的 state owner（及 deliver_now caller）仍在；上述 controls 明确移除该剩余 owner。所有 invariants 在 baseline 与 mutant 中使用相同 predicates。`plain_active` 的 false 分支仅 acquire load、不写 history；negative self-test 检查 history 不变。全部可执行 actions 由静态穷举 self-test 检查 metadata 并实际 render，包含 scope_request_end。

`sensitivity.txt` 人工声明逐配置 outcome，未列出者必须通过；工具从不改写该清单。新增场景时 full 自动扩展每个 mutant 的矩阵，并须重新审查 killer 清单及 survivors。
## 使用与维护

```bash
python3 -B formal/spawn_future/check.py --check-source
python3 -B formal/spawn_future/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/spawn_future/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/spawn_future/check.py --tier full --only 'd_install' --jobs 1
```

Python 3.9+，仅标准库；worker 自行 configure，支持 spawn。一般配置上限 400,000 states；五线程 destruction 的 x_destroy 使用 1,000,000 上限，以容纳 producer invocation/teardown 的新增交错。达到上限不能通过或更新资产。默认不超过八个 workers，每 worker 预算 4 GiB；较小机器可用 jobs 1。`--out` 指定 raw JSONL、summary 和 traces 的输出目录；默认写入 ignored 输出目录。RSS 是单 worker peak，不能直接当作 pool 总峰值。

whole-header source digest 仅规范化 CRLF→LF，包括注释和空白。lock 是 drift detection，不能替代人工 fidelity 检查。checker/expected/sensitivity 改动要求 full；单独 C++ 改动在明确 assessment 后可用 quick，但必须沿用上次 full 的 checker/expected fingerprints。新 expected 与刷新 lock 是两个事务：

```bash
python3 -B formal/spawn_future/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/spawn_future/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明模型与源码的对应关系、抽象边界及存活 mutants'
python3 -B formal/spawn_future/check.py --check-source
```

事务拒绝过滤、空集、incomplete、outcome 矛盾、expected mismatch 及空 assessment；update/refresh 不可同时使用，写入采用 atomic rename。expected 不含 wall/RSS，包含完整配置、计数、coverage、性质 outcome 及 semantic SHA-256。self-tests 检查独立义务、ownership counts、weak expiry、无 atomic move、三处析构顺序、producer/consumer 独立 wait、inline-only 注销、false active history、storage access 冲突、可执行 action 的 metadata/render、stale query 和防御 consume 分支。AST regression 只检查对 w.expected、consumer_expected、receiver_expected、obligation、producer_started、stop_obligation、cancellation、prestart_notified 和 request_owners 的直接 attribute writes：禁止位于直接引用 C.mut/C.control 的 if 的任一分支内，也禁止赋值 RHS 直接引用这两个 variant 字段（包括 conditional expressions）。每个 protected field 都有两种 variant 的负向 fixtures，覆盖 if/else、普通/annotated/augmented assignment 的条件表达式及直接 RHS reference；独立 contract 条件和受控 discharge 有正向 fixture。此检查是语法 regression，不分析别名、helper/setattr writes 或间接数据/控制依赖；独立 oracle 的完整语义仍需人工审查及直接回归。共享 engine 的任何变化要求所有使用模型分别 full qualification。

## 最近一次完整检验

2026-11-16，Python 3.13.13，八 workers / spawn。full 的 132 个 baseline 全部通过；1,496 个 mutant 配置中 340 个反例、1,156 个存活；38 个 controls 中 37 个反例、一个正常 reentry 通过。每项均匹配人工 sensitivity 和 expected。17 个单点 mutants 中十个被杀死、七个在全部 88 个配置中存活；八个 survivor combined controls 都被杀死，repeat_consume 另检验 I2 invocation bound。四个独立 input teardown controls 均失败 C6；combined skip_unstarted_teardown 仍失败。全部配置完成，没有达到 state cap，最大配置 `abandon_stop_first.x_destroy.sc_hb` 为 585,179 states；最大 baseline `base.x_destroy.sc_stale` 为 405,127 states。expected update 和独立 full lock refresh 均完成，1,666 个 semantic records 完全一致。

quick 包含所有场景的 SC、若干 SC-HB 代表（包括 catalog witness）及 d_pre 的 stale baseline，并包含全部 mutants 和 controls。full 在约 15 min、quick 在约 60 s 的预算内。

| Tier | 配置数 | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---:|---:|---:|---:|
| full | 1666 | 107.573 | 1493.12 | 9623011 | 21057963 |
| quick | 912 | 25.589 | 115.11 | 2408058 | 5178171 |

wall 包含 pool 启动，不含 CLI 入口的模型 self-tests；pool peak 未测量。每 worker 实测低于 4 GiB。资源数据随机器及负载变化，不能从本次测量外推更大边界。
