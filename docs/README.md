> [📖 Docs](../README.md) › 文档索引

# wolverine-demo 文档

wolverine 仿真平台（wlsim）的 demo 仓库，展示 signal、factor、strategy 的 C++ 与 Python 实现，覆盖横截面（cross-sectional）和时间序列（time-series）两种行情数据模式。

文档按 [docs-structure](conventions/docs-structure.md) 标准组织：overview / guides / reference / conventions。

## 概览 (Overview)

| 文档 | 说明 |
|------|------|
| [架构概览](overview/architecture.md) | 仓库整体架构、模块划分、数据流 |
| [框架架构](overview/framework.md) | wolverine + cfi_operators 的插件模型、事件、数据范式、算子库总览 |
| [Signal 生命周期](overview/signal-lifecycle.md) | Signal 从创建到销毁的完整生命周期 |

## 指南 (Guides)

| 文档 | 说明 |
|------|------|
| [构建与安装](guides/build-and-install.md) | 环境搭建（Conan 运行时）、编译、运行 |
| [编写一个 Signal](guides/writing-a-signal.md) | C++ / Python 实现 Signal 的完整步骤 |
| [使用 Checkpoint](guides/checkpoint.md) | 状态持久化与跨日恢复 |
| [使用 Operators](guides/operators.md) | 使用 cfi_operators 进行时序/横截面计算 |
| [使用 Feature](guides/feature.md) | 特征工厂与因子组合 |
| [横截面数据处理](guides/cross-sectional-data.md) | CsSnapshot / CsMbo 事件处理 |
| [时间序列数据处理](guides/time-series-data.md) | Snapshot / FullSnapshot / TxSnapshot |

## 参考 (Reference)

| 文档 | 说明 |
|------|------|
| [框架 API 参考](reference/framework-api.md) | wolverine / cfi_operators 头文件级签名速查 |
| [Demo 清单](reference/demo-catalog.md) | 所有 demo 的完整列表与说明 |
| [YAML 配置参考](reference/yaml-config.md) | wlsim.yml 配置项说明 |
| [工具链参考](reference/tools.md) | tools/ 目录下各分析工具说明 |
| [脚本参考](reference/scripts.md) | scripts/ 目录下各脚本说明 |

## 规范 (Conventions)

| 文档 | 说明 |
|------|------|
| [C++ 命名规范](conventions/cpp-naming.md) | 成员变量 `snake_case_`、命名与格式化约定 |
| [文档结构标准](conventions/docs-structure.md) | 文档组织约定 |

## 路线图

参见 [roadmap.md](roadmap.md)