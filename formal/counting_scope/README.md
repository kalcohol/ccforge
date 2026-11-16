# counting scope 协议模型

本工具对 [counting_scope.hpp](../../backport/cpp26/execution/counting_scope.hpp) 做有界显式状态穷举，覆盖两个 scope 的 join 协议（J）、scope stop（S）和 fused callback（F）。F3 检查 committed delivery，并以更强的请求返回期限作为规范负对照。检验结果只适用于声明的有限边界，不是任意 C++ 程序的证明。

## 代码与状态

`model.py` 保存状态、转移、source references、ghost HB 时钟和性质；`scenarios.py` 保存有限脚本；`tiers.py` 保存矩阵与 quick 代表；`mutants.py` 保存变更和 controls；`sensitivity.txt` 是人工维护的逐配置失败名单。`expected/results.tsv` 与 `source.lock` 使用公共 engine 的维护事务。

J 只实现一次，以 `scope_kind` 区分 `simple_counting_scope` 和 `counting_scope`。所有 CLI 入口比较两个类的 destructor、`close`、`__try_associate`、`__start_join`、`__decrement`、association `__release`、`__count`、`__is_closed` 及 token/association 两个 `try_associate` forwarding body。比较实际 token spelling，包括字符串、字符、raw literal、数字和 preprocessing directives；忽略注释，仅规范化两个 scope 类名。注释和空白的漂移仍受全头文件字节 digest 保护。负向 fixtures 包括只改单侧 literal 内容、escape spelling 或其他 body token。

状态保存七种 scope state、count、association engagement/release 历史、joiner list/links、对象 tombstone、join completion、stop inputs、registrations、active invocation、Fn arbitration、带 callback identity 和 winning thread 的 commitments、独立 delivery obligations、线程脚本、scheduler 队列和同步时钟。

```bash
python3 -B formal/counting_scope/check.py --steps
```

step table 同时引用两个 scope 的函数，并引用 state-machine helpers、`__complete_joiners`、join operation、scheduler receiver、`request_stop`、`__prepare_input` 和 trampoline。隐式成员初始化/析构、普通脚本、库契约和 ghost 记录标为 model-only。fragment 存在只是漂移检查的一部分，不能证明模型与源码等价。

## 临界区与可观察边界

`std::mutex::lock` 阻塞；unlock 是 release，且为释放方对 mutex 的最后一次访问。另一线程可以在原 unlock 表面返回前取得、释放并销毁 mutex；没有额外的“等待 unlock 返回”步骤。

每个临界区先 acquire mutex，在以下步骤之间一直持锁，最后单独 unlock。锁外 observers 可在任何步骤之间执行；teardown 也不能因合并而消失。

| 函数 | 源码顺序及 atomic memory order |
|---|---|
| `__try_associate` | 读取 state/count 并决定；拒绝时无 atomic write；接受时独立 state store release（即使值不变），随后 count fetch_add relaxed |
| `close` | state load relaxed；独立 state store release，无条件写入 |
| `__start_join` | count load acquire；非空分支 state load relaxed；两分支各自独立 state store release；非空分支再初始化 links 并 push list |
| `__decrement` | count fetch_sub acq_rel；仅最后一次 decrement 继续 state load relaxed；仅 joining 时独立 state store release，再摘出 list |
| destructor | state load relaxed；独立 unlock；检查允许状态，再做成员 teardown |

只合并以下不能被锁外线程区分的普通访问：持锁 list push 的 next/registered/list 更新；持锁 list exchange 的读取和清空；独占 detached list 的 next 保存、link 清理和 registered 清理。前两项的其他普通访问方都被同一 mutex 阻塞，observers 只读 atomics；后一项的 operation 只有在 receiver delivery 后才可由 continuation 销毁。`__try_associate` 的两个只读判断在持锁期间合并，因为 observers 不修改它们，其他修改者均阻塞。局部返回值、指针保存及 ghost 记录可附着于对应步骤，不发布额外 atomic 写入。

去锁 mutants 同时去掉排斥、同步和由锁支持的普通访问合并：只要 join 或 decrement 任一方去锁，双方都分离 list read/push 和 list read/clear，即使另一方仍持锁；所有 atomic accesses 保持独立。不会把一个去锁函数保留成不可分割的整体。

`f_observer_teardown` 先启动并注册 join，随后只通过 count=0 和 is_closed 的 acquire observers 决定 teardown，不等待 join completion。该脚本没有 close，joining 时 is_closed=false，因此读到 true 表示 joined，保证析构前置状态；失败的 polling 次数被省略，成功的 load 仍是可交错步骤。锁定 destructor 等待最后 decrement 的剩余 list/mutex accesses；去锁版本暴露 storage lifetime 违例。完成路径只再访问 detached join operation。

## 脚本与边界

空 scope 的 join 在 start 线程直接完成；非空 scope 的最后 release 摘出 joiners 后在 mutex 外启动 schedule operation。inline 模式在当前线程完成；queued 模式由专用模型线程执行，enqueue release、dequeue acquire。continuation 可销毁 operation/scope、再启动 join、try_associate、close 或 request_stop；simple scope 不调用 request_stop，也不销毁不存在的 stop source。

association holders 是 model-only scripts，镜像 spawn/associate/spawn_future 的契约。holder 在最后一次 scope/source access 后释放 association，尤其先销毁 child registrations；`S_release_first` 反转该顺序。scope、join operation、holder 和 callback storage 都有 tombstone。所有脚本遵守 callback constructor 返回后才可销毁对象的核心语言前置条件。

最多五个线程（含 scheduler）、三个 holders、两个 joiners、三个 callback objects，每个最多两个 input registrations。同源 inputs 仍是独立 registrations。没有地址复用、allocator、异常、payload、无限递归或任意用户程序。association 默认无模型容量限制；只有 `d_limit` 用 **model-only limit=2** 检验 `count >= limit` 分支，不声称头文件的 size_t 上限为二或三。`a_keep` 明确保留 association，允许 started join 留待外部完成。

场景包括多 holder、多 joiner、close/associate/join 竞争、nested association、continuation reentry/destruction、strict lifetime probes、构造/请求/析构竞争和两个输入的自毁。`s_multi` 穷举三个 scope callbacks 的调用次序，各自同步注销和释放，最后一次 release 在 request 内完成 join；`s_external_complete` 将 holder 自主注销/释放与 requester invocation 竞争，覆盖跨线程注销等待。`d_unused_closed_*` 分别覆盖 unused scope close 后的直接析构和 join；`d_joining_observers` 覆盖 count=3/2 与 open/closed joining states。各配置的 required coverage assertions 以及 quick inclusion self-tests 防止这些路径静默消失。

## stop 库契约与所有权

依赖的库契约是：registration 相对 request 原子化；已停止时在构造线程内联调用；request 在内部锁外、请求线程上至多调用每个 registration 一次；callback 可注销自身或 sibling；跨线程注销等待进行中的 invocation 返回，自注销不等待。pending 注销允许取消尚未选中的 invocation。请求发布 stop、选择节点、trampoline、Fn、invocation return、下一次选择和 request return 都是独立步骤。每次选择均对该 source 的所有 pending registrations 分支，没有固定 identity 顺序或隐含对称约简。

这些假设依赖 [stop_token 模型](../stop_token/README.md) 的 registration/request 仲裁、单次 invocation、pending sibling 删除、自毁、completion 等待与最后 source-lock handoff；对应 `d_req_ctor_*`、`f_body_kills_sibling`、`c_self`、`b_req_dtor_*` 和 `s_inline_*` 场景。此处不重复实现 waiter bit、wait/notify 或标准库算法。

source 0 是 scope 内 borrowed inplace source，必须覆盖所有仍关联的 registrations 和每个 request 调用（包括返回 false 的调用）。source 1 采用相同的 **borrowed inplace** lifetime 契约，代表 downstream 的一种合法实例。`std::stop_callback` 则共享拥有其注册的 stop state：原 `std::stop_source` 对象不必覆盖 registration lifetime；其 state 必须存活，request 仍需自己的有效调用对象。本模型不把 borrowed source tombstone 结果推广为 shared stop_source 对象的析构限制。

已停止时构造的 inline-only callback 从未注册，单独记录为 inline-only；其 destructor 不再访问原 source。曾经注册后 invocation 完成的 callback 仍关联 source，直到注销。`review_inline_registration_lifetime` 覆盖 scope source 在 inline callback 构造返回之后、该 callback 析构之前销毁的合法情况。

strict source tombstones 检测仍在 request、注册或注销时销毁 source；`g_*` 和 `s_inside_request` 是故意违反前置条件的探针。abort 是成功检测，单独计数。queued continuation 可能在 request 返回后运行，因此 strict 场景并非每条路径都 abort；HB 还检查缺少 handoff 的 storage destruction。

## 内存模型

`sc` 穷举顺序一致交错；`sc_hb` 用 vector clocks 检查 holder data、joiner list/links、Fn storage 和 scope/source lifetime。mutex、registration publication、invocation return、scheduler enqueue/dequeue 及 script signal/wait 使用各自的 release/acquire 交接。初始对象在启动所有线程前完成初始化。relaxed trampoline exchange 只仲裁，不发布 Fn，也不代替 losing trampoline 的 completion wait。

atomic history 的每个 modification 保存 value、modification clock 和 release-sequence publication。每个 synchronization publication 分别携带 HB vector clock 和已观察的 modification-order 下界；mutex、queue、signal/wait、constructor、registration、invocation return、request return 和 join completion 的 HB handoff 都合并这两部分。release store 发布 writer 的这两部分；relaxed RMW 只继续之前的 release sequence，acquire 读到它只导入 head 的 publication，不导入 RMW writer 的先前动作或观察下界；acq_rel RMW acquire 旧值并发布自己的 clock 和下界，成为新 release head。没有真实写入的拒绝/不变路径不创建 history entry；值未变但源码确有 store 的路径仍创建 modification。

`sc_stale` 允许 `__count`、`__is_closed` 和 `__prepare_input` 两次短路 acquire 查询读取旧值。下界使用本线程及经 HB handoff 导入的已见 modification position，以及所有 HB modifications 的最新 position；读操作 HB 于另一线程的读操作时，后者只能读取同一或更晚 modification。publication 则来自所读 entry 的 release sequence，两者不能混同。`review_hb_observer_handoff` self-test 通过 relaxed count increment、observer 和 signal/await 检查跨线程 read-read coherence，并逐项验证其他 handoffs；负对照拒绝从 relaxed RMW writer（包括其读下界）或 rejected-association path 发明同步。两个 observers 不构成跨对象一致快照。

不增加 x86-TSO 或 ARM-like store buffers：在此库契约边界，协议决定由 mutex 或 RMW 串行化，实际 atomic publications、锁内可观察边界与锁外 acquire 陈旧性已经显式表示；stop 库内部的 store-buffer 问题由其独立模型负责。这不是完整 C++ axiomatic weak-memory 证明，不能据此推断任意平台映射或库实现。

## 性质与规范负对照

- J1/J2：started join 恰好完成一次；正常 terminal 不丢 join（`a_keep` 例外）；completion 时 joined/count=0，所有 engaged associations 已释放，此后不接受新 association。
- J3/J5：count 与 engaged 数一致且不下溢，正数只允许 open/closed/joining，joined 保持终态；closed/joined 拒绝 association；join state-machine default 不可达；destructor 只接受 joined/unused/unused_and_closed。
- J4/J6：tombstone 后不访问 scope、holder 或 operation，receiver 完成后不触碰 operation。holder 写入 HB 到 continuation；list/links 与 storage destruction 检查普通访问顺序。
- S1/S2：scope 覆盖 request 调用；scope 析构时无遗留 borrowed child registrations；单个 registration 至多执行一次，已停止时注册内联执行。
- F1/F2：两个 inputs 共享 exchange 仲裁，Fn 至多一次；任一 input stop HB 于 constructor 起点时，在构造线程、constructor 返回前运行 Fn。
- F3：成功发布 stop 的 request 开始时独立记录该 source 的 registrations，注销只取消对应 snapshot entry；返回时，每个保留者必须已进入 Fn，或存在一个将在无协议等待的下一步直接进入 Fn 的 commitment（可经任一 input）。exchange 成功时独立于 Fn dispatch 记录 callback identity 和 winning thread；state/terminal 性质独立要求每个 commitment 恰好进入 Fn 一次，拒绝丢弃 dispatch 和进入前的协议等待。返回 false 的 request 自身无 delivery obligation。另检查每个 selected invocation 完成。
- F4/F5：外部 destruction 返回时 Fn 不在运行且以后不再运行；从任一 input invocation 自毁后不再访问已析构 callback storage，Fn/trampoline 只从 stack 返回。no-wait-cycle 仅限声明脚本中的协议等待。Fn construction HB 到每次 invocation，storage destruction 在其他 invocation 的最后 storage access 之后。

库的“执行 selected trampoline”是假设，fused Fn 的单次调用、构造线程要求、storage lifetime 和上述 committed-delivery 条件是组合层独立检查的性质。请求的 registration 快照不依赖选择代码；`skip_retained` 故意跳过保留的节点，必须在 winning request 返回处失败。

两个输入并发请求停止时，输掉 exchange 的输入请求可在赢家线程进入 Fn 前返回；Fn 的 effects 与该返回之间没有 happens-before。输家不能等待赢家：赢家的 Fn 可能析构 callback，而析构等待仍在执行的输家 trampoline，形成等待环。需要完成保证的 requester 应与 **child completion** 同步。`u_retained_deadline` 隔离这个合法交错；baseline 的 committed delivery 通过，`entry_before_return` 则把“每个成功 request 返回前已进入 Fn”作为规范负对照并失败。

commitment 不等于 arbitration flag：`drop_dispatch` 赢得 exchange 并记录 commitment，但丢弃 Fn dispatch，由独立 state 性质检出。self-tests 还插入协议等待或删除 winner 的剩余脚本，并独立检查 terminal 未履约；只有 flag 而没有 commitment 不能满足 request-return 检查。

`literal_f3` 演示无条件 delivery 要求为何不成立：`u_pending_remove` 在 request 开始后合法注销尚未选中的 sibling；其 Fn 可不执行。正常配置取消该 registration 的 snapshot entry。`skip_retained` 则保留 registration 却跳过选择，必须由独立 request-return 检查检出。

全图无环时每条 execution 都结束；引擎的循环 completion reachability 不是所有 fair executions 的 liveness 证明。baseline 穷举全图直到各违例边界；通常 mutants/controls 在首个反例停止，`F_reverse_registration` 和 `F_destroy_fn_first` 继续检索，以分别保留 F2/F4 证据。`complete` 对失败配置表示完成该反例搜索，不能读作通过全部性质。

## 工具敏感性

[mutants.py](mutants.py) 列出 15 个 mutants。去锁的 decrement/start_join 在 SC 被 state property 杀死，destructor 去锁由 observer teardown 杀死；没有沿用合并模型的 survivor 解释。去锁 destructor 的 `f_destroy` 对照仍通过，因为 receiver continuation 已经继承 unlock 的同步；observer witness 不依赖这个同步。`F_reverse_registration` 按 member swap 先构造 downstream，再执行 input initializer 的 prepare，随后注册 input；析构次序随之反转。`u_pre0` 给出 F2 反例，`u_pre1` 是通过的反方向对照；该 mutant 整体被杀死。

七个 controls 为 lost join、缺少 queue release、正常 queue handoff、literal F3、跳过 retained registration、丢弃 Fn dispatch 和 entry-before-return 规范负对照。全部 baseline 配置通过；六个 controls 失败，正常 queue 对照通过。人工 sensitivity 逐配置声明，工具永不重写；未列出的配置必须通过，semantic expected records 还锁定类别和最短长度。

## 使用与维护

```bash
python3 -B formal/counting_scope/check.py --check-source
python3 -B formal/counting_scope/check.py --tier quick --jobs 8 --start-method spawn
python3 -B formal/counting_scope/check.py --tier full --jobs 8 --start-method spawn
python3 -B formal/counting_scope/check.py --tier full --only 'u_pre0'
```

CLI 与 stop_token 相同；worker 自行 import/configure，兼容 spawn。`--out` 指定 formal/ 外的结果目录，保存 results.jsonl、summary.md 和最短 traces。每配置 cap 为 300,000 states；不完整探索、outcome 不符或 catalog/sensitivity 矛盾不能通过维护事务。worker peak RSS 不等于 pool 总峰值。

只有整个 counting_scope.hpp 是 locked source。digest mode 为 `sha256-crlf-to-lf-bytes-v4`，保留所有注释、空白和预处理文本，仅将 CRLF 转 LF。每次编辑都要求资格运行与源码/模型对应 assessment。lock 是 drift detection，不是 fidelity proof。

任何 engine/model/CLI/self-tests 或 expected/sensitivity 改变要求 full；纯 C++ 改动经对应关系检查后可用 quick，但 checker/expected 必须沿用上次 full digests。生成 expected 和刷新 lock 是独立事务：

```bash
python3 -B formal/counting_scope/check.py --tier full --jobs 8 --start-method spawn --update-expected
python3 -B formal/counting_scope/check.py --tier full --jobs 8 --start-method spawn --refresh-lock --assessment '说明源码与模型对应关系、已知反例及抽象边界'
python3 -B formal/counting_scope/check.py --check-source
```

事务拒绝 `--only`、空集、未完成配置、矛盾 outcomes、expected mismatch 和空 assessment；update/refresh 不可合用，采用同目录临时文件 atomic rename。expected 包含 states、transitions、terminals、outcome 及 semantic SHA-256，不包含性能时间和 RSS。

`--check-source` 运行 engine lexical/fragment/事务 self-tests 和本模型的重复 body、边界、coverage、publication、coherence 及 constructor lifetime self-tests。matcher 支持 attribute 列表、以已定义 class 为根的 namespace-block nested class head，以及以 `!Concept` 开始、后接 `&& Concept` 的括号 constraint；concept 路径不接受 keywords。共享 engine 的变化也要求 stop_token 独立 full qualification，不能用本模型 lock 代替。

## 最近一次完整检验

完整配置与资源数据随 [source.lock](source.lock) 的 full qualification 维护。结果区分通过、预期 mutant/control 反例和 strict abort。

| Tier | 配置数 | Workers / start | Runner wall s | 最大 worker MiB | States | Transitions |
|---|---:|---|---:|---:|---:|---:|
| full | 455 | 8 / spawn | 5.830 | 50.57 | 136,769 | 282,616 |
| quick | 109 | 8 / spawn | 1.838 | 49.26 | 37,637 | 78,902 |

2026-11-16，Python 3.13.13。full 中 419 通过、36 预期反例：414 个 baseline 全部通过，34 个 mutant 配置中 30 个失败、四个 witness 对照通过，七个 controls 中六个失败、正常 queue 对照通过。15 个 mutants 都有 killer；strict abort terminals 共 46 个。最大配置为 `base.s_fused.counting_scope.queued.sc_hb`，6,225 states。runner wall 含 pool 启动，但不含 CLI 入口的模型 self-tests；full 从 CLI 启动到结束共 7.681 s。未测量 pool peak，实际资源随机器和负载变化。
