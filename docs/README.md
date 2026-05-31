# CC Forge documentation

Root overview:
[English](../README.md) | [简体中文](../README.zh-CN.md) | [日本語](../README.ja.md)

This directory holds the deeper design notes, caveats, verification guidance,
and roadmap material. These pages intentionally remain mixed English/Chinese
where that is clearer for current contributors; the root README files are the
stable multilingual entrypoints.

## usage

- [forge cookbook](forge-cookbook.md)

## backports

- [`std::execution`](backports/execution.md)
- [`std::linalg`](backports/linalg.md)
- [`std::simd`](backports/simd.md)
- [`std::submdspan` / `std::constant_wrapper`](backports/mdspan.md)
- [`std::unique_resource`](backports/unique_resource.md)

## `forge::` runtime utilities

- [`forge::` 扩展工具](forge-utilities.md)
- [forge runtime lifecycle contract](forge-runtime.md)
- [`forge::io` backend](forge-io.md)
- [`forge::accel` mock command backend](forge-accel.md)
- [`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)

## integration and verification

- [native handoff 与无感注入](native-handoff.md)
- [测试与验证](testing.md)

## roadmap

- [forge runtime 远景图](roadmap/forge-runtime-vision.md)
- [forge stability baseline](roadmap/forge-stability-baseline.md)
- [`forge::accel` backend SPI sketch](roadmap/forge-accel-backend-spi.md)
