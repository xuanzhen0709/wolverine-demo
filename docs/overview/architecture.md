> [📖 Docs](../README.md) › 概览 › 架构概览

# 架构概览

## 仓库结构

```
wolverine-demo/
├── CMakeLists.txt              # 顶层 CMake（依赖 wolverine >= 2.4.0, cfi_operators）
├── conanfile.txt               # Conan 包管理
├── run_build.sh                # 一键构建脚本
├── cmake/version.cmake         # 从 git tag 自动提取版本号
├── demos/                      # 示例实现
│   ├── CMakeLists.txt          # add_all_subdirectories()
│   ├── checkpoint/             # C++ checkpoint 示例
│   ├── checkpoint-py/          # Python checkpoint 示例
│   ├── crypto/                 # 加密货币 trade 数据
│   ├── cssnap/                 # 横截面 snapshot + 信号
│   ├── csmbo/                  # 横截面 MBO 数据
│   ├── csops/                  # 横截面 + FactorNode 算子
│   ├── feature-factor/         # 特征工厂（含因子嵌套）
│   ├── full-snapshot/          # 全量深度快照
│   ├── crossref/               # 多标的配对/参考
│   ├── operators/              # Operators 时序算子
│   ├── ops_feature/            # Feature + CsSnapshot 自适应字段
│   ├── ops-mbo/                # Mbo 算子 + CsMbo
│   ├── signal_reader/          # SignalReader 回放
│   ├── snapshot/               # 时间序列 snapshot
│   ├── snapshot-py/            # Python 版 snapshot
│   └── tx_snapshot/            # 交易 snapshot（含逐笔成交）
├── tools/                      # 分析工具
│   ├── pnl_calculator/         # PnL 计算器
│   ├── edge_calculator/        # Edge 收益计算器
│   ├── ic_calculator/          # IC 信息系数计算器
│   ├── ts_ic_calculator/       # 时间序列 IC
│   ├── cs_pnl_calculator/      # 横截面 PnL
│   ├── cs_edge_calculator/     # 横截面 Edge
│   ├── cs_ic_calculator/       # 横截面 IC
│   └── utils/                  # 公共工具（日历、数据缓存、信号文件类型）
├── scripts/                    # 运维脚本（运行时安装由 Conan 管理，见 build-and-install.md）
│   ├── validate_checkpoint.py  # Checkpoint 正确性验证
│   ├── run_pnl_calculator.py   # 运行 PnL 计算
│   ├── run_edge_calculator.py  # 运行 Edge 计算
│   ├── run_ic_calculator.py    # 运行 IC 计算
│   └── ...                     # 统计 / 转换 / 可视化脚本
└── docs/                       # 文档
```

## 核心概念

### Signal

Signal 是用户策略的基本单元。每个 Signal 实现通过 `on_create()` 注册到 wolverine 引擎，引擎通过回调函数调用用户逻辑。

**C++ 实现模式：**
```cpp
class Signal {
  void initialize(const Config *root);   // 初始化配置
  void set_apis(SignalApis);             // 设置引擎 API
  void on_sod(const SodEvent *ev);       // 日初
  void on_eod(const EodEvent *ev);       // 日末
  void on_cs_snapshot(const CsSnapshotEvent *ev);  // 横截面行情
  void load_state(const std::string &dir);   // 加载 state
  void save_state(const std::string &dir);   // 保存 state
};

// 注册入口
void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;  // 回调函数表
}
```

**Python 实现模式：**
```python
class MySig(SignalBase):
    def initialize(self, cfg_str: str): ...
    def on_sod(self, ev: SodEvent): ...
    def on_eod(self, ev: EodEvent): ...
    def on_cs_snapshot(self, ev: CsSnapshotEvent): ...

def pysig_create():
    return MySig()
```

### 数据模式

| 模式 | 事件类型 | 说明 |
|------|---------|------|
| 横截面 (CS) | `CsSnapshotEvent`, `CsMboEvent` | 批量推送所有标的，适合股票横截面策略 |
| 时间序列 (TS) | `SnapshotEvent`, `FullSnapshotEvent`, `TxSnapshotEvent` | 按标的分开推送，适合期货单品种策略 |

### 配置驱动

所有 demo 通过 `wlsim.yml` 配置文件驱动，指定：
- 回测日期范围（`start` / `end`）
- 行情数据源（`marketdata`）
- Signal 模块与参数（`signal`）
- Checkpoint 策略（`checkpoint`）

## 依赖关系

```
wolverine-demo
├── wolverine (>= 2.4.0)   — 仿真引擎核心
├── cfi_operators           — 时序算子库（Operators / FactorNode / Mbo / Feature）
├── Conan                   — 包管理器
└── CMake / Ninja           — 构建系统
```