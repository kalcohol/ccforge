# Learnings

## [2026-03-21] Session Bootstrap
- 项目使用 GTest（gtest_main） + compile probes 两种测试
- backport/cpp26/execution.hpp 当前 1325 行，所有符号在 namespace std 或 namespace std::execution
- backport/cpp26/simd/ 子目录模式：simd.hpp 为协调头，内部拆分为 simd/base.hpp, simd/types.hpp 等
- forge.cmake 使用 check_cxx_source_compiles() 探测原生支持
- 4 工具链验证：本地 GCC、zig cc/c++、podman clang:latest、podman gcc:latest
- tag_invoke 迁移决策：保留现有 MVP 类型（just/then/sync_wait/inline_scheduler）的 tag_invoke，新类型使用成员函数优先
- run_loop：使用 mutex+cv（非 atomic::wait），确保 zig/libc++ 兼容性

## [T2+T1] File split + enable_sender/get_completion_scheduler (commit 2d7ec38)
- execution.hpp split into 8 sub-headers under backport/cpp26/execution/
- detail.hpp: __forge_detail (tag_invoke protocol) — CRITICAL: must close 'namespace std::execution' at end
- stop_token.hpp: namespace std { inplace_stop_* + stoppable concepts } — standalone, no sub-include deps
- env.hpp: CPOs + get_completion_scheduler (T1 addition)
- concepts.hpp: completion_signatures, meta, receiver/sender/operation_state/scheduler + enable_sender
- enable_sender MUST use nested requires: `requires { requires std::derived_from<...>; }` not direct derived_from<T::sender_concept, X> (fails for non-class T like int)
- sync_wait.hpp: needs 'namespace std::execution {}' wrapper around __forge_sync_wait block
- Coordinator execution.hpp: ~68 lines, all std headers + 8 sub-header includes
- git command got doubled in shell heredoc — use Python for file construction when precision needed

## [T3] run_loop implementation (commit 3affce7)
- run_loop uses mutex+cv queue (not atomic::wait) for zig/libc++ portability
- __task_base is intrusive singly-linked list node with function pointer (__execute)
- __push enqueues + notifies cv; __pop blocks until task or finishing_ true
- run() calls __pop() in loop, executes, decrements __task_count_
- destructor calls terminate() if __task_count_ > 0
- scheduler nested class uses tag_invoke pattern (consistent with inline_scheduler)
- __forge_run_loop::__env provides get_scheduler query for sender's env
- operation_state __op inherits __forge_detail::__immovable
- __task_base member must be in public section (accessible by __op via pointer cast)
- Avoid shell heredoc doubling: use python3 -c for file manipulation, not bash heredoc
