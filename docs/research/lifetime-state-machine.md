> [📖 Docs](../README.md) › 研究 › MBO 委托生命周期状态机

# MBO 委托生命周期状态机

本文定义 `factors/mbo/lifetime` 应遵循的订单状态机和统计口径。它是目标设计规范，
不表示当前 `main.cpp` 已经完整实现了所有规则。

相关背景：

- [MBO 逐笔数据处理指南](mbo-data-handling.md)
- [lifetime 当前实现](../../factors/mbo/lifetime/main.cpp)
- [lifetime 配置](../../factors/mbo/lifetime/wlsim.yml)

## 1. 设计结论

生命周期是否可统计，取决于订单是否真实进入过订单簿，而不是事件发生在连续竞价还是
集合竞价：

1. 集合竞价的 `Order` 和 `Cancel` 都是有效的生命周期事件。
2. 连续竞价的 `Trade` 才能按 `Trade.Side` 拆成 taker 和 maker。
3. 集合竞价没有唯一的 taker/maker；每笔成交的 bid 和 ask 都是 resting fill。
4. 纯主动且完全成交的订单没有 resting lifetime，不进入生命周期统计。
5. 主动成交后留下的 residual 已真实挂入订单簿，应从 residual 的挂簿时刻开始追踪。
6. 部分被动成交不会开始新生命周期；后续成交和撤单都沿用当前 resting cohort 的
   `rest_arrival`。
7. cohort 的 `arrival` 是排序键，创建后不可原地修改。

用一条规则概括：

```text
Order 是否真实进入订单簿，决定是否创建 resting cohort；
交易阶段决定 Trade 是单边 maker fill，还是双边 resting fill；
Cancel 对所有真实 resting order 使用相同的 remaining/lifetime 逻辑。
```

## 2. 状态和数据结构

每个 `(instrument, order_id)` 维护一个日内状态：

```cpp
enum class OrderStatus {
  PENDING,    // 连续竞价 Order 已出现，等待同时间主动成交归类
  RESTING,    // 当前有真实挂簿剩余量
  ACTIVE_DONE,// 纯主动全部成交，无 resting lifecycle
  FILLED,     // resting 剩余量全部成交
  CANCELLED   // resting 剩余量被撤销
};

struct OrderState {
  Side side;
  int64_t original_arrival; // 原始 Order 时间，仅用于诊断
  int64_t rest_arrival;     // 当前 resting cohort 起点，因子使用
  int64_t rest_qty0;        // 本次进入订单簿的初始量
  int64_t passive_filled;   // 进入订单簿后的累计被动成交量
  OrderStatus status;
  CohortHandle cohort;      // 稳定回指，不依赖可变排序位置
};
```

当前剩余量统一定义为：

\[
R_i=Q_i^{rest}-F_i^{passive}.
\]

只保留 `PENDING` 和 `RESTING` 于 live map 即可；三个终态可以在记录统计结果后从 live map
删除，但应保留质量计数器。

## 3. 交易阶段

状态机只需要三类阶段：

| 阶段 | 语义 | Trade 处理 |
| --- | --- | --- |
| `CALL` | 开盘/收盘集合竞价 | bid、ask 两腿都按 resting fill |
| `CONTINUOUS` | 连续竞价 | 按 `Trade.Side` 区分 taker/maker |
| `INACTIVE` | 静默、午休、闭市等 | 不应有正常撮合；出现则计异常 |

优先使用数据源提供的交易阶段字段。若该字段为空，再根据 `exchange + date + exchtime`
配置阶段边界；不要用一套永久不变的硬编码覆盖全部历史日期和板块。

当前样本数据可观察到的典型阶段是：

```text
09:15–09:25  开盘集合竞价
09:25         集合竞价撮合
09:30–11:30  上午连续竞价
13:00–14:57  下午连续竞价
14:57–15:00  收盘集合竞价（以交易所/板块/日期规则为准）
```

09:20–09:25 的制度性禁撤意味着这段没有 Cancel 不是市场信号，但这不影响此前有效
Order 的生命周期继续增长。

## 4. 总体状态图

```text
                                      residual = 0
                                 ┌──────────────────> ACTIVE_DONE
                                 │
ABSENT ── Order(CONTINUOUS) ──> PENDING
                                 │
                                 ├── residual > 0 ─> RESTING
                                 │
                                 └── 无主动成交 ───> RESTING

ABSENT ── Order(CALL) ─────────────────────────────> RESTING

RESTING ── 部分被动成交 ───────────────────────────> RESTING
RESTING ── 完全被动成交 ───────────────────────────> FILLED
RESTING ── Cancel ─────────────────────────────────> CANCELLED
```

完整转移表：

| 当前状态 | 事件/条件 | 操作 | 下一状态 |
| --- | --- | --- | --- |
| `ABSENT` | `Order` in `CALL` | 用原始时间和原始量创建 cohort | `RESTING` |
| `ABSENT` | `Order` in `CONTINUOUS` | 暂存 Order，检查同时间 taker trades | `PENDING` |
| `PENDING` | 无对应 taker trade | 创建普通被动 cohort | `RESTING` |
| `PENDING` | taker trades 后 residual > 0 | 创建 residual cohort | `RESTING` |
| `PENDING` | taker trades 后 residual = 0 | 不创建 cohort，不记 lifetime | `ACTIVE_DONE` |
| `RESTING` | 被动部分成交 | 记录 fill 片段，累计 `passive_filled` | `RESTING` |
| `RESTING` | 被动完全成交 | 记录 fill 片段并移出 live | `FILLED` |
| `RESTING` | `Cancel` | 记录剩余量及撤单寿命并移出 live | `CANCELLED` |
| 任意终态 | 再次收到有效事件 | 不复活订单，增加异常计数 | 不变 |

## 5. 集合竞价

### 5.1 Order

集合竞价 Order 直接形成 resting cohort，沪深一致：

\[
t_i^{rest}=t_i^{order},\qquad
Q_i^{rest}=Q_i^{order},\qquad
F_i^{passive}=0.
\]

```text
ABSENT + Auction Order
  -> RESTING(
       rest_arrival = Order.exchtime,
       rest_qty0 = Order.qty,
       passive_filled = 0
     )
```

它是真实挂单，因此必须进入 `fleet_order` 分母。

### 5.2 Cancel

集合竞价的撤单计算与连续竞价完全相同：

\[
L_i^{cancel}=t_i^{cancel}-t_i^{rest},
\]

\[
V_i^{cancel}=Q_i^{rest}-F_i^{passive}.
\]

`Cancel.qty` 只用于数据校验，不能作为实际撤走量；数据源里的 `Cancel.qty` 可能仍是原始
报单量。

### 5.3 Trade

集合竞价没有唯一主动方。对每条

```text
Trade(bid_id, ask_id, qty, trade_time)
```

执行：

```cpp
apply_passive_fill(bid_id, qty, trade_time);
apply_passive_fill(ask_id, qty, trade_time);
```

`Trade.Side` 在这里不用于选择 taker。每一腿独立执行：

\[
v_i^{fill}=\min(q^{trade},R_i),
\]

\[
L_i^{fill}=t^{trade}-t_i^{rest},
\]

\[
F_i^{passive}\leftarrow F_i^{passive}+v_i^{fill}.
\]

约束：

- bid 和 ask 两边都生成 fill lifetime；
- 不重置 `rest_arrival`；
- 不创建新的 residual cohort；
- 完全成交才进入 `FILLED`；
- 部分成交后的剩余量继续沿用原始生命周期；
- 未成交剩余量进入后续连续竞价时仍不重置 arrival。

## 6. 连续竞价

### 6.1 普通被动订单

如果新 Order 在同一撮合时刻没有对应 taker trade：

\[
t_i^{rest}=t_i^{order},\qquad
Q_i^{rest}=Q_i^{order}.
\]

```text
ABSENT -> PENDING -> RESTING
```

### 6.2 主动成交及 residual

一张主动订单可能扫过多个 maker。先按 taker `order_id` 汇总：

\[
Q_i^{active}=\sum_j q_{ij}^{taker},
\]

\[
t_i^{last\_active}=\max_j t_{ij}^{trade}.
\]

连续竞价中才根据 `Trade.Side` 识别两腿：

```text
TRADED_BID/BID：bid_id=taker，ask_id=maker
TRADED_ASK/ASK：ask_id=taker，bid_id=maker
```

#### 深市

深市 `Order.qty` 是原始委托量：

\[
Q_i^{residual}
=
\max\left(Q_i^{order}-Q_i^{active},0\right).
\]

若 residual 为 0，订单进入 `ACTIVE_DONE`，不创建 cohort。若 residual 大于 0：

\[
t_i^{rest}=t_i^{last\_active},\qquad
Q_i^{rest}=Q_i^{residual}.
\]

#### 沪市

沪市连续竞价中，纯主动且完全成交的委托通常没有 Order 消息；主动后仍有挂簿剩余量时，
Order 消息里的数量已经是 residual：

\[
Q_i^{residual}=Q_i^{order}.
\]

因此不能再次扣除 Trade.qty。若找不到对应 Order，只忽略 taker 的生命周期状态，但仍要
正常处理该 Trade 的 maker 腿。找到 residual Order 时：

\[
t_i^{rest}=t_i^{last\_active},\qquad
Q_i^{rest}=Q_i^{order}.
\]

沪深差异仅存在于连续竞价 taker residual 的恢复；普通 resting、maker fill、Cancel 和
集合竞价逻辑应保持一致。

## 7. 被动成交与部分成交后撤单

`apply_passive_fill(order_id, q, t)` 只接受 `RESTING` 订单：

```text
remaining_before = rest_qty0 - passive_filled
fill_vol = min(q, remaining_before)
life = t - rest_arrival
append fill LifeRec(life, fill_vol, side)
passive_filled += fill_vol
if passive_filled == rest_qty0:
    transition to FILLED
```

一张订单的完整路径可以是：

```text
RESTING(Q0, F=0)
  -- fill q1 -->
RESTING(Q0, F=q1)
  -- fill q2 -->
RESTING(Q0, F=q1+q2)
  -- cancel -->
CANCELLED
```

最终撤单量：

\[
V^{cancel}=Q_0-q_1-q_2.
\]

撤单寿命：

\[
L^{cancel}=t^{cancel}-t^{rest}.
\]

当前 fill 分布的样本单位是“被动成交片段”：每个部分成交打印产生一条 `LifeRec`。这与
订单级首次成交/完全成交寿命不同，因子说明中必须明确。

## 8. Cohort 与 fleet 记账

每次真正形成 `RESTING` 时创建且只创建一条 cohort：

| 订单类型 | 创建 cohort | `arrival` | `vol` |
| --- | --- | --- | --- |
| 普通被动订单 | 是 | Order 时间 | `Order.qty` |
| 深市纯主动全成交 | 否 | — | — |
| 深市主动后 residual | 是 | 最后主动成交时间 | `Order.qty-active_qty` |
| 沪市纯主动全成交 | 无 Order，因此不创建 | — | — |
| 沪市主动后 residual | 是 | 最后主动成交时间 | `Order.qty` |
| 集合竞价订单 | 是 | 原始 Order 时间 | `Order.qty` |

cohort 的 `arrival` 创建后不可修改。若业务上确实产生新 cohort，应终止或 tombstone
旧记录，并按新 arrival 插入新记录；不能原地修改排序键。

事件窗口为：

\[
[t-W,t].
\]

冷却后的 Order cohort 窗口为：

\[
(t-W-\tau,t-\tau].
\]

必须满足：

\[
\tau\ge \max(\text{fleeting thresholds}).
\]

以方向 \(s\) 和阈值 \(h\) 为例：

\[
r_{s,h}^{cancel}
=
\frac{
  \sum_{\substack{\text{cancel event}\\side=s,\ life<h}}V_i^{cancel}
}{
  \sum_{\substack{\text{cancel event}\\side=s}}V_i^{cancel}
},
\]

\[
r_{s,h}^{order}
=
\frac{
  \sum_{\substack{\text{placed cohort}\\side=s,\ cancel\ life<h}}V_i^{cancel}
}{
  \sum_{\substack{\text{placed cohort}\\side=s}}Q_i^{rest}
}.
\]

二者分母不同：

- `fleet_cancel`：在已经撤掉的量中，有多少属于快速撤单；
- `fleet_order`：在进入订单簿的量中，有多少最终被快速撤走。

## 9. 每 3 秒 callback 的处理

`CsMboEvent` 将 Order、Trade、Cancel 放在三条独立数组中。逻辑上应按 instrument 和
`Exchtime` 归并成时间桶，时间桶升序处理：

```text
for each instrument:
  for each timestamp t ascending:
    1. 收集 t 时刻的 Orders
    2. 判断 t 的 CALL / CONTINUOUS 阶段
    3. 收集并解释 t 时刻的 Trades
    4. 解析新 Order 是普通 resting、纯主动还是 residual
    5. 应用 passive fills
    6. 应用 Cancels
```

连续竞价时间桶：

```text
1. 用 Trade.Side 汇总每个 taker 的 active_qty 和 last_active_time
2. 解析新 Orders
   - 无 taker trade     -> 普通 RESTING
   - 深市 taker Order   -> Order.qty - active_qty
   - 沪市 taker Order   -> Order.qty 直接作为 residual
3. 每条 Trade 只对 maker 腿执行 apply_passive_fill
4. 处理 Cancel
```

集合竞价时间桶：

```text
1. 所有 Order 直接创建 RESTING
2. 每条 Trade：
   apply_passive_fill(bid_id)
   apply_passive_fill(ask_id)
3. 处理 Cancel
```

若数据包含交易所原始序号，应优先按原始序号处理。没有序号时，同一时间戳采用
`Order -> Trade -> Cancel`，并用状态不变量检查潜在歧义。若同时间事件可能跨 callback，
应通过 callback watermark 延迟完成 `PENDING` 分类。

## 10. 状态不变量和质量计数器

每个时间桶或 callback 后检查：

\[
0\le F_i^{passive}\le Q_i^{rest},
\]

\[
R_i=Q_i^{rest}-F_i^{passive}\ge 0,
\]

\[
t_i^{rest}\le t_i^{fill/cancel}.
\]

同时保证：

- 每个 OrderId 最多对应一个 live 状态；
- terminal 订单不能重新进入 `RESTING`；
- 连续竞价 taker 应对应同撮合时刻的新 Order，否则记录异常且不得直接重置旧状态；
- 集合竞价 Trade 的 bid/ask 两腿各扣一次成交量；
- 完全成交订单不应再收到有效 Cancel；
- `PlacedRec.arrival` 不可变，cohort 容器始终保持有序；
- EOD 清空 live 状态，但不把清空自动统计成 Cancel。

至少维护以下计数器：

```text
duplicate_order_id
unknown_order_side
unknown_trade_side_continuous
maker_miss
cancel_miss
terminal_event
overfill
negative_lifetime
continuous_taker_without_same_time_order
call_trade_bid_miss
call_trade_ask_miss
```

集合竞价中即使 `Trade.Side` 未知，只要 bid/ask ID 有效，仍可分别处理两条 resting 腿；
连续竞价方向未知时则不能安全识别 maker，应跳过并计数。

## 11. 验收用例

| 场景 | 预期状态 | fill 记录 | cancel 记录 | cohort |
| --- | --- | ---: | ---: | ---: |
| 连续竞价普通挂单完全成交 | `FILLED` | 每个成交片段一条 | 0 | 1 |
| 连续竞价部分成交后撤单 | `CANCELLED` | 每个成交片段一条 | 1，量为 remaining | 1 |
| 深市纯主动全成交 | `ACTIVE_DONE` | taker 侧 0 | 0 | 0 |
| 深市主动后有 residual | `RESTING` | 后续 maker 片段才记录 | 后续可记录 | 1 个 residual |
| 沪市纯主动全成交且无 Order | 无 live 状态 | taker 侧 0 | 0 | 0 |
| 沪市主动后推 residual Order | `RESTING` | 后续 maker 片段才记录 | 后续可记录 | 1 个 residual |
| 集合竞价双边完全成交 | 双方 `FILLED` | bid/ask 各一条 | 0 | 双方各 1 |
| 集合竞价部分成交后进入连续竞价 | 双方剩余量继续 `RESTING` | bid/ask 各记录 | 后续可记录 | 不新建 |
| 集合竞价撮合前撤单 | `CANCELLED` | 0 | 1 | 1 |

## 12. 当前实现与目标设计的差异

截至本文编写时，`factors/mbo/lifetime/main.cpp` 的连续竞价 residual、部分成交和 remaining
撤单量逻辑基本符合本文，但仍需完成两项关键修正：

1. 当前所有 Trade 都按 `Trade.Side` 拆 taker/maker；集合竞价应改成 bid/ask 双边
   `apply_passive_fill`。
2. 当前 residual 转换会原地修改 `PlacedRec.arrival`，可能破坏 `plwin_` 的有序性；
   目标实现必须让 cohort arrival 保持不可变。

实现完成后，应以第 11 节用例和第 10 节不变量作为最小回归测试集。
