> [📖 Docs](../README.md) › 概览 › Signal 生命周期

# Signal 生命周期

## 完整流程

```
┌──────────────┐
│  on_create() │  注册 Signal，分配回调表
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ initialize() │  读取 yaml config section，初始化内部状态
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  set_apis()  │  接收引擎 API 句柄（用于 update_signal 等操作）
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ load_state() │  如配置 checkpoint load，从磁盘恢复状态
└──────┬───────┘
       │
       ▼
  ┌──────────────────────────────┐
  │        每天循环               │
  │  ┌──────────────────────┐    │
  │  │      on_sod()        │    │  日初：重置计数器、获取标的列表
  │  │  on_<marketdata>()   │    │  行情回调（每次行情到达触发）
  │  │  on_<marketdata>()   │    │
  │  │        ...           │    │
  │  │      on_eod()        │    │  日末：汇总统计、最后信号
  │  └──────────────────────┘    │
  └──────────────────────────────┘
       │
       ▼
┌──────────────┐
│ save_state() │  如配置 checkpoint save，持久化状态到磁盘
└──────┬───────┘
       │
       ▼
┌──────────────┐
│  ~Signal()   │  析构
└──────────────┘
```

## 回调函数详解

### on_create

引擎加载动态库时调用的入口函数。必须实现为自由函数（非成员函数）：

```cpp
// C++
void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;  // 填充回调表
}
```

```python
# Python
def pysig_create():
    return MySig()
```

### initialize

在 `load_state` 之前调用，用于读取 yaml 配置文件中的 `signal.config` 段，初始化内部数据结构。

```cpp
void Signal::initialize(const Config *root)
{
  auto seed = (*root)["seed"].as<double>();
}
```

### set_apis

引擎注入 API 句柄，Signal 可调用以下方法：
- `apis_.update_signal(token, exchtime, localtime, ins_nr, sigs)` — 输出信号
- `apis_.get_factor_list(token, &children)` — 获取因子列表
- `apis_.get_factors(token, &ins_nr, &factor_nr)` — 获取因子值
- `apis_.get_feature_buf(token, name)` — 获取 feature buffer

### on_sod（日初）

每个交易日开始时调用。典型操作：
- 重置计数器
- 获取标的列表（`SodEvent::ms[]`）
- 预分配信号数组
- 调用 FactorNode/Mbo 的 `on_day_begin()`

### 行情回调

根据配置的 `marketdata.module` 类型，引擎调用不同的回调函数：

| 事件类型 | 回调函数 | 数据维度 |
|----------|---------|---------|
| `CsSnapshotEvent` | `on_cs_snapshot()` | 所有标的同时推送 |
| `CsMboEvent` | `on_cs_mbo()` | 所有标的的逐笔成交 |
| `SnapshotEvent` | `on_snapshot()` | 单标的多档快照 |
| `FullSnapshotEvent` | `on_full_snapshot()` | 单标的完整深度快照 |
| `TxSnapshotEvent` | `on_tx_snapshot()` | 单标的快照 + 逐笔成交明细 |
| `CryptoTradeEvent` | `on_crypto_trade()` | 加密货币逐笔成交 |

### on_eod（日末）

每个交易日结束时调用。典型操作：
- 汇总日频统计
- 调用 FactorNode/Mbo 的 `on_day_end()`
- 发送最终信号

### load_state / save_state

Checkpoint 机制用于跨日状态的持久化与恢复。详见 [Checkpoint 指南](../guides/checkpoint.md)。

## Python 与 C++ 对应关系

Python Signal 继承 `SignalBase`，方法名完全对应：

| C++ | Python |
|-----|--------|
| `void initialize(const Config*)` | `initialize(self, cfg_str: str)` |
| `void set_apis(SignalApis)` | 内置在 `SignalBase` 中 |
| `void on_sod(const SodEvent*)` | `on_sod(self, ev: SodEvent)` |
| `void on_eod(const EodEvent*)` | `on_eod(self, ev: EodEvent)` |
| `void on_cs_snapshot(const CsSnapshotEvent*)` | `on_cs_snapshot(self, ev: CsSnapshotEvent)` |
| `void on_snapshot(const SnapshotEvent*)` | `on_snapshot(self, ev: SnapshotEvent)` |
| `void load_state(const string&)` | `load_state(self, path: str)` |
| `void save_state(const string&)` | `save_state(self, path: str)` |
| `apis_.update_signal(...)` | `self.update_signal(exchtime, localtime, sigs)` |

Python 额外支持 Cython 加速（`.pyx` 文件），使用 `cimport` 导入 C 原生类型提升性能。