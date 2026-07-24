> [📖 Docs](../README.md) › 研究 › MBO 逐笔数据处理与两所机制

# MBO 逐笔数据处理指南（基于 20250604 实盘 dump 的两所机制分析）

> 本文用一天真实数据（`stats/mbo_dump` dump 的 **20250604**，跨沪深两所四板块 5 只代表股）
> 反推深交所 / 上交所逐笔订单的处理逻辑，给出**实现因子时操作 MBO 数据的规则**。
> 数据源：`stocks.CHN/mbo@3s`（与你 3s 推送一致）；产物：`output/mbo_dump/20250604/mbo-20250604.csv`。

代表股：`600000.SH`(沪主板) `601398.SH`(沪主板) `688981.SH`(科创板) `000001.SZ`(深主板) `300750.SZ`(创业板)。
当日 5 只合计 **Order 442,174 / Cancel 141,431 / Trade 301,262**。

---

## 0. 数据在本框架里的形态（先对齐 API）

逐笔经 `on_cs_mbo(const CsMboEvent *ev)` 推来，**三条独立列式流**，每条都可能为 null：

| 流 | 指针 | 关键字段（`CsMboUtils::get_fld<...>(ev)` → `fld[ins][k]`，条数 `cnt[ins]`） |
|----|------|------|
| Order | `ev->orders` | Cnt/Exchtime/Localtime/Price/Qty/Side/**OrderId**/BidPrice/BidSize/AskPrice/AskSize/TradingPhase |
| Cancel | `ev->cancels` | 同 Order 布局（含 **OrderId**） |
| Trade | `ev->trades` | Cnt/Exchtime/Localtime/Price/Qty/Side/**BidId**/**AskId**/TradingPhase |

字段访问、判空的完整参考实现见 [stats/mbo_dump/main.cpp](../../stats/mbo_dump/main.cpp)。

**本管线已做的归一**：深交所原生把"撤单"塞在成交流里（成交类型标志 + price=0），**但本数据源已把 Cancel 拆成独立流并带上 `order_id`**——沪深两所的 Cancel 都能直接拿到被撤委托号，无需自己从 Trade 流里解析撤单。

---

## 1. 最关键结论：沪深"委托广播"机制不对称（决定生命周期因子能不能做）

用 `Trade.bid_id/ask_id` 去匹配 `Order.order_id`，命中率天差地别：

| 标的 | Trade→Order 买方委托号命中 | 卖方委托号命中 |
|------|--------|--------|
| `000001.SZ`（深） | **100.0%** | **100.0%** |
| `300750.SZ`（深） | **100.0%** | **100.0%** |
| `600000.SH`（沪） | 56.7% | 53.8% |
| `688981.SH`（沪） | 53.6% | 61.4% |

进一步拆解沪市（`600000.SH` 连续竞价段）：

```
连续竞价成交 54,082 笔：
  被动方(maker/挂在簿上的) 委托号可链 = 100.0%
  主动方(taker/发起成交的) 委托号可链 =  10.3%
开盘/收盘集合竞价成交：委托号可链 ≈ 100%
```

**机制解释（实证反推）：**

- **深交所**：Order 流**广播全部委托**——无论最终挂住还是即时成交。所以 Trade 的 `bid_id`/`ask_id` 双边 100% 能回链到 Order，**完整生命周期（下单→部分成交→撤单）可重建**。
- **上交所**：Order 流**只广播"挂进簿子"的委托**（限价挂单）。**吃单/可成交委托在到达瞬间即成交，从不作为 Order 消息出现**——它们只在 Trade 流里以一个"凭空出现"的 `id` 露面（该 id 常大于当前已见的最大 order_id）。
  - 沪市有 45% 的连续竞价 trade-id 找不到对应 Order；其中相当一部分 id **高于已见最大 order_id**（`600000.SH`：above-max bid_id 16,530 个、ask_id 27,946 个）。
  - 但**被动方（挂单方）100% 可链**：因为挂单必然先作为 Order 广播过。

> ⚠️ **对因子实现的硬约束**：
> - **深市**：Order/Cancel/Trade 三流齐全，可做任何生命周期因子（存活加权深度、逃单、幌骗墙、补单吸收……即研究文档视角一全套）。
> - **沪市**：只能重建**"挂单侧"生命周期**（排队时长、被动撤单、被吃后补单）；**主动单**没有 Order 消息，其"下单激进度/拆单/生命周期"类因子**无法从 Order 流构造**——只能从 **Trade 流本身**推（用 `Trade.side` 判主动方向、用 `bid_id`/`ask_id` 在成交序列里聚类同一主动单的连续吃单）。
> - **横截面因子务必按市场分开计算再合并**，否则沪市主动流"缺失"会引入系统性偏差（沪市看起来"撤单/挂单占比"虚高，因为分母只有挂住的单）。

---

## 2. order_id 的性质（别乱假设）

- **交易所内全局递增序列**，非按股票连续：单只股票内 `order_id` 稀疏（`000001.SZ` 跨度 3458 万、只有 9.2% 是 +1 相邻），**mean gap ≈ 200–270**。→ 不要用"id 连续/id 差"推委托数量或顺序密度。
- **日内不复用**（当日重复 order_id = 0）。→ 可安全用作当日字典 key。
- **时间单调**：同一只股票，Order/Cancel/Trade 各流内部都严格按 `exchtime` 升序（0 乱序）；被链接的 Order 一定早于成交（0 acausal）。→ 可流式增量维护，无需回溯排序。

---

## 3. Cancel 的语义（易踩坑）

- **两所 Cancel 都带 `order_id`**（本管线已归一），可直接标记对应委托为撤单终态。
- **`Cancel.qty` = 原始下单量，不是剩余未成交量！**（实证：部分成交后再撤的样本 100% 满足 `Cancel.qty==Order.qty`，0% 等于剩余量）
  → 维护剩余量必须用 `remaining = Order.qty − Σ已成交`，**不要拿 `Cancel.qty` 去减簿**。Cancel 只表示"这张单被移除"。
  → 示例：深市 `id=7875654` 下单卖 1200 → 成交 200 → Cancel 显示 qty=1200（原始量），真正撤掉的是剩余 1000。
- **Cancel 也带撤时一档盘口**（`bid_price/bid_size/ask_price/ask_size` 列，在 Order/Cancel 行有值），可作 running-best 的兜底。

---

## 4. 集合竞价 / 交易时段（无 `trading_phase` 字段，靠 exchtime 判）

本数据源 `trading_phase` 列为空，**用 `exchtime`（当日纳秒，`time::hhmmss_to_exchtime` 同量纲）判时段**：

| 时段 | HHMMSS | 特征（实证） |
|------|--------|------|
| 开盘集合竞价·可撤 | 09:15–09:20 | 有 Order 也有 Cancel |
| 开盘集合竞价·**禁撤** | **09:20–09:25** | **全部 5 只 0 笔 Cancel**（符合不可撤规则） |
| 撮合/静默 | 09:25–09:30 | 09:25 出集合竞价成交（沪深 trade-id 均 ~100% 可链） |
| 连续竞价 | 09:30–11:30 / 13:00–14:57 | 沪市主动单开始"消失" |
| 收盘集合竞价（深/科创/创业） | 14:57–15:00 | trade-id ~100% 可链 |

**因子实现规则：**
- 集合竞价的 Order 和 Cancel 都是真实生命周期事件，可以统计；竞价撮合前的撤单仍按
  `remaining=Order.qty−Σ已成交` 和 `lifetime=cancel_time−rest_arrival` 计算。
- 09:20–09:25 无撤单是**制度性**的，不要把"这段撤单率=0"当成市场主体主动选择。
- 集合竞价 Trade 没有唯一 taker/maker：bid 和 ask 两腿都应作为 resting fill，不能根据
  `Trade.side` 重置其中一侧的 arrival。
- 是否将竞价 lifecycle 与连续竞价混成一个分布属于研究口径选择。若保留，应保存真实
  arrival 并对交易阶段做控制；若只研究连续竞价，应显式门控，而不是因为实现限制丢弃
  有效竞价撤单。
- 完整转移规则见 [MBO 委托生命周期状态机](lifetime-state-machine.md)。

---

## 5. 成交方向与主动方判定

- `Trade.side` 给出主动方（本 dump 里 T 行 side 为 B/S，来自 `Side::TRADED_BID`/`TRADED_ASK` 的语义）。
- **深市**：主动单也有 Order 记录，可交叉验证；**沪市**：主动方只能靠 `Trade.side`。
- 想聚合"同一主动单扫了几档/几笔"（拆单足迹）：
  - **深市**：按主动方 `order_id`（成交里对应那一侧的 id）分组即可。
  - **沪市**：主动方 id 虽"凭空出现"但在一个成交簇里**是稳定的同一个值**（实证：above-max 的 taker id 会连续出现在多笔成交中，`600000.SH` 多次复用的 above-max id 数千个）——可按该 id 在**相邻成交序列**里聚类同一次扫单。

---

## 6. 量纲与横截面可比

| 板块 | 代表 | min 单笔 qty | mean 单笔 qty | 备注 |
|------|------|------|------|------|
| 沪主板 | 600000 | 10 | 1970 | 100 股整手 |
| 科创板 | 688981 | 1 | 909 | 限价 200 股起、可 1 股递增 |
| 深主板 | 000001 | 1 | 3234 | |
| 创业板 | 300750 | 2 | 330 | 高价股，单笔股数小 |

→ 跨股比较**用成交额 `price*qty`（turnover）或按该股当日分位数自适应阈值**，别直接比原始股数。研究文档里"大单阈值"一律用 per-stock q75/q90 正是这个原因。

---

## 7. 实现 MBO 因子的操作清单（Checklist）

在 `on_cs_mbo` 里，**每标的维护一个当日状态**（`std::vector<PerInsState>`，`on_sod` 按 `ins_nr` 重置）：

1. **判市场**：由 `ev->ms[i]` 的 ticker 前缀分沪/深（6/688→沪，0/3→深），沪市禁用"主动单生命周期"分支。
2. **LiveOrders 字典**（深市全部委托 + 沪市可观测挂单侧）：
   `order_id → {side, rest_qty0, passive_filled, rest_arrival, status, cohort}`。
   - Order → 先按交易阶段和是否存在同时间 taker trade 判断普通挂单、纯主动或 residual。
   - 连续竞价 Trade → maker 腿累计被动成交；沪市无对应 Order 的 taker 跳过生命周期
     状态，但仍处理 maker。
   - 集合竞价 Trade → bid/ask 两腿都累计被动成交，不使用 taker/maker 解释。
   - Cancel → 记 `lifespan=cancel_exchtime−rest_arrival`；**剩余量 =
     rest_qty0−passive_filled，不能使用 Cancel.qty**。
3. **时间基准**：一切 age/窗口/门控用 `exchtime`；`localtime` 只用于跨日/日志。**严格 look-ahead**：t 时点出值时只用 `exchtime ≤ t` 的事件。
4. **交易阶段**：连续竞价和集合竞价使用不同 Trade 转移；09:20–09:25 的制度性禁撤应
   作为 phase/control，而不是普通的“低撤单率”信号。
5. **横截面**：按市场分组算，再各自 winsorize+zscore/rank；阈值用 per-stock 在线分位数（不能用当日全量→look-ahead）。
6. **null 防御**：`ev->orders/cancels/trades` 及每个字段指针都可能为 null，逐一判空（见 mbo_dump 的写法）。
7. **同一 3s 窗口内**：Order 与其 Cancel 可能落在同一帧（实证 17.8% 撤单在其下单后
   3s 内）。应按 `exchtime` 归并三流；同时间戳优先使用原始序号，没有序号时采用
   `Order → Trade → Cancel` 并检查状态不变量。完整算法见
   [MBO 委托生命周期状态机](lifetime-state-machine.md)。

---

## 8. 复现本分析

```bash
enable_wlsim_env
# dump（已配好 5 只代表股；改 symbols 可扩）
wl-sim stats/mbo_dump/dump_20250604.yml
# 产物：output/mbo_dump/20250604/mbo-20250604.csv（17 列统一格式）
```

分析脚本见本文各节 awk 片段；核心指标：Trade→Order 命中率（分买卖方、分市场、分时段）、Cancel.qty 语义、order_id 稀疏性/复用、09:20–09:25 撤单数。
