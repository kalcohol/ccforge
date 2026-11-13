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
