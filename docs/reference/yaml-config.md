> [📖 Docs](../README.md) › 参考 › YAML 配置参考

# YAML 配置参考

wlsim 使用 YAML 文件作为配置入口。所有 demo 都包含一个或多个 `wlsim.yml` 配置文件。

## 顶层结构

```yaml
env: {}                    # 环境变量
start: 20230103            # 回测起始日期 (YYYYMMDD)
end: 20230104              # 回测结束日期 (YYYYMMDD)
calendar: /path/to/cal     # （可选）自定义交易日历
refdata: {}                # 参考数据配置
worker: {}                 # （可选）工作线程配置
checkpoint: {}             # （可选）Checkpoint 配置
features: []               # （可选）特征定义
marketdata: []             # 行情数据源配置（必需）
signal: {}                 # Signal 配置（必需）
```

## 配置段详解

### env

环境变量，用于控制运行时行为：

```yaml
env:
  print_exec_time: true    # 打印执行时间
```

### start / end

回测日期范围，格式 `YYYYMMDD`：

```yaml
start: 20230103
end: 20230110
```

### calendar

（可选）自定义交易日历文件路径：

```yaml
calendar: /mnt/nas-3/cta/Data/CryptoTradingDates.txt
```

### refdata

参考数据配置：

```yaml
refdata:
  ticker_mapping: HFT      # 品种映射
```

### worker

工作线程配置：

```yaml
worker:
  type: pool               # 线程池模式
  threads: 128             # 线程数
```

### checkpoint

跨日状态持久化配置。详见 [Checkpoint 指南](../guides/checkpoint.md)。

```yaml
checkpoint:
  dir: checkpoint           # 存放目录
  save:                     # 保存指定日期的 checkpoint
    - 20230103
  load: 20230103            # 加载指定日期的 checkpoint
  auto_load: yesterday      # 自动加载策略
```

### features

特征定义，用于预计算并缓存特征数据：

```yaml
features:
  - name: ft1                               # 特征名称
    module: system.ftreader                 # 特征读取器模块
    output:
      module: parquet                       # 输出格式
      config:
        output_dir: output
    config:
      format: parquet                       # 输入格式
      dir: output                           # 输入目录
      provides:                             # 提供的特征名列表
        - order_cnt
        - order_pxmean
```

### marketdata

行情数据源配置，支持多个数据源同时使用：

```yaml
marketdata:
  - module: cs-snapshot           # 横截面快照
    symbols:
      - stocks.CHN                # 标的集
    config:
      fields:                     # 需要的字段
        - last_price
        - volume
        - ap
      levels: 5                   # 深度档位
    features:                     # （可选）关联的特征
      - ft1
    client:                       # 消费者列表
      - strat
      - fct1

  - module: cs-mbo                # 横截面逐笔成交
    symbols:
      - stocks.CHN
    config:
    client:
      - my_signal

  - module: snapshot              # 时间序列快照
    symbols:
      - ZC.CME
    config:
      dataset: cme                # 数据集名称
    client:
      - operators

  - module: crypto-trade          # 加密货币逐笔成交
    symbols:
      - ETHUSDT.BIANUM
    config:
      dataset: binance-mm-tick.v3
    client:
      - trade
```

**行情模块类型：**

| 模块名 | 模式 | 说明 |
|--------|------|------|
| `cs-snapshot` | 横截面 | 所有标的 N 档快照 |
| `cs-mbo` | 横截面 | 所有标的逐笔成交 |
| `snapshot` | 时间序列 | 单标的 N 档快照 |
| `full-snapshot` | 时间序列 | 单标的完整深度快照 |
| `tx-snapshot` | 时间序列 | 快照 + 逐笔成交明细 |
| `crypto-trade` | 时间序列 | 加密货币逐笔成交 |

### signal

Signal 配置：

```yaml
signal:
  name: my_signal                # 唯一标识符
  module: namespace.classname    # 模块路径（C++：命名空间.类名；Python：模块名）
  is_python: false               # 是否为 Python 实现
  output:
    module: csv                  # csv / npy / parquet
    config:
      output_dir: output
  factors:                       # （可选）因子列表
    - name: fct1
      module: system.sigreader   # 或自定义 factor 模块
      config:
        name: fct1
        format: csv
        dir: output
  config:                        # 传递给 initialize() 的自定义配置
    targets:                     # 目标标的
      - dynamic:stocks.CHN
      - ZC.CME
    seed: 1.234                  # 用户自定义参数
    formula: "ts_sum(@bp1, 5)"   # 用户自定义参数
```

**targets 格式：**

| 格式 | 说明 |
|------|------|
| `dynamic:stocks.CHN` | 动态标的集（从行情数据中获取） |
| `ZC.CME` | 固定标的（ticker.exchange） |
| `ETHUSDT.BIANUM` | 加密货币标的 |

**factor 类型：**

| module | 说明 |
|--------|------|
| `system.sigreader` | 回放已保存的信号文件 |
| `namespace.factor_class` | 自定义 C++ factor 实现 |
| `namespace.factor_module` | 自定义 Python factor 模块 |

## 完整示例

### 横截面策略

```yaml
env: {}
start: 20230703
end: 20230703
refdata: {}

worker:
  type: pool
  threads: 128

marketdata:
  - module: cs-snapshot
    symbols:
      - stocks.CHN
    config:
      levels: 10
      fields:
        - last_price
        - volume
        - ap
        - bp
    client:
      - my_signal

signal:
  name: my_signal
  module: myns.mysig
  output:
    module: csv
    config:
      output_dir: output
  config:
    targets:
      - dynamic:stocks.CHN
```

### 时间序列策略

```yaml
env: {}
start: 20250106
end: 20250107
refdata:
  ticker_mapping: HFT

marketdata:
  - module: snapshot
    symbols:
      - ZC.CME
    config:
      dataset: cme
    client:
      - operators

signal:
  name: operators
  module: nickchenyj.operators
  output:
    module: csv
    config:
      output_dir: output
  config:
    targets:
      - ZC.CME
```