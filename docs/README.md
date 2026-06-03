# CC Forge documentation

根目录概览：
[English](../README.md) | [简体中文](../README.zh-CN.md) | [日本語](../README.ja.md)

这个目录放置更深入的 design notes、caveats、verification guidance 和 roadmap
material。根目录 README 是稳定的多语种入口；`docs/` 下的用户向文档采用中文技术文章
风格：正文解释以中文为主，API、类型、header、CMake option、test target、标准术语和
外部项目名保留英文原名，并用代码格式或原始拼写表达。

`roadmap/` 下的规划和审计文档可以保持英文，因为它们主要服务工程决策和 review
上下文，不是面向 newcomer 的教程。

只需要 standard-header backport 的 consumer 使用 `forge::std`；需要完整
`include/forge/` extension layer 的 consumer 使用 `forge::forge`。Standard-shaped
headers 只在 `backport/` 下保持无后缀，例如 `<execution>`；`forge::` extension
headers 刻意保留 `.hpp` 入口，例如 `<forge/io.hpp>`。

## usage

- [forge cookbook](forge-cookbook.md)

## feature docs

- [forge runtime lifecycle contract](forge-runtime.md)
- [`forge::` utilities](forge-utilities.md)
- [`forge::io` backend](forge-io.md)
- [`forge::accel` runtime vocabulary and mock backend](forge-accel.md)
- [`forge::erased_sender` design and limits](forge-erased-sender-design.md)

## integration and verification

- [native handoff 与无感注入](native-handoff.md)
- [测试与验证](testing.md)

## backports

- [`std::execution`](backports/execution.md)
- [`std::linalg`](backports/linalg.md)
- [`std::simd`](backports/simd.md)
- [`std::submdspan` / `std::constant_wrapper`](backports/mdspan.md)
- [`std::unique_resource`](backports/unique_resource.md)

## roadmap

- [forge stability baseline](roadmap/forge-stability-baseline.md)
- [forge runtime vision](roadmap/forge-runtime-vision.md)
- [backend proof policy](roadmap/forge-backend-proof-policy.md)
- [`forge::io` backend SPI sketch](roadmap/forge-io-backend-spi.md)
- [`forge::accel` backend SPI sketch](roadmap/forge-accel-backend-spi.md)
- [`forge::accel` runtime design](roadmap/forge-accel-runtime-design.md)
- [`forge::accel` runtime v2 roadmap](roadmap/forge-accel-runtime-v2.md)（historical）
- [`forge::accel` runtime v3 roadmap](roadmap/forge-accel-runtime-v3.md)（historical）
- [`std::execution` conformance ledger](roadmap/execution-conformance-ledger.md)
- [`std::execution` current-WD convergence checklist](roadmap/execution-wd-convergence-checklist.md)
