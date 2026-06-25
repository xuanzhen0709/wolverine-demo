> [📖 Docs](../README.md) › 参考 › 框架 API 参考

# 框架 API 参考

wolverine 与 cfi_operators 的头文件级速查。头文件位于 wlsim 运行时安装目录
（`<install>/include/wolverine/*.hpp`、`<install>/include/cfi_ops/*.h`）。
架构总览见 [框架架构](../overview/framework.md)。

> 所有用户类型在 `cfi::wolverine` 命名空间下；算子库在 `cfi` 命名空间下。
> 命名空间 `cfi::wolverine` 可用 `using namespace cfi::wolverine;` 引入。

---

## wolverine

### `signal.hpp` — Signal 插件

```cpp
struct SignalApis {              // 引擎 → signal 的能力注入（set_apis 时获得）
  void *token;
  const std::vector<std::string> &(*get_targets)(void *token);
  void (*update_signal)(void *token, int64_t exchtime, uint64_t localtime,
                        uint16_t ins_nr, const double *sigs);
  std::string (*get_static_data_dir)(void *token);
  std::string (*get_priv_data_dir)(void *token);
  std::string (*get_model_dir)(void *token);
  void (*get_factor_list)(void *token, std::vector<std::string> *factors);
  const double *(*get_factors)(void *token, int *ins_nr, int *factor_nr); // [factor_nr][ins_nr]
  const std::vector<double> *(*get_feature_buf)(void *token, std::string_view name);
  const std::vector<std::string> &(*get_factor_targets)(void *token, int idx);
  void (*load_factor_history)(void *token, std::string_view from, std::string_view name,
                              int date, std::vector<std::string> *targets,
                              std::vector<int64_t> *exchtime, std::vector<uint64_t> *localtime,
                              std::vector<double> *values);
  void (*register_priv_buf)(void *token, std::string_view name, void **buf);
  void **(*get_priv_buf)(void *token, std::string_view name);
};

struct SignalOps {               // signal → 引擎 的回调表（on_create 时填充）
  void (*initialize)(void *hdl, const Config *root);
  void (*set_apis)(void *hdl, SignalApis apis);
  void (*load_state)(void *hdl, const std::string &indir);
  void (*save_state)(void *hdl, const std::string &outdir);
  void (*on_sod)(void *hdl, const SodEvent *ev);
  void (*on_eod)(void *hdl, const EodEvent *ev);
  void (*on_snapshot)(void *hdl, const SnapshotEvent *ev);
  void (*on_full_snapshot)(void *hdl, const FullSnapshotEvent *ev);
  void (*on_bar)(void *hdl, const BarEvent *ev);
  void (*on_cs_snapshot)(void *hdl, const CsSnapshotEvent *ev);
  void (*on_cs_mbo)(void *hdl, const CsMboEvent *ev);
  void (*on_tx_snapshot)(void *hdl, const TxSnapshotEvent *ev);
  void (*on_crypto_trade)(void *hdl, int64_t exchtime, int64_t localtime,
                          const CryptoTradeEvent *ev);
  void (*on_destroy)(void *hdl);
};

// C 入口
typedef void (*SigOnCreateFunc)(void **, cfi::wolverine::SignalOps *);
```

⚠ `update_signal` 参数顺序为 **(token, exchtime, localtime, ins_nr, sigs)**。
`on_crypto_trade` 同理为 (exchtime, localtime, ev)。

### `feature.hpp` — Feature 插件（每标的一个实例）

```cpp
struct FeatureApis {
  void *token;
  void (*update)(void *token, int64_t exchtime, uint64_t localtime, const double *sigs);
  const std::vector<std::string> &(*get_targets)(void *token);
};
struct FeatureOps {
  void (*initialize)(void *hdl, const Config *root);
  void (*set_apis)(void *hdl, FeatureApis apis);
  void (*on_sod)(void *hdl, const SodEvent *ev);
  void (*on_eod)(void *hdl, const EodEvent *ev);
  void (*on_cs_snapshot)(void *hdl, const CsSnapshotEvent *ev);
  void (*on_cs_mbo)(void *hdl, const CsMboEvent *ev);
  void (*on_destroy)(void *hdl);
};
typedef void (*FeatureOnCreateFunc)(void **, cfi::wolverine::FeatureOps *, int insidx);
```

### `md_loader.hpp` / `signal_sink.hpp` — 行情源与输出消费

```cpp
struct MdLoaderOps {
  int  (*initialize)(void *hdl, const std::vector<std::string> &symbols, const Config *root);
  const Event *(*on_sod)(void *hdl, uint32_t date);
  void (*on_eod)(void *hdl, uint32_t date);
  const Event *(*read_next)(void *hdl);
  void (*on_destroy)(void *hdl);
};

struct signal_sink_ops_t {
  void (*initialize)(void *hdl, std::string_view root_name, std::string_view name, const Config *cfg);
  void (*on_set_targets)(void *hdl, const std::vector<std::string> &targets);
  void (*on_sod)(void *hdl, const SodEvent *ev);
  void (*on_update)(void *hdl, int64_t exchtime, uint64_t localtime, uint16_t ins_nr, const double *sigs);
  void (*on_eod)(void *hdl, const EodEvent *ev);
  void (*on_destroy)(void *hdl);
};
```

### `event.hpp` — 事件类型

`EventType`：Sod/Eod/Snapshot/Bar/CsSnapshot/CsMbo/FullSnapshot/TxSnapshot/CryptoTrade。
`MdSrcType` 同构。事件结构体摘要见 [框架架构 §事件与回调](../overview/framework.md#事件与回调)。

`CsSnapshotEvent::FldType`（20 项）：

| FldType | 类型 | dim | 含义 |
|---------|------|-----|------|
| `exchtime` | int64_t | 1 | 交易所时间 |
| `localtime` | uint64_t | 1 | 本地时间 |
| `last_price` | double | 1 | 最新价 |
| `volume` | int64_t | 1 | 累计成交量 |
| `turnover` | double | 1 | 累计成交额 |
| `open_interest` | int64_t | 1 | 持仓量 |
| `bp`/`bv` | double/int64_t | 2 | 买价/买量 [level][ins] |
| `ap`/`av` | double/int64_t | 2 | 卖价/卖量 [level][ins] |
| `bid_cnt`/`ask_cnt` | int64_t | 2 | 买/卖盘笔数 [level][ins] |
| `total_bid_qty`/`total_ask_qty` | int64_t | 1 | 总买/卖数量 |
| `total_bid_cnt`/`total_ask_cnt` | int64_t | 1 | 总买/卖笔数 |
| `total_bid_lvl`/`total_ask_lvl` | int32_t | 1 | 总买/卖档数 |
| `bid_volume`/`ask_volume` | int64_t | 1 | 买/卖成交量 |

`CsMboEvent` 三类消息 `OrderFldType`/`CancelFldType`/`TradeFldType`：均含 `Cnt` +
`Exchtime/Localtime/Price/Qty/Side/OrderId`（Order/Cancel 还有 BidPrice/BidSize/AskPrice/
AskSize/TradingPhase；Trade 有 BidId/AskId/TradingPhase）。访问形式 `fld[ins][msg_idx]`，
消息数 `cnt[ins]`。

### `marketdata.hpp` / `marketdata/types.hpp`

- `MdStatic`：tick_size/limitup/limitdown/multiplier/lot_size/session_nr/session[4]/instrument[16]/ticker[16]/exchange[8]。
- `MdSnapshot`：Level1/Level5；`MD_SNAPSHOT_MAX_LEVEL_NR=10`；ap/bp/av/bv[10]；total_bid/ask_vol。
- `MdFullSnapshot`：`level_nr` + 柔性数组 `MdLevel levels[]`（ap/bp/av/bv）。
- `MdBar`：OHLC + volume/turnover + twap_bid/twap_ask。
- `tx_snapshot_t`：packed，`static_assert sizeof==128`；含 `tx_t txs[]`（TRADE/NEW/MODIFY/CANCEL/EXEC/DUMMY）。
- `crypto_trade_t`：packed，`static_assert sizeof==25`；seq_no/price/qty/side。
- 枚举：`side_t`(BID=BUY=0,ASK=SELL=1,...)、`trading_status_t`、`snapshot_flag_t`、`trade_flag_t`。

### `config.hpp` — YAML 配置

```cpp
class Config {
  static Config from_file(std::string_view path);
  static Config from_string(const std::string &str);
  operator bool() const;                 // 是否定义
  void assert_valid() const;             // 缺失则 wllog_fatal
  template <typename T> Config operator[](const T &key) const;  // map/数组访问
  const_iterator begin() const / end() const;
  template <typename T> T as() const;
  template <typename T, typename S> T as(const S &fallback) const;
  template <typename T> void set_child(std::string_view key, T val);
  std::string full_path() const;
  bool is_array() const; size_t size() const;
  std::string to_string() const;
};
```

### `traits/` — 类型安全字段访问

```cpp
// cs_snapshot_traits.hpp
template <CsSnapshotEvent::FldType Fld> struct CsSnapshotTraits { using type; static constexpr int dim; static constexpr const char *name; };
struct CsSnapshotUtils {
  template <FldType Fld> static constexpr auto get_fld(const CsSnapshotEvent *ev);      // 无符号→有符号视图
  template <FldType Fld> static constexpr auto get_raw_fld(const CsSnapshotEvent *ev);  // 原始类型
  template <typename Base, size_t dim, typename Ptr> static constexpr auto cast_ptr(Ptr); // dim=1→T*, dim=2→T*const*
};

// cs_mbo_traits.hpp
struct CsMboUtils {
  template <OrderFldType Fld>  static constexpr auto get_fld(const CsMboEvent *ev);
  template <CancelFldType Fld> static constexpr auto get_fld(const CsMboEvent *ev);
  template <TradeFldType Fld>  static constexpr auto get_fld(const CsMboEvent *ev);
};
```

用法：`const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);` → `bp[level][ins]`。

### `logging.hpp` — 日志

```cpp
wllog_info(fmt, ...);    wllog_warn(fmt, ...);    wllog_error(fmt, ...);
wllog_debug(fmt, ...);   // NDEBUG 下编译为空
wllog_fatal(fmt, ...);   // fflush + exit(-1)
wllog_info_cont(fmt, ...);  // 无前缀续行
```

基于 fmt；时间戳为本地时区。`WLLOG_MYFUNC` 在 Release 下为 `__func__`，Debug 下为
`__PRETTY_FUNCTION__`。

### `utils/`

| 头文件 | 主要内容 |
|--------|----------|
| `time.hpp` | `str_to_exchtime`、`hhmmss_to_exchtime`、`ymdhmsf_to_exchtime/epoch`、`exchtime_to_str`、`epoch_to_str`、`parse_session`、`generate_bars_from_sessions`、`timestamp_cmp_func_t` |
| `mmap.hpp` | `ro_mapped_file_t`（只读 mmap，`open/read<T>/eof`） |
| `zstd.hpp` | `zstd_reader_t`（zstd 流式解压） |
| `lib.hpp` | `shared_lib_t`（dlopen + `get_function<T>(name)`） |
| `utils.hpp` | `create_directories`、`get_hostname/username`、`set_cpu/parse_cpuid`、`safe_read`、`get_command_output`、`duration_str2ns` |

### 其他

- `common.hpp`：`C_DECLARATION_BEGIN/END`、`likely/unlikely`、`WOLVERINE_EXPORT`、`CacheLineSz`。
- `constants.hpp`：NS/US/MS/S/MIN/HOUR/DAY 换算常量，KB/MB。
- `refdata.hpp`：`wlrefdata_find(symbol)`、`wlrefdata_parse_dataset`。
- `calendar.hpp`：`wlcalendar_shift(date, shift)`（交易日偏移）。
- `env.hpp`：`wlsim_getenv()`（运行时环境 Config）。

---

## cfi_operators (`cfi_ops/`)

所有类在 `cfi` 命名空间；`item_t = double`。

### `operators.h` — `Operators`（时序）

```cpp
class Operators {
  Operators();
  Operators(const std::string &formula, const std::string &id = "");
  void initialize(const std::string &formula, const std::string &id);
  template <typename... Args> item_t update(Args... args);  // 按位置匹配 @占位符
  size_t size() const;
  int get_op_arity(const std::string &op_name);
  void save_checkpoint(const std::string &cp_dir = "", bool binary = true);
  void load_checkpoint(const std::string &cp_dir = "", bool binary = true);
  void on_day_begin(uint32_t date);     // 注意：仅 date，无 ins_nr
  void on_day_end(uint32_t date);
};
```

### `factornode.h` — `FactorNode`（横截面）

```cpp
class FactorNode {
  FactorNode();
  FactorNode(const std::string &formula, size_t cs_len, const std::string &id = "");
  FactorNode(const std::string &formula, const std::string &id = "");
  void initialize(const std::string &formula, size_t cs_len, const std::string &id);
  void initialize(std::string formula, const std::string &id);
  void resize(size_t);
  [[deprecated]] const item_t *update(const item_t *, int);
  [[deprecated]] const item_t *update(const item_t *, const item_t *, int);  // 注意上游拼写 "Fearture"
  void save_checkpoint(...); void load_checkpoint(...);
  void on_day_begin(uint32_t, size_t cs_len);
  void on_day_end(uint32_t);
  size_t get_cs_len() const;
};
```

### `feature.h` — `Feature`（自动解析依赖）

```cpp
class Feature {
  Feature();
  Feature(const std::string &formula, size_t cs_len, const std::string &name = "");
  Feature(const std::string &formula, const std::string &name = "");
  void initialize(const std::string &formula, size_t cs_len, const std::string &name);
  void initialize(const std::string &formula, const std::string &name);
  const item_t *update(const std::vector<const item_t *> &inputs, int size);
  const item_t *update(const std::vector<feature_variant> &inputs, int size);
  std::vector<std::string> get_datanames();          // 公式所需字段名
  void save_checkpoint(...); void load_checkpoint(...);
  void on_day_begin(uint32_t, int cs_len);
  void on_day_end(uint32_t);
  static void register_user_data_dir(const std::string &dir);
  static void register_user_op_dir(const std::string &dir);
};
```

`feature_variant = std::variant<const int64_t*const, const int32_t*const, const double*const, const uint64_t*const>`。

### `mbo.h` — `Mbo`（逐笔聚合）

```cpp
class Mbo {
  Mbo();
  Mbo(const std::string &formula, size_t cs_len, const std::string &name = "");
  Mbo(const std::string &formula, const std::string &name = "");
  void initialize(...);
  item_t update(const item_t *x, const item_t *y, int size, int index);
  item_t update(const std::vector<const item_t *> &inputs, int size, int index);
  std::vector<std::string> get_datanames();
  void on_day_begin(uint32_t, int cs_len);
  void on_day_end(uint32_t);
  static void register_user_op_dir(const std::string &dir);
  // checkpoint 未启用（头文件中注释掉）
};
```

公式用 `sum_agg(@expr)` 做按标的聚合；逐笔字段如 `@trade_price/@trade_qty/@trade_side`。

### `checkpoint.h` / `linear_algebra.h` / `types.h` / `common_define.h`

```cpp
namespace cfi::checkpoint {
  void save_checkpoint(const std::vector<double>&, const std::string& name, const std::string& cp_dir, bool binary = true);
  void load_checkpoint(std::vector<double>&, ...);
  // 同名重载：vector<vector<double>>、deque<double>
}

namespace cfi {
  void dgemm(const double *A, const double *B, double *C, int m, int n, int k);
  void daddmm(const double *A, const double *B, double *C, int m, int n);
  void dsubmm(const double *A, const double *B, double *C, int m, int n);

  using item_t = double;
  using feature_variant = std::variant<const int64_t*const, const int32_t*const, const double*const, const uint64_t*const>;
}

// common_define.h 宏
NAN_FILTER(x...)          // 任一参数非有限 → 返回 NaN
CHECKPOINT(T)             // 在 OpBase 子类中注入 has_state/save_checkpoint/load_checkpoint（nlohmann::json）
DISALLOW_COPY_AND_ASSIGN(TypeName)
```
