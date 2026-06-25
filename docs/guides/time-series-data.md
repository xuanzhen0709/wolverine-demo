> [📖 Docs](../README.md) › 指南 › 时间序列数据处理

# 时间序列数据处理

时间序列（Time-Series, TS）模式按标的分开推送行情数据，适合期货单品种策略。

## 数据源

| 模块 | 事件类型 | 数据内容 |
|------|---------|---------|
| `snapshot` | `SnapshotEvent` | 单标的 N 档快照 |
| `full-snapshot` | `FullSnapshotEvent` | 单标的完整深度快照 |
| `tx-snapshot` | `TxSnapshotEvent` | 快照 + 逐笔成交明细 |
| `crypto-trade` | `CryptoTradeEvent` | 加密货币逐笔成交 |

## SnapshotEvent

### 字段访问

```cpp
void Signal::on_snapshot(const SnapshotEvent *ev)
{
  const auto ss = ev->snapshot;

  // 标量字段
  ss->last_price;
  ss->total_volume;
  ss->total_turnover;
  ss->total_bid_vol;
  ss->total_ask_vol;
  ss->level_nr;          // 档位数
  ss->localtime;
  ss->exchtime;

  // 层级字段（数组，长度为 level_nr）
  ss->bv[i];  // 第 i 档 bid volume
  ss->bp[i];  // 第 i 档 bid price
  ss->av[i];  // 第 i 档 ask volume
  ss->ap[i];  // 第 i 档 ask price
  ss->ac[i];  // 第 i 档 ask count
  ss->bc[i];  // 第 i 档 bid count
}
```

### 完整示例

```cpp
void Signal::on_snapshot(const SnapshotEvent *ev)
{
  const auto ss = ev->snapshot;
  wllog_info("{},{},{},{},{},{}@{},{}@{}\n",
             ss->last_price, ss->total_volume,
             ss->total_turnover,
             ss->bv[0], ss->bp[0], ss->av[0], ss->ap[0]);
}
```

### Python 访问

```python
def on_snapshot(self, ev: SnapshotEvent):
    ss: MdSnapshot = ev.snapshot.contents
    print(f"last_price:{ss.last_price},volume:{ss.total_volume}")

    # 获取数组
    ap = ss.get_arr("ap")  # 所有档位 ask price
    bp = ss.get_arr("bp")  # 所有档位 bid price
    av = ss.get_arr("av")
    bv = ss.get_arr("bv")
```

## FullSnapshotEvent

完整深度快照，结构与 SnapshotEvent 类似，但包含所有档位的完整信息：

```cpp
void Signal::on_full_snapshot(const FullSnapshotEvent *ev)
{
  const auto ss = ev->snapshot;
  wllog_info("last_price:{},total_volume:{},total_turnover:{},levels:{}\n",
             ss->last_price, ss->total_volume, ss->total_turnover,
             ss->level_nr);

  for (size_t i = 0; i < ss->level_nr; ++i) {
    const auto &lvl = ss->levels[i];
    wllog_info("\t{},{}@{},{}%{}\n", i + 1, lvl.bv, lvl.bp, lvl.av, lvl.ap);
  }
}
```

## TxSnapshotEvent

在快照基础上增加了逐笔成交明细（交易/新增/修改/撤单）：

```cpp
void Signal::on_tx_snapshot(const TxSnapshotEvent *ev)
{
  const auto *ss = ev->snapshot;

  // 快照字段
  ss->last_price;
  ss->total_volume;
  ss->total_turnover;
  ss->level_nr;
  ss->tx_nr;  // 逐笔成交条数

  // 档位信息
  for (int i = 0; i < ss->level_nr; ++i) {
    ss->ap[i], ss->av[i], ss->ac[i];
    ss->bp[i], ss->bv[i], ss->bc[i];
  }

  // 逐笔成交明细
  for (int i = 0; i < ss->tx_nr; ++i) {
    const auto &tx = ss->txs[i];
    switch (tx.type) {
    case tx_snapshot_t::tx_type_e::TRADE:
      const auto &trade = tx.trade;
      // trade.id, trade.side, trade.price, trade.qty
      // trade.bid_id, trade.ask_id, trade.flag
      break;
    case tx_snapshot_t::tx_type_e::NEW:
      const auto &add = tx.new_order;
      // add.id, add.side, add.price, add.qty
      break;
    case tx_snapshot_t::tx_type_e::MODIFY:
      const auto &modify = tx.modify_order;
      break;
    case tx_snapshot_t::tx_type_e::CANCEL:
      const auto &cancel = tx.cancel_order;
      break;
    }
  }
}
```

## CryptoTradeEvent

加密货币逐笔成交数据：

```cpp
void Signal::on_crypto_trade(int64_t exchtime, int64_t localtime,
                              const CryptoTradeEvent *ev)
{
  const auto &trade = ev->trade;
  wllog_info("seq:{},price:{},qty:{},side:{}\n",
             trade.seq_no, trade.price, trade.qty, trade.side);
}
```

### Python 访问

```python
def on_crypto_trade(self, exchtime: int, localtime: int, ev: CryptoTradeEvent):
    print(f"price:{ev.trade.price},qty:{ev.trade.qty}")
```

## 多标的订阅

时间序列模式下，每个 `on_sod` 只会收到一个标的的信息（因为行情按标的独立推送）：

```cpp
void Signal::on_sod(const SodEvent *ev)
{
  if (ev->src_type == MdSrcType::Snapshot ||
      ev->src_type == MdSrcType::FullSnapshot) {
    const auto *ms = ev->ms[0];
    const std::string ticker = ms->ticker;
    const std::string exch = ms->exchange;
    wllog_info("ticker:{},exch:{}\n", ticker, exch);
  }
}
```

如需同时订阅多个标的，在 `marketdata.symbols` 中列出：

```yaml
marketdata:
  - module: snapshot
    symbols:
      - ZC.CME
      - ZS.CME
    client:
      - my_signal
```

引擎会为每个标的创建独立的 Signal 实例，或通过 `SodEvent::ms[0]` 区分。

## 配置示例

```yaml
marketdata:
  - module: snapshot              # 时间序列快照
    symbols:
      - ZC.CME
    config:
      dataset: cme               # 数据集
    client:
      - operators

  - module: full-snapshot         # 完整深度快照
    symbols:
      - ZC.CME
    config:
      dataset: cme
    client:
      - my_signal

  - module: tx-snapshot           # 快照 + 逐笔成交
    symbols:
      - ZC.CME
    config:
      dataset: cme
    client:
      - my_signal

  - module: crypto-trade          # 加密货币
    symbols:
      - ETHUSDT.BIANUM
    config:
      dataset: binance-mm-tick.v3
    client:
      - trade
```

## 相关 Demo

| Demo | 数据源 | 语言 |
|------|--------|------|
| `snapshot` | snapshot | C++ |
| `snapshot-py` | snapshot | Python |
| `crossref` | snapshot | C++ |
| `crossref-py` | snapshot | Python |
| `full-snapshot` | full-snapshot | C++/Python |
| `tx_snapshot` | tx-snapshot | C++ |
| `operators` | snapshot | C++ |
| `crypto` | crypto-trade | C++ |