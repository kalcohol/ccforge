# 协议模型检验

`formal/` 保存 formal-method artifacts，包括 Python explicit-state models，后续也可容纳 TLA+ specifications 或真实代码 harness。这里的检验独立于 `test/`、CMake test build 和 container lanes；有限模型不能替代 C++ 测试、sanitizer 和平台验证。

## 目录与约定

`engine/` 是标准库实现的小型内部搜索工具，提供 BFS、全图 completion reachability、trace、并行 runner、source lock 和通用 memory machinery。模型目录保存状态与转移、场景、mutant catalog、tier manifests、逐配置 expected records 和 source lock。搜索器不解释协议字段或 step id；memory client 通过约定的 clocks、histories、observed bounds 和 access records 保存同步状态。

Python 模型要求 Python 3.9+，统一提供以下 CLI：

```bash
python3 -B formal/stop_token/check.py --check-source
python3 -B formal/stop_token/check.py --tier quick
python3 -B formal/stop_token/check.py --tier full
python3 -B formal/stop_token/check.py --steps
```

`--check-source` 不探索状态，检查字节级 source digest（仅将 CRLF 转成 LF）、checker digest、expected-data digest 及函数内唯一 fragment；文档审计运行此项。verification floor 默认运行 quick。结果写入 ignored 输出目录，只有显式维护事务可改写 expected 或 lock。

## 模型索引

| 模型 | 范围 | 最近 full check |
|---|---|---|
| [spawn_future](spawn_future/README.md) | eager producer、single consumer、取消、shared ownership、allocator/association 顺序与 HB | 2026-11-16：1,666 配置 |
| [counting_scope](counting_scope/README.md) | 两种 scope 的 join、scope stop、fused callback 生命周期、HB 与 committed delivery | 2026-11-16：455 配置 |
| [stop_token](stop_token/README.md) | inplace_stop_source 锁、回调生命周期、completion wait/notify | 2026-11-16：7,577 配置 |

[source.lock](stop_token/source.lock) 同时保存最近资格运行和上次 full 的日期、配置数、结果摘要及指纹。头文件的每次编辑（包括注释和空白）都要求刷新 lock；source digest 是仅将 CRLF 转成 LF 后的头文件字节的 SHA-256，不删除注释或空白。C++ 变化不需要修改模型时可用 quick 重新确认；纯注释或纯空白改动也可用 quick，assessment 必须明确改动类型并说明模型为何不变。任何 checker 文件（包括 CLI）或 expected 数据变化都要求 full；quick 只可沿用上次 full 锁定的 checker 和 expected-data digests。Python 不变时 full 会重现相同结果，因此 C++ 单独变化的真正保障是 assessment 中的对应关系审查，tier 本身不能证明 fidelity。

## 添加模型

创建独立模型目录及 README，声明边界、内存与库假设、性质、mutants 和 negative controls。Python 模型采用相同 CLI，并提供 configure、initial、key/state、successors、state/terminal violations、blocked/aborted、describe、describe_initial、metrics 与 coverage callbacks（initial rendering 未提供时采用中性默认值）。保持 successor 顺序确定，worker 自行初始化配置；一个 expected record 对应一个完整配置。

镜像仓库代码的任何 artifact 均须提供等价的 drift detection 和 fidelity review 记录；其他形式的模型可采用自己的 runner，无需套用 Python 引擎。加入模型后检查 fail-closed discovery、审计和 floor，并给出可复现命令及资源边界。
