# 安装与 CMake 选项

只消费标准接口时链接 `forge::std`，需要 Forge 扩展时链接 `forge::forge`。先设置 `CMAKE_CXX_STANDARD`，再引入 Forge，使 probe 与最终编译采用相同语言模式。

```sh
cmake -S . -B build/package -DCMAKE_CXX_STANDARD=26 -DFORGE_ENABLE_INSTALL=ON -DFORGE_BUILD_TESTS=OFF -DFORGE_BUILD_EXAMPLES=OFF
cmake --install build/package --prefix /tmp/ccforge-install
```

```cmake
set(CMAKE_CXX_STANDARD 26)
find_package(CCForge CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE forge::std)
```

通过 `CMAKE_PREFIX_PATH` 指向安装前缀。消费者不需要 GoogleTest；开发测试需要先初始化 `3rdparty/googletest`。

安装包在 consumer 配置时重新运行 probe。`find_package` 前未设置
`CMAKE_CXX_STANDARD` 会报告 `CC Forge requires an explicit CMAKE_CXX_STANDARD`；
接收 `forge::forge` / `forge::std` 编译要求的 C++ translation unit，其有效语言模式
与 probe 不一致时会在编译阶段报告 `CC Forge language standard mismatch`，列出两个
语言阶段与 probe 请求的标准，并提示修复设置。compile features 提升后与 probe 一致的
target 可以正常编译；仅通过 PRIVATE library 依赖、未接收 Forge 编译要求的 consumer
可使用其他标准。检查头在 consumer build tree 生成，适用于单配置与多配置 generator。
同一次 configure 中重复查找或引入 Forge 时，标准、force flags 和其他配置输入必须
一致（包括 deployment target、自定义 try-compile platform variables 和头文件搜索环境），
否则报告 `CC Forge configuration changed after first inclusion` 并列出变化项与前后值。
fingerprint 同时记录 toolchain 与 probe 头文件内容摘要，路径不变的内容变更也会触发冲突。
强制包含校验头对 C translation unit 完全无效。检查机制与源码消费一致，详见 [native handoff](native-handoff.md)。

| 选项 | 用途 |
| --- | --- |
| `FORGE_ENABLE_INSTALL` | 生成安装规则与 CMake package |
| `FORGE_INSTALL_CMAKEDIR` | package config 与 probe 的安装目录 |
| `FORGE_INSTALL_BACKPORTDIR` | backport headers 的安装目录 |
| `FORGE_ENABLE_FORGE_RUNTIME` | 仅控制 runtime 示例，不控制头文件或 runtime 测试 |
| `FORGE_ENABLE_FORGE_RESOURCE_POLICY` | 控制资源策略示例与相应测试 |
| `FORGE_ENABLE_FORGE_IO` | AUTO/ON/OFF，选择平台 backend |
| `FORGE_ENABLE_FORGE_IO_URING` | AUTO/ON/OFF，独立启用 Linux io_uring |

强制 `FORGE_FORCE_*_BACKPORT` 开关只用于诊断，会绕过原生检测并带来 ODR 风险，列表见 [native handoff](native-handoff.md)。测试组的 `FORGE_TEST_ENABLE_*` 与验证流程见[测试说明](testing.md)。打包应关闭测试和示例，避免开发依赖进入消费者配置。

## 版本策略

当前版本为 `0.1.0`，仍处于 1.0 之前的开发期；minor 版本变化可能破坏源码兼容性，不承诺跨 minor 兼容。生成的 package version 使用 `SameMinorVersion`：同 major、同 minor 且已安装版本不低于请求版本才兼容。项目尚无发布 tag，当前使用应固定提交。
