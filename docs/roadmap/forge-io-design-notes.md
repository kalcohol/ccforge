# Forge IO 实现设计笔记



以下记录弃置仲裁、分配布局及既有验证方法；用户契约以 [IO 文档](../forge-io.md) 为准。

## 弃置仲裁

弃置（销毁悬挂中的 `io_task` 链）按迭代方式逐帧执行：每帧在 await 子 task 时记录
向下链接，销毁时沿链循环拆帧而不是经嵌套 awaitable 析构递归，深链弃置的 native
stack 占用是常数。该保证只覆盖 task 链本身；悬挂中的叶子 awaitable/operation 的
弃置语义按 backend 分为两类：

- 可安全弃置（poller 持有，库可同步撤销）：timer facade 与 epoll backend 的
  readiness/byte operation。弃置析构先原子认领完成权（poller 此后不可能再打进
  该 operation state），再从注册表摘除记录。若完成方先赢得认领，析构按投递
  线程区分：完成正发生在本调用栈上（正常 continuation 路径销毁自身 state）时
  直接放行以免自锁；另一线程的在途投递则被等待收尾——receiver 完成与 stop
  注册的拆除都严格先于析构返回。协程桥（`await_sender`）在析构时同时认领
  完成槽，迟到的投递只写结果成员、不会 resume 正在销毁的 coroutine。因此与
  完成"同一瞬间"的弃置也是安全结算的：析构返回后结果不再变化。
- task 链层面的弃置仲裁：链根 promise 记录 started/finalized 标记、在途 resume
  credit 与当前挂起所在的 generation-tagged 桥完成槽。完成槽状态存放在稳定的链根
  而不是 awaitable 对象内，连接失败会撤销发布，因此仲裁不会读取已结束生命周期的
  awaitable 成员。跨线程弃置先对完成槽做认领仲裁（认领成功则
  投递永不 resume；失败则等链推进到下一稳定态后重仲裁），并在任何拆帧前等在途
  resume 计数归零，保证 resume 调用回卷离开帧之前帧不消失。弃置发生在 resume
  链自己调用栈上（receiver 在完成回调内同步销毁 operation state，这是受支持
  用法，`when_all_results` 的自毁聚合即依赖它）时既不等待
  （自锁）也不当场拆帧（resume 仍在帧间回卷），而是把拆帧交给最后一个回卷完毕
  的 resume 尾部执行。初始 task start、sender bridge completion，以及 Forge 自带的
  backend-direct/erased-stream continuation 都使用同一 credit；sender operation-state
  先析构 task、后析构 receiver，使跨线程弃置等待期间 receiver 仍然有效。
- 快速失败（kernel 持有 borrowed buffer，无法同步撤销）：io_uring 与 IOCP 的
  in-flight operation。弃置违反 borrowed 契约，析构护栏 `std::terminate()`，
  见各 backend 一节。erased async stream 的悬挂 operation 同属此类。


## epoll 验证方法

epoll backend 的 operation state 支持安全弃置：销毁已 start 未完成的 operation
会认领完成权并从注册表摘除记录，poller 不会再打进已销毁的 state 或向借用
buffer 写入（见上文弃置语义两分类）。弃置与并发投递的对抗性交错（readiness
已触发、poller 正在投递、拥有方同时析构）由 512 轮扫窗压力测试在 readiness
与 byte 两条路径上钉住，sanitizer lane 下任何协议破坏都会硬失败。

## 帧布局

- 帧分配在帧尾保留一个 pointer 大小的 trailer 记录 owning resource；deallocate 与
  allocate 的 size/alignment 严格配对。全局路径同样带 trailer，即每帧一个指针的
  固定开销。
