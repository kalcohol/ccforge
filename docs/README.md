# CC Forge Documentation

Root Overview:
[English](../README.md) | [简体中文](../README.zh-CN.md) | [日本語](../README.ja.md)

This directory holds the deeper design notes, caveats, verification guidance,
and roadmap material. These pages intentionally remain mixed English/Chinese
where that is clearer for current contributors; the root README files are the
stable multilingual entrypoints.

## Usage

- [Forge Cookbook](forge-cookbook.md)

## Backports

- [`std::execution`](backports/execution.md)
- [`std::linalg`](backports/linalg.md)
- [`std::simd`](backports/simd.md)
- [`std::submdspan` / `std::constant_wrapper`](backports/mdspan.md)
- [`std::unique_resource`](backports/unique_resource.md)

## `forge::` Runtime Utilities

- [`forge::` 扩展工具](forge-utilities.md)
- [Forge Runtime Lifecycle Contract](forge-runtime.md)
- [`forge::io` Backend](forge-io.md)
- [`forge::accel` Mock Command Backend](forge-accel.md)
- [`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)

## Integration and Verification

- [Native Handoff 与无感注入](native-handoff.md)
- [测试与验证](testing.md)

## Roadmap

- [Forge runtime 远景图](roadmap/forge-runtime-vision.md)
