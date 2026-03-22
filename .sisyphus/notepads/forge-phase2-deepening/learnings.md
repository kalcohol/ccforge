# Learnings

## [2026-03-21] Session Bootstrap
- Phase 1 complete: execution Phase 1+2 + linalg BLAS Level 1/2/3 (all scalar)
- Terminal session doubles ALL command output (display bug, not execution bug)
- Python scripts with conditions (if old in content) are idempotent despite display doubling
- Commands like printf >> can execute TWICE — use python3 for reliable single-writes
- let.hpp uses 1024B fixed buffer — will overflow for when_all with N child op-states
- All execution sub-headers under backport/cpp26/execution/
- linalg under backport/cpp26/linalg.hpp (monolithic, ~1054 lines)
- 4-toolchain: GCC 13 (no mdspan), Zig (has mdspan), Podman Clang, Podman GCC
- Cross-arch verified: zig cc -target {aarch64,riscv64,loongarch64}-linux-musl + qemu-static all work
- GCC 13 has NO <mdspan> — linalg tests skip on GCC 13, pass on Zig
- simd backport: scalar array loops + compiler auto-vectorization (no intrinsics)
- simd base.hpp: native_lane_count uses ISA macros (AVX512=64B, AVX2=32B, SSE/NEON=16B, fallback=1)

## [T2] CPO dispatch bridge
- connect_t/start_t/get_completion_signatures_t now try member function first
- connect_result_t updated to use decltype(connect_t{}(...))
- tag_invoke path still works for all Phase 1/2 senders
- if constexpr dispatches at compile time based on member availability

## [T3] transform_completion_signatures + cartesian product metafunctions

- Added to `namespace __forge_meta` inside `backport/cpp26/execution/concepts.hpp`
- `__concat_cs<CS1, CS2, ...>` — recursive pack concat via partial specialization; empty base returns `completion_signatures<>`
- `__sig_map<Sig, V, E, S>` — free-standing template (not member class) to avoid explicit specialization inside class restriction; partial specializations for `set_value_t(Vs...)`, `set_error_t(E)`, `set_stopped_t()`
- `transform_completion_signatures<CS, V, E, S>` — expands Sigs... through `__sig_map` then `__concat_cs_t`; default template params: V=empty, E=set_error_t(exception_ptr), S=set_stopped_t()
- `__single_value_tuple<CS>` — pattern-matches first `set_value_t(Vs...)` sig via partial specializations; fallback `Other` case recurses; result is `tuple<decay_t<Vs>...>`
- `__tuple_cat_type<T1, T2, ...>` — type-level `std::tuple_cat`; recursive partial specializations on `tuple<Ts...>, tuple<Us...>, Rest...`
- `__cartesian_product_value_sigs<CS1, CS2, ...>` — combines `__single_value_tuple_t` of each CS into one tuple via `__tuple_cat_t`, then wraps in `completion_signatures<set_value_t(...)>`; used by `when_all` to build joint value signature
- LSP (clangd) shows false-positive errors from `detail.hpp` using `requires`/`concept` keywords — these are pre-existing and harmless; actual GCC/Clang builds clean
- 43/43 tests pass after addition; compile probe static_asserts verified: concat and cartesian product both correct

## [T4+T5] Test structure reorganization

- execution compile probe: forge_add_execution_compile_probe must also link TBB (same as runtime tests); without it, link fails on tbb::detail symbols even for header-only static_asserts
- linalg: test_linalg_core.cpp renamed to test_linalg_auxiliary.cpp (git detects as rename at 100% similarity); stub level1/2/3 tests added with SUCCEED()
- linalg tests skip entirely on GCC 13 (no <mdspan>) — level stubs also skip; on Zig they will run
- 44/44 tests pass after reorganization (1 new compile probe: execution_api_core_compile)
