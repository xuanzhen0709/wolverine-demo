// factors/snapshot/orderbook-imb —— 盘口深度不平衡（Order Book Imbalance）因子
//
// 目标：低频横截面因子。每天在 9 个固定 30 分钟时点
//   （09:30 / 10:00 / 10:30 / 11:00 / 13:00 / 13:30 / 14:00 / 14:30 / 15:00）各输出一次。
//   前 8 点与 stats/mid 对齐；末点 15:00 是收盘半小时（mid/label 无对应，见下）。
//
// 计算逻辑：
//   1. 每来一帧 cs-snapshot（约每 3s），对每个标的算一个「切片 OBI」：
//         obi = (bid_depth - ask_depth) / (bid_depth + ask_depth)
//      其中 bid_depth/ask_depth 为前 K 档买/卖挂量之和（K = depth_levels，
//      0 表示用当帧全部推送档位 level_nr）。单边缺失（涨跌停 / 停牌）记为无效切片跳过。
//   2. 每个标的维护一个「过去 window_sec 秒」的切片 OBI 滚动窗口（deque）。
//   3. 到达 9 个报出时点 t 时，对窗口内的切片 OBI 序列算 4 个汇总量并各自落盘：
//         mean  —— 等权均值（每个切片权重相同）
//         skew  —— 样本偏度  m3 / m2^1.5（n<3 或方差≈0 记 NaN）
//         twa   —— 时间加权均值（每个切片按其存活时长加权，即窗口内的 TWAP；
//                  相对 mean 的区别是「每秒等权」而非「每切片等权」，能正确处理
//                  午休 / 停牌等时间空洞）
//         last  —— 窗口内最后一个切片的 OBI（最新值）
//
// 各报出点的窗口（session 时间下的 [t-30min, t]，见「时钟」）：
//   09:30 落在开盘/集合竞价段；13:00 = 午前 11:00–11:30；15:00 = 收盘 14:30–15:00
//   （含尾盘连续竞价 + 收盘集合竞价）。开盘与收盘语义不同，**不做跨日填充**，
//   每天只用当天数据；开盘行(09:30)如何使用留给下游模型判断。
//
// 输出：**不走 update_signal**，自己开缓冲写文件，一次运行同时产出 4 个因子。
//   路径 / 格式与 stats/mid 对齐（父目录名 == 文件前缀）：
//     <output_dir>/<prefix>/mean/<date>/mean-<date>.csv
//     <output_dir>/<prefix>/skew/<date>/skew-<date>.csv
//     <output_dir>/<prefix>/twa /<date>/twa -<date>.csv
//     <output_dir>/<prefix>/last/<date>/last-<date>.csv
//   默认 output_dir=output/factors, prefix=obi → output/factors/obi/{mean,skew,twa,last}/<date>/
//   每个 csv：表头 exchtime,localtime,<symbol...>；9 个数据行；缺失写 "nan"。
//   前 8 行的 exchtime/localtime 与 stats/mid 逐行一致，可直接喂 compute_label.py /
//   cs_ic_calculator 做跨日 label 与 IC；第 9 行(15:00) label 无对应时钟点，
//   IC 计算里因 exchtime 不匹配自动跳过（不影响前 8 行；15:00 主要作为模型特征）。
//
// 时钟：默认用「session 时间」——只把午休(11:30–13:00)从时间轴上挤掉，其余恒等
//   （竞价/上午原样，午后左移 90min）。这样 13:00 的 30min 回看窗口 = session[11:00,11:30]
//   = wall 11:00–11:30 的真实分布，而非被午休空洞裁成「午后开盘单切片」。twa 的存活时长
//   也走 session 时间，午休段权重自动为 0。与 mbo/lifetime 的 session_ns 完全同源。
//   报出仍在 wall-clock 09:30/13:00/15:00… 触发（输出的 exchtime/localtime 两列不变）。
//
// 无 look-ahead：日内窗口 / 时长一律用 exchtime（当日纳秒，经 session 变换）；无跨日状态；
//   localtime（epoch）仅写列。
//
// 可选配置（signal.config）：
//   output_dir       : 输出根目录，默认 output/factors
//   prefix           : 因子分组名，默认 obi（→ output/factors/obi/<stat>/...）
//   window_sec       : 滚动窗口秒数，默认 1800（30 分钟）
//   depth_levels     : OBI 用的档位数 K，默认 0 = 用当帧全部档位
//   session_time     : true(默认)=按 session 时间(仅剔午休)算窗口/twa；false=wall-clock
//                      (不推荐，13:00 窗口近乎空)
//   label_times      : 覆盖默认 9 个报出时点（HHMMSS 整数数组）
//   min_slice_hhmmss : 只把 exchtime>=该时点的切片纳入窗口（HHMMSS），默认 0=全部纳入
//                      （例如设 93000 可剔除开盘集合竞价切片，避免跨竞价 / 连续竞价混用）

#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/format.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace cfi::wolverine;

namespace factors {
namespace orderbook_imb {

// 默认 9 个截面时间点（HHMMSS，交易所时间）。前 8 点与 stats/mid 对齐，
// 末点 15:00 为收盘半小时窗口（14:30–15:00，含尾盘 / 收盘集合竞价）。
static constexpr int kDefaultLabelTimes[] = {
    93000,  100000, 103000, 110000,  // 上午 09:30 / 10:00 / 10:30 / 11:00
    130000, 133000, 140000, 143000,  // 下午 13:00 / 13:30 / 14:00 / 14:30
    150000};                         // 收盘 15:00（窗口 14:30–15:00）

static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// —— session 时钟：只把午休(11:30–13:00)从时间轴挤掉，其余恒等（与 mbo/lifetime 同源）。
static constexpr int64_t kAmClose = 41400LL * 1000000000LL; // 11:30:00
static constexpr int64_t kPmOpen = 46800LL * 1000000000LL;  // 13:00:00
static constexpr int64_t kLunch = kPmOpen - kAmClose;       // 午休 90min
static inline int64_t session_ns(int64_t e)
{
  if (e < kPmOpen) return (e > kAmClose) ? kAmClose : e; // 竞价/上午恒等；午休钉 11:30
  return e - kLunch;                                     // 下午左移 90min
}

// 一个报出时点对某标的窗口的 4 个汇总量。
struct Agg {
  double mean = kNaN;
  double skew = kNaN;
  double twa = kNaN;
  double last = kNaN;
};

class Signal {
public:
  Signal() {}

  void initialize(const Config *root);
  void set_apis(SignalApis apis) { apis_ = apis; }
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

private:
  // 单个切片：session 时间（当日纳秒，已剔午休）+ 该切片的 OBI。
  struct Slice {
    int64_t s;
    double obi;
  };

  // 对某标的的窗口序列求 4 个汇总量。t_emit 为报出时点（用于 twa 的末段存活时长）。
  static Agg aggregate(const std::deque<Slice> &dq, int64_t t_emit);

  // session 时钟：use_session_ 时剔午休，否则用原始 exchtime。
  inline int64_t tclock(int64_t e) const { return use_session_ ? session_ns(e) : e; }

  // 由 MdStatic 构造 "code.exch" symbol 串（表头列名用）。
  static std::string make_sym(const MdStatic *ms);

  // 把某个报出时点的一行（exchtime,localtime,<vals...>）追加进对应 buffer。
  static void append_row(std::string &buf, int64_t ex, uint64_t lt,
                         const std::vector<double> &vals);

  // 落盘到 <output_dir>/<prefix>/<stat>/<date>/<stat>-<date>.csv。
  void flush_file(const std::string &stat, const std::string &buf,
                  std::uint32_t date) const;

  SignalApis apis_ = {nullptr};

  // 配置
  std::string output_dir_ = "output/factors";
  std::string prefix_ = "obi";
  int64_t window_ns_ = 1800LL * 1000000000LL; // 30 分钟
  int depth_levels_ = 0;                       // 0 = 用当帧全部档位
  int64_t min_slice_ns_ = std::numeric_limits<int64_t>::min(); // 切片纳入下限
  bool use_session_ = true;                    // 按 session 时间(剔午休)算窗口/twa

  // 每日状态
  std::vector<int64_t> targets_; // 已排序的报出时点（当日纳秒）
  std::size_t next_ = 0;         // 下一个待输出的目标下标
  std::uint16_t ins_nr_ = 0;
  std::string sym_hdr_;                     // "exchtime,localtime,<sym...>"（表头）
  std::vector<std::deque<Slice>> windows_;  // 每标的的滚动窗口
  // 4 个因子各自的当日 CSV 文本 buffer。
  std::string buf_mean_, buf_skew_, buf_twa_, buf_last_;
  // 复用的每标的输出向量（避免每个时点重新分配）。
  std::vector<double> v_mean_, v_skew_, v_twa_, v_last_;
};

void Signal::initialize(const Config *root)
{
  output_dir_ = (*root)["output_dir"].as<std::string>(output_dir_);
  prefix_ = (*root)["prefix"].as<std::string>(prefix_);
  depth_levels_ = (*root)["depth_levels"].as<int>(depth_levels_);
  use_session_ = (*root)["session_time"].as<bool>(use_session_);

  const int window_sec = (*root)["window_sec"].as<int>(1800);
  window_ns_ = static_cast<int64_t>(window_sec) * 1000000000LL;

  std::vector<int> hhmmss;
  if (auto node = (*root)["label_times"]) {
    hhmmss = node.as<std::vector<int>>();
  } else {
    hhmmss.assign(std::begin(kDefaultLabelTimes), std::end(kDefaultLabelTimes));
  }
  targets_.clear();
  targets_.reserve(hhmmss.size());
  for (int t : hhmmss) targets_.push_back(time::hhmmss_to_exchtime(t));
  std::sort(targets_.begin(), targets_.end());

  if (auto node = (*root)["min_slice_hhmmss"])
    min_slice_ns_ = time::hhmmss_to_exchtime(node.as<int>());

  wllog_info("orderbook-imb: prefix={}, window={}s, depth_levels={}({}), "
             "session_time={}, {} label times, out={}/{}/<stat>/<date>/\n",
             prefix_, window_sec, depth_levels_,
             depth_levels_ == 0 ? "all" : "topK", use_session_,
             targets_.size(), output_dir_, prefix_);
}

void Signal::on_sod(const SodEvent *ev)
{
  next_ = 0;
  ins_nr_ = ev->ins_nr;

  // 建立表头：exchtime,localtime,<code.exch...>（列顺序即列式数组下标）。
  sym_hdr_ = "exchtime,localtime";
  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    sym_hdr_.push_back(',');
    sym_hdr_ += make_sym(ev->ms[i]);
  }

  windows_.assign(ev->ins_nr, {});
  v_mean_.assign(ev->ins_nr, kNaN);
  v_skew_.assign(ev->ins_nr, kNaN);
  v_twa_.assign(ev->ins_nr, kNaN);
  v_last_.assign(ev->ins_nr, kNaN);

  // 4 个 buffer 各写表头。
  buf_mean_ = sym_hdr_ + "\n";
  buf_skew_ = sym_hdr_ + "\n";
  buf_twa_ = sym_hdr_ + "\n";
  buf_last_ = sym_hdr_ + "\n";

  wllog_info("orderbook-imb: date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  using FldType = CsSnapshotEvent::FldType;
  const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev); // bp[lvl][ins]
  const auto *ap = CsSnapshotUtils::get_fld<FldType::ap>(ev); // ap[lvl][ins]
  const auto *bv = CsSnapshotUtils::get_fld<FldType::bv>(ev); // bv[lvl][ins] (有符号视图)
  const auto *av = CsSnapshotUtils::get_fld<FldType::av>(ev); // av[lvl][ins]

  // 用当帧档位数与配置取交集：depth_levels_==0 → 全部推送档位。
  const int K = (depth_levels_ <= 0)
                    ? ev->level_nr
                    : std::min<int>(depth_levels_, ev->level_nr);

  // 1) 每个标的算一个切片 OBI，压入其滚动窗口，并裁掉窗口外的旧切片。
  const int64_t ex = ev->exchtime;      // 原始交易所时间（报出门控 / 输出列用）
  const int64_t now_s = tclock(ex);     // session 时间（窗口 / twa 用，剔午休）
  const bool ingest = ex >= min_slice_ns_;
  const int64_t cutoff = now_s - window_ns_; // 早于此的切片移出窗口
  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    auto &dq = windows_[i];
    // 先裁旧（无论是否纳入本切片，都保持「窗口 = 过去 window_sec」）。
    while (!dq.empty() && dq.front().s < cutoff) dq.pop_front();

    if (!ingest) continue;

    // 单边缺失（涨跌停单边 / 停牌 / 竞价前无报价）→ 无效切片，不纳入。
    const double best_b = bp[0][i];
    const double best_a = ap[0][i];
    if (!(best_b > 0.0 && best_a > 0.0)) continue;

    double bid_depth = 0.0, ask_depth = 0.0;
    for (int l = 0; l < K; ++l) {
      bid_depth += static_cast<double>(bv[l][i]);
      ask_depth += static_cast<double>(av[l][i]);
    }
    const double denom = bid_depth + ask_depth;
    if (!(denom > 0.0)) continue; // 无深度，跳过
    const double obi = (bid_depth - ask_depth) / denom;

    dq.push_back(Slice{now_s, obi});
  }

  // 2) 报出时点门控（与 stats/mid 一致）：一帧可能跨过多个目标，逐一补出。
  //    门控仍用原始 ex（在 wall-clock 09:30/13:00/15:00… 报出）；聚合的 t_emit 用 now_s。
  while (next_ < targets_.size() && ex >= targets_[next_]) {
    for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
      const Agg a = aggregate(windows_[i], now_s);
      v_mean_[i] = a.mean;
      v_skew_[i] = a.skew;
      v_twa_[i] = a.twa;
      v_last_[i] = a.last;
    }
    append_row(buf_mean_, ex, ev->localtime, v_mean_);
    append_row(buf_skew_, ex, ev->localtime, v_skew_);
    append_row(buf_twa_, ex, ev->localtime, v_twa_);
    append_row(buf_last_, ex, ev->localtime, v_last_);
    ++next_;
  }
}

Agg Signal::aggregate(const std::deque<Slice> &dq, int64_t t_emit)
{
  Agg out;
  const std::size_t n = dq.size();
  if (n == 0) return out; // 全 NaN

  // mean
  double sum = 0.0;
  for (const auto &s : dq) sum += s.obi;
  const double mean = sum / static_cast<double>(n);
  out.mean = mean;

  // skew = m3 / m2^1.5（总体中心矩）；n<3 或方差≈0 → NaN
  double m2 = 0.0, m3 = 0.0;
  for (const auto &s : dq) {
    const double d = s.obi - mean;
    m2 += d * d;
    m3 += d * d * d;
  }
  m2 /= static_cast<double>(n);
  m3 /= static_cast<double>(n);
  if (n >= 3 && m2 > 1e-18) out.skew = m3 / std::pow(m2, 1.5);

  // twa：按存活时长加权（窗口内 TWAP）。切片 k 的值持续到下一切片；
  // 最后一个切片持续到报出时点 t_emit。总权重为 0（罕见）时回退到 mean。
  double wsum = 0.0, wobi = 0.0;
  for (std::size_t k = 0; k < n; ++k) {
    const int64_t nxt = (k + 1 < n) ? dq[k + 1].s : t_emit;
    double w = static_cast<double>(nxt - dq[k].s);
    if (w < 0.0) w = 0.0; // 时间单调保护（正常不触发）
    wsum += w;
    wobi += w * dq[k].obi;
  }
  out.twa = (wsum > 0.0) ? (wobi / wsum) : mean;

  // last：窗口内最新切片
  out.last = dq.back().obi;
  return out;
}

void Signal::append_row(std::string &buf, int64_t ex, uint64_t lt,
                        const std::vector<double> &vals)
{
  fmt::format_to(std::back_inserter(buf), "{},{}", ex, lt);
  for (double v : vals) {
    buf.push_back(',');
    if (std::isfinite(v))
      fmt::format_to(std::back_inserter(buf), "{}", v);
    else
      buf += "nan"; // 与 stats/mid / compute_label 的缺失表示一致
  }
  buf.push_back('\n');
}

void Signal::flush_file(const std::string &stat, const std::string &buf,
                        std::uint32_t date) const
{
  const std::string date_s = fmt::format("{}", date);
  // <output_dir>/<prefix>/<stat>/<date>/
  const std::string dir = output_dir_ + "/" + prefix_ + "/" + stat + "/" + date_s;
  std::error_code fec;
  std::filesystem::create_directories(dir, fec);
  if (fec) {
    wllog_error("orderbook-imb: cannot create dir {}: {}\n", dir, fec.message());
    return;
  }
  const std::string path = dir + "/" + stat + "-" + date_s + ".csv";
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) {
    wllog_error("orderbook-imb: cannot open {}\n", path);
    return;
  }
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

void Signal::on_eod(const EodEvent *ev)
{
  if (next_ < targets_.size())
    wllog_warn("orderbook-imb: only {}/{} time points emitted on {}\n", next_,
               targets_.size(), ev->date);

  flush_file("mean", buf_mean_, ev->date);
  flush_file("skew", buf_skew_, ev->date);
  flush_file("twa", buf_twa_, ev->date);
  flush_file("last", buf_last_, ev->date);

  wllog_info("orderbook-imb: {} done, wrote 4 factors ({} rows each)\n",
             ev->date, next_);
}

std::string Signal::make_sym(const MdStatic *ms)
{
  std::string code{ms->instrument};
  code.erase(std::find(code.begin(), code.end(), '\0'), code.end());
  std::string exch{ms->exchange};
  exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());
  return code + "." + exch;
}

} // namespace orderbook_imb
} // namespace factors

using factors::orderbook_imb::Signal;

C_DECLARATION_BEGIN;

static const SignalOps my_ops = {
    .initialize = [](void *hdl, const Config *root) -> void
    { reinterpret_cast<Signal *>(hdl)->initialize(root); },

    .set_apis = [](void *hdl, SignalApis apis) -> void
    { reinterpret_cast<Signal *>(hdl)->set_apis(apis); },

    .on_sod = [](void *hdl, const SodEvent *ev) -> void
    { reinterpret_cast<Signal *>(hdl)->on_sod(ev); },

    .on_eod = [](void *hdl, const EodEvent *ev) -> void
    { reinterpret_cast<Signal *>(hdl)->on_eod(ev); },

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void
    { reinterpret_cast<Signal *>(hdl)->on_cs_snapshot(ev); },
};

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
