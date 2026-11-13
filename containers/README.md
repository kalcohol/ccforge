# 验证镜像

lane 内容、运行命令、串行要求与支持范围统一见[测试说明](../docs/testing.md)。

| 目标 | 镜像 | Containerfile |
| --- | --- | --- |
| llvm | forge-llvm | Containerfile.llvm |
| gcc16 / gcc-exec | forge-gcc16 | Containerfile.gcc16 |
| zig | forge-zig | Containerfile.zig |
| tsan | forge-tsan | Containerfile.tsan |
| asan | forge-asan | Containerfile.asan |
| local | 主机工具链 | 无 |
