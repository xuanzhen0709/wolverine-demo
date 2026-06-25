> [📖 Docs](../README.md) › 概览 › 框架架构

# 框架架构（wolverine + cfi_operators）

本文从 `build/` 解析出的头文件出发，总结 wolverine 仿真平台与 cfi_operators 算子库的总体特征。
头文件实际位于 wlsim 运行时安装目录（编译命令中 `-isystem .../include/wolverine`、
`.../include/cfi_ops`）。逐头文件签名见 [框架 API 参考](../reference/framework-api.md)。

## 两个层次

```
┌──────────────────────────────────────────────────────────┐
│  你的代码（demos/）                                        │
│    Signal / Factor / Feature（C++ .so 或 Python .pyx）    │
└───────────────▲──────────────────────────▲──────────────┘
                │ Ops（回调表，C ABI）        │ Apis（引擎能力注入）
┌───────────────┴──────────────────────────┴──────────────┐
│  wolverine 引擎（cfi::wolverine）                         │
│    事件调度 / 行情加载 / 配置 / 日志 / 工具 / traits       │
└───────────────▲──────────────────────────────────────────┘
                │ 公式求值（ descent-parser）
┌───────────────┴──────────────────────────────────────────┐
│  cfi_operators（cfi::）                                   │
│    Operators / FactorNode / Feature / Mbo / checkpoint    │
└──────────────────────────────────────────────────────────┘
```

- **wolverine**：低延迟行情仿真与信号引擎。负责回放行情、按事件回调用户代码、收集输出信号。
- **cfi_operators**：基于公式字符串的算子库，提供时序 / 横截面 / 逐笔聚合计算，供用户在
  回调中调用。两者解耦——你可以只用 wolverine 写裸信号（如 `snapshot`、`cssnap`），也可以
  叠加 cfi_operators 做公式化计算（如 `operators`、`csops`、`ops_feature`、`ops-mbo`）。

## 插件式 C ABI 模型

wolverine 的所有可扩展组件都以 **共享库 + C 入口函数** 形式加载：

1. 用户把逻辑编译成 `.so`（C++）或 `.pyx`（Python，经 Cython 编译）。
2. 库导出一个 `extern "C"` 的 `on_create(void** ptr, <X>Ops* ops, ...)`。
3. 引擎 `dlopen` 该库，调用 `on_create`：用户 `new` 出自己的对象赋给 `*ptr`，并把一组
   **函数指针**填进 `*ops`（回调表）。
4. 引擎后续通过 `ops->on_xxx(hdl, ...)` 回调用户逻辑；`hdl` 就是第 3 步的对象指针。

`common.hpp` 的 `C_DECLARATION_BEGIN/END`（即 `extern "C" {}`）保证 `on_create` 符号不被
C++ name mangling 破坏。`WOLVERINE_EXPORT`/`WOLVERINE_NO_EXPORT` 控制符号可见性。

四种插件角色：

| 角色 | 头文件 | Ops 表 | on_create 签名 | 实例粒度 |
|------|--------|--------|----------------|----------|
| Signal | `signal.hpp` | `SignalOps` | `(void**, SignalOps*)` | 每配置一个 |
| Feature | `feature.hpp` | `FeatureOps` | `(void**, FeatureOps*, int insidx)` | **每标的**一个 |
| MdLoader | `md_loader.hpp` | `MdLoaderOps` | `(void**, MdLoaderOps*)` | 行情源 |
| SignalSink | `signal_sink.hpp` | `signal_sink_ops_t` | `(void**, signal_sink_ops_t*)` | 输出消费 |

> Signal 与 Factor 共用 `SignalOps`/`SignalApis`——Factor 本质上也是一个 Signal，只是它把
> 结果通过 `update_signal` 推给上层 Signal 消费（见 `feature-factor` demo）。

## 事件与回调

引擎把行情抽象成 9 类 `EventType`，并对应 `SignalOps` 上的回调：

| EventType | 回调 | 载荷 | 数据范式 |
|-----------|------|------|----------|
| Sod | `on_sod` | `SodEvent{date, ins_nr, src_type, ms[]}` | — |
| Eod | `on_eod` | `EodEvent{date}` | — |
| Snapshot | `on_snapshot` | `MdSnapshot`（10 档定长） | TS |
| Bar | `on_bar` | `MdBar`（OHLC+twap） | TS |
| FullSnapshot | `on_full_snapshot` | `MdFullSnapshot`（柔性数组 levels[]） | TS |
| TxSnapshot | `on_tx_snapshot` | `tx_snapshot_t`（快照+逐笔，packed 128B） | TS |
| CsSnapshot | `on_cs_snapshot` | `CsSnapshotEvent`（列式 SoA） | CS |
| CsMbo | `on_cs_mbo` | `CsMboEvent`（Order/Cancel/Trade 列式） | CS |
| CryptoTrade | `on_crypto_trade` | `crypto_trade_t`（25B） | TS |

`SodEvent::src_type`（`MdSrcType`）告知本日行情来源类型，便于 TS 模式下区分标的。

## 两种数据范式

| | 横截面 Cross-Sectional (CS) | 时间序列 Time-Series (TS) |
|---|---|---|
| 推送方式 | 一次推送**全部标的** | 按**单个标的**逐条推送 |
| 内存布局 | **列式 / struct-of-arrays** | **array-of-structs** |
| 典型事件 | `CsSnapshotEvent`、`CsMboEvent` | `SnapshotEvent`、`FullSnapshotEvent`、`TxSnapshotEvent`、`CryptoTradeEvent` |
| 字段访问 | `CsSnapshotUtils::get_fld<Fld>(ev)` / `CsMboUtils::get_fld<Fld>(ev)`（类型擦除 + 维度推导） | 直接 `ev->snapshot->bp[i]` 等成员 |
| 适合 | 股票横截面选股策略 | 期货单品种 / 配对策略 |
| `ins_nr` | `ev->ins_nr` 为当日标的总数 | 通常为 1（多标的时由引擎分实例或 `ms[0]` 区分） |

CS 字段用 **traits** 体系做类型安全访问：`CsSnapshotTraits<Fld>` 给出 `type`、`dim`（1=标量
`[ins]`，2=`[level][ins]`）、`name`；`get_fld` 对无符号字段返回有符号视图（便于做差）。
逐笔 MBO 字段同理（`CsMboFldTraits`），每个标的的消息数由 `cnt` 给出，访问形式为
`fld[ins][msg_idx]`。

## Signal 生命周期

```
on_create → initialize(cfg) → set_apis(apis) → load_state? →
   ┌─ on_sod → on_<marketdata>() × N → on_eod ─┐  ← 每个交易日
   └────────────────────────────────────────────┘
→ save_state? → on_destroy
```

- `initialize`：读 yaml 的 `signal.config` 段（`Config` 对象）。
- `set_apis`：引擎注入 `SignalApis`，后续可用 `update_signal` 输出、`get_factors` 取因子、
  `get_feature_buf` 取特征缓冲等。
- `load_state`/`save_state`：可选的跨日状态持久化（checkpoint）。
- `on_sod`/`on_eod`：日初重置 / 日末汇总；cfi_operators 的 `on_day_begin/on_day_end` 也在此调用。

详见 [Signal 生命周期](signal-lifecycle.md) 与 [编写一个 Signal](../guides/writing-a-signal.md)。

## 配置驱动

所有 demo 通过 `wlsim.yml` 驱动：日期范围、行情源（`marketdata`）、信号模块（`signal`）、
可选的 `features`/`factors`/`checkpoint`/`worker`。`Config` 是 yaml-cpp 的薄封装，带路径
追踪，缺节点时 `assert_valid()` 直接 `wllog_fatal`。配置项全集见
[YAML 配置参考](../reference/yaml-config.md)。

## cfi_operators 算子库

公式字符串 + `@name` 占位符，`update()` 时按位置喂入实参，内部由 descent parser 求值。

| 类 | 范式 | 关键 API | on_day_begin | checkpoint |
|----|------|----------|--------------|-----------|
| `Operators` | TS | `update(args...)` 变参 → `item_t` | `(date)` | ✔ |
| `FactorNode` | CS | `update(ptr, n)` / `update(a, b, n)` | `(date, cs_len)` | ✔ |
| `Feature` | CS | `get_datanames()` + `update(vector<variant>, n)` 自动解析依赖 | `(date, cs_len)` | ✔ |
| `Mbo` | CS 逐笔 | `update(x, y, n, index)` / `update(vector, n, index)` | `(date, cs_len)` | ✗（未启用） |

- `item_t = double`；`feature_variant` 是 `int64_t*/int32_t*/double*/uint64_t*` 的类型擦除。
- 公式函数示例：`ts_sum`、`ts_mean`、`ts_std`、`ts_inner_product`、`sum_agg`（Mbo 聚合）。
- `cfi::checkpoint` 提供对 `vector<double>` / `vector<vector<double>>` / `deque<double>` 的
  通用 save/load；`cfi::linear_algebra` 提供 `dgemm/daddmm/dsubmm`。
- `common_define.h`：`NAN_FILTER` 宏（非有限值返回 NaN）、`CHECKPOINT(T)` 宏（基于
  nlohmann::json 的算子状态序列化，需 `OpBase` 基类）。

详见 [使用 Operators](../guides/operators.md)、[使用 Feature](../guides/feature.md)。

## 工具与基础设施

- **日志** `logging.hpp`：`wllog_info/warn/error/debug/fatal`（fmt + 本地时区）；`fatal` 会
  `fflush` 后 `exit(-1)`；`debug` 在 `NDEBUG` 下编译为空。
- **时间** `utils/time.hpp`：`str_to_exchtime`、`exchtime_to_str`、`epoch_to_str`、
  `parse_session`、`generate_bars_from_sessions`；`timestamp_cmp_func_t` 可切换按 exchtime
  或 localtime 排序。常量 `NIGHT_SESSION_CUTOFF_HOUR=18`（夜盘分割点）。
- **IO** `utils/mmap.hpp`（只读 mmap）、`utils/zstd.hpp`（zstd 流式解压）、
  `utils/lib.hpp`（dlopen/dlsym 封装 `shared_lib_t`）。
- **系统** `utils/utils.hpp`：目录创建、hostname/username、CPU 绑定、`duration_str2ns`。
- **参考数据** `refdata.hpp`（`wlrefdata_find`）、`calendar.hpp`（`wlcalendar_shift` 交易日偏移）、
  `env.hpp`（`wlsim_getenv`）。
- **vendored**：fmt、yaml-cpp、rapidcsv（随头文件分发；编译时 `FMT_HEADER_ONLY=1`）。

## 已知上游问题（记录，非本仓库可修）

以下位于 wlsim 运行时安装目录的头文件中，不在本 demo 仓库内，仅作记录：

- `cfi_ops/dp_export.h`：`#d efine DP_EXPORT __declspec(dllexport)` 中 `#d efine` 是断词，
  会破坏 MSVC 构建路径（GCC 下该分支不触发，故不影响当前构建）。
- `cfi_ops/factornode.h`：`[[deprecated("Use Fearture instead.")]]` 有拼写错误（Fearture）。
  这也是 `csops`、`ops_feature` 等用 `FactorNode::update` 时会出现 deprecated 警告的来源。
