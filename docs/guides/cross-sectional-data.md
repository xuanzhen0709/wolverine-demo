> [📖 Docs](../README.md) › 指南 › 横截面数据处理

# 横截面数据处理

横截面（Cross-Sectional, CS）模式一次推送所有标的的行情数据，适合股票横截面策略。

## 数据源

| 模块 | 事件类型 | 数据内容 |
|------|---------|---------|
| `cs-snapshot` | `CsSnapshotEvent` | 所有标的的 N 档快照 |
| `cs-mbo` | `CsMboEvent` | 所有标的的逐笔成交（Trade/Cancel） |

## CsSnapshotEvent

### 字段访问

使用 `CsSnapshotUtils::get_fld<FldType>()` 按类型获取字段数据：

```cpp
using FldType = CsSnapshotEvent::FldType;

// 层级字段（返回二维数组: [level][ins_idx]）
const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);     // bid price
const auto *bv = CsSnapshotUtils::get_fld<FldType::bv>(ev);     // bid volume
const auto *ap = CsSnapshotUtils::get_fld<FldType::ap>(ev);     // ask price
const auto *av = CsSnapshotUtils::get_fld<FldType::av>(ev);     // ask volume
const auto *bid_cnt = CsSnapshotUtils::get_fld<FldType::bid_cnt>(ev);
const auto *ask_cnt = CsSnapshotUtils::get_fld<FldType::ask_cnt>(ev);

// 标量字段（返回一维数组: [ins_idx]）
const auto *last_price = CsSnapshotUtils::get_fld<FldType::last_price>(ev);
const auto *volume = CsSnapshotUtils::get_fld<FldType::volume>(ev);
const auto *turnover = CsSnapshotUtils::get_fld<FldType::turnover>(ev);
const auto *open_interest = CsSnapshotUtils::get_fld<FldType::open_interest>(ev);
```

### 可用字段

| 字段 | 维度 | 类型 | 说明 |
|------|------|------|------|
| `bp` | `[level][ins]` | `double*` | 买一价（Bid Price） |
| `bv` | `[level][ins]` | `double*` | 买一量（Bid Volume） |
| `ap` | `[level][ins]` | `double*` | 卖一价（Ask Price） |
| `av` | `[level][ins]` | `double*` | 卖一量（Ask Volume） |
| `bid_cnt` | `[level][ins]` | `double*` | 买盘笔数 |
| `ask_cnt` | `[level][ins]` | `double*` | 卖盘笔数 |
| `last_price` | `[ins]` | `double*` | 最新价 |
| `volume` | `[ins]` | `double*` | 累计成交量 |
| `turnover` | `[ins]` | `double*` | 累计成交额 |
| `open_interest` | `[ins]` | `double*` | 持仓量 |
| `exchtime` | `[ins]` | `int64_t*` | 交易所时间 |
| `localtime` | `[ins]` | `int64_t*` | 本地时间 |
| `total_bid_vol` | `[ins]` | `double*` | 总买量 |
| `total_ask_vol` | `[ins]` | `double*` | 总卖量 |
| `total_bid_cnt` | `[ins]` | `double*` | 总买笔数 |
| `total_ask_cnt` | `[ins]` | `double*` | 总卖笔数 |
| `total_bid_qty` | `[ins]` | `double*` | 总买数量 |
| `total_ask_qty` | `[ins]` | `double*` | 总卖数量 |
| `bid_volume` | `[ins]` | `double*` | 买成交量 |
| `ask_volume` | `[ins]` | `double*` | 卖成交量 |

### Python 访问

```python
def on_cs_snapshot(self, ev: CsSnapshotEvent):
    # ev.data 是 dict[int -> np.ndarray]
    last_price = ev.data[CsSnapshotEvent.FldType.LAST_PRICE.value]
    ap = ev.data[CsSnapshotEvent.FldType.AP.value]  # shape: (levels, ins_nr)

    # 必须显式复制以缓存
    data = np.ndarray.copy(last_price)

    # 或使用 get_arr 方法
    last_price = ev.get_arr(CsSnapshotEvent.FldType.LAST_PRICE)
```

### 获取标的列表

在 `on_sod` 中获取当天的标的列表：

```cpp
void Signal::on_sod(const SodEvent *ev)
{
  wllog_info("ins_nr={}\n", ev->ins_nr);
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string ins{ms->instrument};
    ins.erase(std::find(ins.begin(), ins.end(), '\0'), ins.end());

    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    std::string symbol = ins + "." + exch;
  }
}
```

```python
def on_sod(self, ev: SodEvent):
    for i in range(ev.ins_nr):
        ms: MdStatic = ev.ms[i].contents
        ticker = ms.instrument.decode("utf8") + "." + ms.exchange.decode("utf8")
```

### 输出信号

横截面信号需要为每个标的提供一个值：

```cpp
void on_cs_snapshot(const CsSnapshotEvent *ev)
{
  std::vector<double> sigs(ev->ins_nr, 0.0);
  for (int i = 0; i < ev->ins_nr; ++i) {
    sigs[i] = compute_signal_for_ins(i);  // 逐标的信号计算
  }
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime,
                       ev->ins_nr, sigs.data());
}
```

## CsMboEvent

### 字段访问

```cpp
using TradeFldType = CsMboEvent::TradeFldType;

const auto *trade_cnt = CsMboUtils::get_fld<TradeFldType::Cnt>(ev);
const auto *trade_price = CsMboUtils::get_fld<TradeFldType::Price>(ev);
const auto *trade_qty = CsMboUtils::get_fld<TradeFldType::Qty>(ev);
const auto *trade_side = CsMboUtils::get_fld<TradeFldType::Side>(ev);
const auto *trade_bidid = CsMboUtils::get_fld<TradeFldType::BidId>(ev);
const auto *trade_askid = CsMboUtils::get_fld<TradeFldType::AskId>(ev);
```

### 逐标的遍历

```cpp
void on_cs_mbo(const CsMboEvent *ev)
{
  for (int ins = 0; ins < ev->ins_nr; ++ins) {
    const auto cnt = trade_cnt[ins];
    const auto *price = trade_price[ins];
    const auto *qty = trade_qty[ins];
    const auto *side = trade_side[ins];

    for (int ti = 0; ti < cnt; ++ti) {
      // price[ti], qty[ti], side[ti]
    }
  }
}
```

### Python 访问（Cython 加速）

```python
def on_cs_mbo(self, ev: CsMboEvent):
    cdef int ins_nr = ev.ins_nr
    cdef uint32_t* cnts = <uint32_t*><intptr_t>(ev.get_trades_cnt_ptr())
    cdef double* price_arr
    for ii in range(ins_nr):
        ins_cnt = cnts[ii]
        if ins_cnt == 0:
            continue
        price_arr = <double*><intptr_t>(
            ev.get_trades_fld_ptr(CsMboEvent.Trade.FldType_Price.value, ii))
        for ti in range(ins_cnt):
            pass  # price_arr[ti]
```

## 配置示例

```yaml
marketdata:
  - module: cs-snapshot            # 横截面快照
    symbols:
      - stocks.CHN                 # 标的集
    config:
      fields:
        - last_price
        - volume
        - ap
      levels: 5                    # 深度档位
    client:
      - my_signal

  - module: cs-mbo                 # 横截面逐笔成交
    symbols:
      - stocks.CHN
    config:
    client:
      - my_signal
```

## 相关 Demo

| Demo | 数据源 | 语言 |
|------|--------|------|
| `cssnap` | cs-snapshot | C++ |
| `cssnap-py` | cs-snapshot | Python |
| `csmbo` | cs-mbo | C++ |
| `csmbo-py` | cs-mbo | Python (Cython) |
| `csops` | cs-snapshot + FactorNode | C++ |
| `ops_feature` | cs-snapshot + Feature | C++ |
| `ops-mbo` | cs-mbo + Mbo | C++ |
| `feature-factor` | cs-snapshot + Feature + Factor | C++/Python |