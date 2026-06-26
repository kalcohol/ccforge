# CC Forge 文档

根目录概览：
[English](../README.md) | [简体中文](../README.zh-CN.md) | [日本語](../README.ja.md)

这个目录放置更深入的 design notes、caveats、verification guidance 和 roadmap
material。根目录 README 是稳定的多语种入口；`docs/` 下的用户向文档采用中文技术文章
风格：正文解释以中文为主，API、类型、header、CMake option、test target、标准术语和
外部项目名保留英文原名，并用代码格式或原始拼写表达。

`roadmap/` 下的规划和审计文档也采用同样的中文技术文体，但会保留更多标准条款、
verification lane 和 backend proof 的英文术语，便于 review 时精确引用。

只需要 standard-header backport 的 consumer 使用 `forge::std`；需要完整
`include/forge/` extension layer 的 consumer 使用 `forge::forge`。Standard-shaped
headers 只在 `backport/` 下保持无后缀，例如 `<execution>`；`forge::` extension
headers 刻意保留 `.hpp` 入口，例如 `<forge/io.hpp>`。

## 使用入口

- [Forge cookbook](forge-cookbook.md)

## 功能文档

- [Forge runtime lifecycle contract](forge-runtime.md)
- [`forge::` 扩展工具](forge-utilities.md)
- [`forge::io` backend](forge-io.md)
- [`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)

## 集成与验证

- [Native handoff 与无感注入](native-handoff.md)
- [测试与验证](testing.md)

## Backport 文档

- [`std::execution`](backports/execution.md)
- [`std::linalg`](backports/linalg.md)
- [`std::simd`](backports/simd.md)
- [`std::submdspan` / `std::constant_wrapper`](backports/mdspan.md)
- [`std::unique_resource`](backports/unique_resource.md)

## Roadmap 文档

- [Forge 稳定性基线](roadmap/forge-stability-baseline.md)
- [Forge runtime 远景图](roadmap/forge-runtime-vision.md)
- [Coroutine-native byte IO 路线图](roadmap/coroutine-native-io.md)
- [Backend proof 策略](roadmap/forge-backend-proof-policy.md)
- [`forge::io` backend SPI 草案](roadmap/forge-io-backend-spi.md)
- [`std::execution` 一致性台账](roadmap/execution-conformance-ledger.md)
- [`std::execution` 当前 WD 收敛清单](roadmap/execution-wd-convergence-checklist.md)
- [Execution stop-token allocator 设计记录](roadmap/execution-stop-token-allocator-design.md)
