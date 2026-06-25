> [📖 Docs](../README.md) › 参考 › 工具链参考

# 工具链参考

`tools/` 目录包含分析信号结果的计算工具，以 Cython（`.pyx`）编译模块形式提供，通过 Python 脚本调用。

## 工具列表

| 工具 | 目录 | 功能 | 脚本入口 |
|------|------|------|---------|
| PnL 计算器 | `pnl_calculator/` | 计算信号 PnL（支持横截面/时间序列） | `scripts/run_pnl_calculator.py` |
| Edge 计算器 | `edge_calculator/` | 计算执行收益 | `scripts/run_edge_calculator.py` |
| IC 计算器 | `ic_calculator/` | 信息系数计算 | `scripts/run_ic_calculator.py` |
| TS IC 计算器 | `ts_ic_calculator/` | 时间序列信息系数 | — |
| CS PnL 计算器 | `cs_pnl_calculator/` | 横截面 PnL 计算 | `scripts/run_cs_pnl_calculator.py` |
| CS Edge 计算器 | `cs_edge_calculator/` | 横截面执行收益 | `scripts/run_cs_edge_calculator.py` |
| CS IC 计算器 | `cs_ic_calculator/` | 横截面信息系数 | `scripts/run_cs_ic_calculator.py` |
| 公共工具 | `utils/` | 公共工具库 | — |

## PnL 计算器

**文件：** `tools/pnl_calculator/main.pyx`

计算信号 PnL（盈亏），支持多种执行价格类型和计算模式。

### 执行价格类型

| 类型 | 枚举值 | 说明 |
|------|--------|------|
| `mid` | `ExecPriceType.mid` | 中间价 (ap+bp)/2 |
| `close` | `ExecPriceType.close` | 收盘价，支持 bias 偏移 |
| `vwap` | `ExecPriceType.vwap` | 成交量加权平均价 |

### 计算模式

| 模式 | 说明 |
|------|------|
| `daily` | 每日独立计算，不考虑跨日持仓 |
| `continuous` | 连续计算，考虑跨日持仓和复权因子 |

### 数据模式

| 缓存类型 | 适用数据 |
|----------|---------|
| `CsSnapshotCache` | 横截面数据（需 stock_map 映射） |
| `TsSnapshotCache` | 时间序列数据（单标的独立） |

### PnL 公式

```
PnL_i = trade_PnL + hold_PnL
trade_PnL = (sig_i - sig_{i-1}) * (close_i / exec_price - 1)
hold_PnL = sig_{i-1} * (close_i / close_{i-1} - 1)
```

### 配置示例

```yaml
signame: "my_signal"
sigdir: "output"
file_type: "npy"
exec_price:
  - mid              # 中间价
  - close_1s         # 信号后 1 秒收盘价
  - vwap_5m          # 信号后 5 分钟 VWAP
output_dir: "pnl_output"
mode: "daily"
stock_map: "stock_map.xlsx"  # 仅横截面模式需要
```

## Edge 计算器

**文件：** `tools/edge_calculator/main.pyx`

计算价格优势（edge），即执行价格相对于 mid-price 的价差。

## IC 计算器

**文件：** `tools/ic_calculator/main.pyx`

计算信息系数（Information Coefficient），衡量信号值与未来收益的相关性。

### 横截面 IC（CS IC）

计算每个时间截面内所有标的的信号值与收益的秩相关系数。

### 时间序列 IC（TS IC）

计算单个标的的信号值与未来收益的时间序列相关性。

## 公共工具

### calendar_utils

```python
from cfi.wolverine.misc.calendar_utils import CalendarMgr

cal = CalendarMgr.get()
next_day = cal.shift(date, 1)   # 下一个交易日
prev_day = cal.shift(date, -1)  # 上一个交易日
```

### SignalReader

```python
from cfi.wolverine.misc.sigreader import SignalReader

reader = SignalReader(data_path, instrument="stocks.CHN")
df = reader.read()
```

### sig_file_type

信号文件类型枚举：

```python
class SigFileType(IntEnum):
    csv = 0
    npy = 1
```

## 性能优化

工具使用 Cython 编译为原生代码，关键路径使用 C 原生类型：

```python
cdef void match_A_with_B_cs(
    const int A_nr,
    const cnp.uint64_t[:] A_localtime,
    cnp.float64_t[:, :] ans,
    const int B_nr,
    const cnp.uint64_t[:] B_localtime,
    const cnp.float64_t[:, :] matched_data):
    # 使用 C 原生类型和指针，避免 Python 开销
    ...
```

## 构建

所有工具通过 `tools/CMakeLists.txt` 的 `add_all_subdirectories()` 自动构建，依赖：
- `wolverine` Python 绑定
- `Cython` >= 3.0
- `numpy`