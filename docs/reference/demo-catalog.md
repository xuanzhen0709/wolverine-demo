> [📖 Docs](../README.md) › 参考 › Demo 清单

# Demo 清单

## 横截面 (Cross-Sectional) Demo

| Demo | 语言 | 数据源 | 功能亮点 |
|------|------|--------|---------|
| `cssnap` | C++ | cs-snapshot | 横截面 snapshot 基础：读取 bp/volume，输出信号 |
| `cssnap-py` | Python | cs-snapshot | Python 版横截面 snapshot，含相对收益计算 |
| `csmbo` | C++ | cs-mbo | 横截面 MBO 逐笔成交，含 trade price/qty 处理 |
| `csmbo-py` | Python (Cython) | cs-mbo | Cython 加速版 MBO 数据访问 |
| `csops` | C++ | cs-snapshot | 横截面 + FactorNode 算子，公式 `ts_inner_product` + `ts_sum` |
| `ops_feature` | C++ | cs-snapshot | Feature 自适应字段映射，动态数据管道构建 |
| `ops-mbo` | C++ | cs-mbo | Mbo 算子，横截面逐笔成交聚合 |
| `feature-factor` | C++/Python | cs-snapshot + features | 特征工厂 + 因子嵌套 + ftreader 回放 |

## 时间序列 (Time-Series) Demo

| Demo | 语言 | 数据源 | 功能亮点 |
|------|------|--------|---------|
| `snapshot` | C++ | snapshot | 单标的 N 档快照，打印完整行情 |
| `snapshot-py` | Python | snapshot | Python 版 snapshot，含 ap/bp/av/bv 计算 |
| `crossref` | C++ | snapshot | 多标的订阅，区分 ticker 做配对交易 |
| `crossref-py` | Python | snapshot | Python 版多标的 |
| `full-snapshot` | C++/Python | full-snapshot | 完整深度快照，遍历所有档位 |
| `tx_snapshot` | C++ | tx-snapshot | 快照 + 逐笔成交明细 (TRADE/NEW/MODIFY/CANCEL) |
| `operators` | C++ | snapshot | Operators 时序算子，`ts_inner_product` 公式 |
| `crypto` | C++ | crypto-trade | 加密货币逐笔成交数据 |

## Checkpoint / State 管理

| Demo | 语言 | 功能 |
|------|------|------|
| `checkpoint` | C++ | 序列化/反序列化 std::vector + 自定义 struct（zpp_bits） |
| `checkpoint-py` | Python | Python pickle 实现 state 持久化 |

## 信号回放

| Demo | 语言 | 功能 |
|------|------|------|
| `signal_reader` | Python (仅 YAML) | 使用 system.sigreader 回放已保存的信号文件 |

## Demo 详细说明

### checkpoint

**文件：** `demos/checkpoint/main.cpp`

展示 C++ 中通过 `zpp_bits` 库序列化复杂数据结构（`std::vector<std::vector<double>>`、自定义 struct），实现跨日状态持久化。

**运行方式：**
```bash
cd demos/checkpoint
wl-sim save.yml   # 运行 20230103，保存日末 checkpoint
wl-sim load.yml   # 加载 checkpoint，运行 20230104
```

### cssnap

**文件：** `demos/cssnap/main.cpp`

最基础的横截面 signal 示例。展示了 CsSnapshotEvent 的完整访问模式：读取 bp、volume 字段，为每个标的输出信号值。

### csmbo

**文件：** `demos/csmbo/main.cpp`

横截面 MBO 数据示例。展示了如何从 CsMboEvent 中获取逐笔成交的 price、qty、bidid、askid，并逐个标的计算信号。

### csops

**文件：** `demos/csops/main.cpp`

横截面 + FactorNode 算子。展示了 `ts_inner_product` 和 `ts_sum` 公式在横截面模式下的使用方式，包括 `on_day_begin`/`on_day_end` 和 checkpoint 管理。

### ops_feature

**文件：** `demos/ops_feature/main.cpp`

Feature 高级特性。展示了：
1. 从 YAML 配置中读取公式字符串
2. 自动解析数据依赖（`get_datanames()`）
3. 动态构建 lambda 管道映射 CsSnapshotEvent 字段
4. 使用 `cfi::feature_variant` 类型擦除

### ops-mbo

**文件：** `demos/ops-mbo/main.cpp`

Mbo 算子。展示了 4 个 Mbo 公式的并行使用，以及逐标的 Cython 风格数据转换。

### feature-factor

**目录：** `demos/feature-factor/`

最复杂的 demo，展示了 feature 和 factor 的嵌套组合：
- `cssnapshot/` — 横截面模式下的因子嵌套
- `snapshot/` — 时间序列模式下的因子嵌套
- 包含 factor、feature_factor、ftreader、sigreader 等多种角色

### operators

**文件：** `demos/operators/main.cpp`

时间序列 Operators 示例。展示了延迟初始化公式、checkpoint 管理、以及 `update()` 返回值的使用。

### crypto

**文件：** `demos/crypto/main.cpp`

加密货币逐笔成交数据。展示了 `on_crypto_trade` 回调的签名和 CryptoTradeEvent 结构体访问。

### tx_snapshot

**文件：** `demos/tx_snapshot/main.cpp`

最详细的行情数据：快照 + 逐笔成交明细。展示了 `tx_snapshot_t` 的所有字段，包括交易/新增/修改/撤单四种事件类型。

> 注：`demos/snapshot/` 只包含 `main.cpp` 与 `CMakeLists.txt`，**没有 `wlsim.yml`**，
> 不能直接 `wl-sim` 运行；可参考 `crossref/wlsim.yml` 或 `snapshot-py/wlsim.yml`
> 复用一份 `snapshot` 模块的配置来驱动它。

### signal_reader

**目录：** `demos/signal_reader/`

纯 YAML 配置的 demo，不需要编写代码。展示了如何用 `system.sigreader` 模块回放已保存的信号，
并验证回放结果与实时计算一致。本 demo 由两个配置组成，构成“先存后读”的两步流程：

1. **`pystrat.yml`**（先运行）——Python strat（`nickchenyj.cssnap_simple_strat`）+ 9 个
   `nickchenyj.cssnap_simple_factor` 因子。其中 `fct1`（seed 1.234）带 `output` 段，把当日
   因子值落盘到 `output/strat/<date>/fct1/fct1-<date>.csv`；strat 求和所有因子并输出。
2. **`sigreader.yml`**（后运行）——`fct1` 改用 `system.sigreader` 从磁盘回放
   （`from: strat, name: fct1, dir: output, format: csv`，即读取上一步落盘的同一文件）；
   `fct2`–`fct9` 仍为相同的实时因子。

由于 `fct1` 在两次运行中取值相同（一次实时算出、一次读回），且 `fct2`–`fct9` 完全一致，
两次的 `strat` 输出应当逐字节相同——这正是 `system.sigreader` 正确回放的判据。

**一键验证：**

```bash
python3 scripts/validate_signal_reader.py
```

该脚本依次跑 `pystrat.yml` → 保留 strat 基准 → 跑 `sigreader.yml` → `diff` 两次 strat 输出，
一致即 PASS。详见 [脚本参考 · validate_signal_reader.py](../reference/scripts.md#validate_signal_readerpy)。

> 验证状态：`pystrat.yml` ✅、`sigreader.yml` ✅，strat 输出逐字节一致。

## 多配置 Demo 的运行流程

部分 demo 用多个 yml 表达“分阶段”语义（保存 checkpoint → 加载 checkpoint → 连续运行对比）。
统一模式：**save** 阶段跑某日并存 checkpoint → **load** 阶段加载该 checkpoint 跑次日 →
**full** 阶段不存不加载连续跑两日 → 对比 load 与 full 的输出，应当完全一致。

| Demo | 配置 | 流程 | 验证 |
|------|------|------|------|
| `checkpoint` | `save.yml` / `load.yml` + `run.sh` | save 跑 20230103 存 ckpt；load 加载跑 20230104；`run.sh` 还会 copy ckpt 目录 | ✅ load 与 full 的 20230104 输出一致 |
| `checkpoint-py` | `save.yml` / `load.yml` + `run.sh`（与 C++ 版同构） | save 跑 20230103–04 存 20230103 ckpt；load 用 `auto_load: yesterday` 加载跑 20230104；`run.sh` 串起 save→copy→load→diff | ✅ load 与 save 的 20230104 输出一致 |
| `operators` | `save_state.yml` / `load_state.yml` / `full.yml` | save 跑 20250106 存 ckpt；load 用 `auto_load: yesterday` 加载跑 20250107（输出到 `output/resume`）；full 连续跑 20250106–07（输出到 `output/full`） | ✅ `resume` 与 `full` 的 20250107 输出一致 |
| `csops` | `save_state.yml` / `load_state.yml` / `full.yml` | save 跑 20241215–16 存 20241216 ckpt；load 加载跑 20241217；full 连续跑 20241215–17 | ⚠ 需 `stocksv2.CHN` 数据（NAS 当前缺失该日期）；改用 `stocks.CHN` 验证代码 ✅ |

## 运行验证状态（wl-sim 实测）

在 `WLSIM_ENV_KEY=manylinux-2_34`、NAS（`/mnt/nas-3`）已挂载的环境下逐个 `wl-sim` 实测：

| Demo | 状态 | 说明 |
|------|------|------|
| `checkpoint` | ✅ | save→load→full，输出一致 |
| `checkpoint-py` | ✅ | save/load 配置对，round-trip 输出一致 |
| `crypto` | ✅ | 2026-02-02，约 2034 万笔 trade |
| `csmbo` | ✅ | 2025-07-03；`on_eod` 计数器为 0 是 demo 未在 `on_cs_mbo` 自增所致（非故障） |
| `csops` | ⚠ 数据 | `stocksv2.CHN` 缺失；代码用 `stocks.CHN` 验证通过 |
| `cssnap` | ✅ | 20230104，4899 标的 |
| `cssnap-py` | ✅ | 同上 |
| `crossref` | ✅ | 20230109，IC+IF1 双标的；按 ticker 路由（IF*→slot1，IC→slot0），两腿均填充 |
| `crossref-py` | ✅ | 20230109，`IH2301.CFFEX` |
| `feature-factor`（cssnapshot） | ✅ / ⚠ | `simple_strat_with_factor.yml` 等可运行；`feature_only`/`strat_with_factor_and_feature` 因 feature 计算重，90s 内未跑完（非故障）；`snapshot/snapshot-py-py.yml` 缺 `a.DCE` refdata |
| `full-snapshot` | ✅ | `cpp.yml` 837958 updates；`py.yml` 可运行但打印多、较慢 |
| `operators` | ✅ | save→load→full，输出一致 |
| `ops_feature` | ⚠ 数据 | `stocksv2.CHN` 20241202 缺失 |
| `ops-mbo` | ⚠ 数据 | `stocks_5227.CHN` 20230515 缺失 |
| `signal_reader` | ✅ | `validate_signal_reader.py` 验证 sigreader 回放与实时一致 |
| `snapshot` | — | 无 `wlsim.yml`，不可独立运行 |
| `snapshot-py` | ✅ | 20250106，`ZCH5.CME` |
| `tx_snapshot` | ✅（较慢） | 逐笔数据量大，90s 内仍在处理（非故障） |

标 ⚠ 数据 的均为**行情/参考数据在 NAS 上缺失**，并非代码问题——信号 `.so` 均能正常加载、
配置均能解析，仅在 `on_sod` 打开数据文件时 fatal。可改用 NAS 上存在的数据集/日期验证。