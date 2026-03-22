# Learnings

## [2026-03-22] Session Bootstrap
- Phase 2 COMPLETE: execution Phase 1-3 + linalg BLAS + simd backport
- Terminal session doubles ALL command output (display bug, not execution bug)
- Python scripts with `if old in content` are idempotent despite display doubling
- cat > file << 'EOF' writes ONCE (idempotent, second write overwrites)
- Subagents with SINGLE TASK ONLY guard refuse multi-task prompts — use unspecified-high category
- Files are not doubled; terminal display is doubled

## Architecture
- backport/cpp26/simd/types.hpp: 819 lines, basic_vec uses std::array<T,N> data_ (pure scalar)
- backport/cpp26/linalg.hpp: 1154 lines monolithic — needs split
- backport/cpp26/execution/ — 28 sub-files (Phase 1-3 complete)
- include/forge/ — forge namespace extensions (res_guard.hpp pattern)
- forge.cmake: uses FORGE_* variables, forge::forge target (KEEP THESE NAMES)

## Key Decisions
- Rename: user-visible text only; FORGE_* vars and forge::forge target stay unchanged
- simd Layer 1: vector_size attribute + constexpr dual path (is_constant_evaluated)
- any_sender → include/forge/any_sender.hpp in namespace forge
- P3149 target R11 (associate/scope_token/simple_counting_scope/counting_scope)
- OpenMP: #ifdef _OPENMP ONLY, Zig has no OpenMP
- linalg split: 5 sub-files; submdspan.hpp stays monolithic
