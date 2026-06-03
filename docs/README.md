# CC Forge documentation

Root overview:
[English](../README.md) | [简体中文](../README.zh-CN.md) | [日本語](../README.ja.md)

This directory holds the deeper design notes, caveats, verification guidance,
and roadmap material. These pages intentionally remain mixed English/Chinese
where that is clearer for current contributors; the root README files are the
stable multilingual entrypoints.

Use `forge::std` for standard-header-only consumers and `forge::forge` for the
full `include/forge` extension layer. Standard-shaped headers are extensionless
only under `backport/`; `forge::` extension headers intentionally keep `.hpp`
entries such as `<forge/io.hpp>`.

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
- [`forge::accel` runtime v2 roadmap](roadmap/forge-accel-runtime-v2.md)
- [`std::execution` conformance ledger](roadmap/execution-conformance-ledger.md)
- [`std::execution` current-WD convergence checklist](roadmap/execution-wd-convergence-checklist.md)
