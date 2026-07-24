// factors/mbo/lifetime —— 委托存活时间（order lifespan）分布形态因子
//
// 思路：用逐笔 MBO 的 order_id 生命周期，刻画"被动挂单挂多久才撤 / 才被吃掉成交"的
//   分布形态与买卖不对称。大量"秒撤单"（lifetime < 3–6s）是算法试探 / 幌骗（spoof）
//   指纹；买方秒撤多 = 虚假买盘支撑 → 次日偏空；卖方秒撤多 = 虚假压力 → 次日偏多。
//   每个被动成交片段的存活时长（fill lifetime）刻画成交发生时该委托已排队多久；
//   部分成交后撤单的委托，其已成交部分进入 fill，剩余量仍进入 cancel。
//
// ============================ Resting 口径与沪深差异（关键）============================
// 见 docs/research/mbo-data-handling.md。本因子只统计订单**进入 order book 后**的生命周期：
//   • 纯主动且全成交：没有 resting 阶段，不统计；
//   • 主动部分成交后有剩余：从 residual 真正挂入簿子的时刻重新起算，后续被动成交/
//     撤单正常统计；不能因为该 order_id 曾经是 taker 就永久排除。
//   主动方由 Trade.Side 解析（实测 20250604：Side 100% 填 BID/ASK，语义为主动方）：
//     Side==BID ⇒ 主动买 ⇒ bid_id=taker(主动)、ask_id=maker(被动)；
//     Side==ASK ⇒ 主动卖 ⇒ ask_id=taker(主动)、bid_id=maker(被动)。
//   （TRADED_BID/TRADED_ASK 若出现，同 BID/ASK 处理。）
//   • 深市：Order 流广播原始委托（含主动单）。taker 成交先从原始量扣除；若仍有
//     residual，则在最后一次主动成交时刻新建 resting cohort。
//   • 沪市：纯主动单不发 Order；主动部分成交后的 residual 挂簿时才发 Order，Order.qty
//     已代表挂入簿子的量。因此同 order_id 的 taker 成交只用于确定 resting 起点，不能再从
//     沪市 Order.qty 重复扣减。
//   ⇒ 因此横截面使用时**务必分市场（6/688→SH、0/3→SZ）各自 winsorize+zscore/rank
//      再合并**，否则残余的市场级 level/scale 差异会污染排序。本因子只输出**裸值**。
//   • ⚠ 撤单的 volume 用 remaining = qty0 − filled（真正撤走的量），绝不用 Cancel.qty
//     （实测 Cancel.qty 恒等于原始下单量）。
//   • order_id 交易所内唯一、日内不复用；按标的各维护一份字典，天然无跨股冲突。
//
// ============================ 计算（每标的，过去 window_sec 秒回看）============================
// 维护 LiveOrders: order_id → {rest_arrival, qty0, filled, is_buy, plidx}。逐笔：
//   Order  → 建记录；push 一条 PlacedRec 进"挂单窗口 plwin"（cohort 分母用）。
//   Trade  → taker 腿不记 fill lifetime；若主动成交后仍有 residual，结束旧 cohort 并从
//            本次主动成交时刻新建 resting cohort。maker 腿的每个成交片段记成交寿命，
//            life=该次成交时刻−rest_arrival、vol=实际成交量。
//   Cancel → 找 resting 记录；lifetime=撤单时刻−rest_arrival；
//            push 一条**撤单寿命** cwin（vol=remaining）；若 life<阈值置 plwin 的秒撤位；删记录。
// 时间一律用 exchtime（当日纳秒），严格无 look-ahead。
//
// ★ 午休处理（默认 session_time=true）：窗口回看与 lifetime 都按「剔除午休的交易时间」
//   计，仅把午休 11:30–13:00 压缩为端点；集合竞价段(09:15–09:25)保留真实逐笔时间戳。
//   于是 13:00 的过去 30min 窗口 = 11:00–11:30（不跨午休）；15:00 窗口 = 14:30–15:00
//   （收盘集合竞价成交戳≥15:00:00，可靠触发）；跨午休委托的 lifetime 自动剔除 90min。
//
// ============================ 两类窗口 ============================
//   • 分布窗（cwin/fwin）：按**事件时间**（撤单/每次被动成交时刻）裁到 [now−W, now]。
//   • cohort 窗（plwin）：按**到达时间**裁到 (now−W−τ, now−τ]。τ 是冷却期，保证 cohort
//     内每单都有 ≥τ (≥最大秒撤阈值) 的时间暴露出是否秒撤 → fleet_order 分子分母是**同一批
//     订单**。默认 τ=6s，可配 settle_sec。
//
// ============================ 输出 76 个因子（每个单独一个 CSV）============================
//   分布块 64 个：每 (事件∈{cancel,fill}) × (方向∈{buy,sell}) 输出 16 个统计量：
//     <event>_<side>_loglife_q05/q10/q25/q50/q75/q90/q95  等权 log(lifetime秒) 分位
//     <event>_<side>_loglife_skew                          log lifetime 偏度（总体 m3/m2^1.5）
//     <event>_<side>_loglife_bimod                         Sarle 双峰系数 (skew²+1)/kurt
//     <event>_<side>_vw_loglife_q05..q95                   量加权分位（权=vol）
//   fleet 因子 12 个：
//     fleet3_cancel_{buy,sell,imb}  秒撤(<3s)量 / 该侧被动撤单量；imb=(rb−rs)/(rb+rs)
//     fleet6_cancel_{buy,sell,imb}  秒撤(<6s) 版本
//     fleet3_order_{buy,sell,imb}   cohort内该侧秒撤(<3s)量 / 该侧被动挂单量；imb 同上
//     fleet6_order_{buy,sell,imb}   秒撤(<6s) 版本
//   预期方向：fleet*_imb（买方虚假支撑多）→ 次日偏空（负相关）。
//
// 输出：不走 update_signal，自写文件，路径/格式与 stats/mid 对齐：
//   <output_dir>/<prefix>/<series>/<date>/<series>-<date>.csv
//   默认 output/factors/lifetime/<series>/<date>/...；表头 exchtime,localtime,<sym...>；
//   9 行（09:30/10:00/10:30/11:00/13:00/13:30/14:00/14:30/15:00）；缺失写 "nan"。
//
// 可选配置（signal.config）：
//   output_dir       : 默认 output/factors
//   prefix           : 因子分组名，默认 lifetime
//   window_sec       : 回看窗口秒数，默认 1800（30 分钟）
//   settle_sec       : cohort 冷却期 τ 秒（fleet_order 用），默认 6；须 ≥ 最大秒撤阈值 6s
//   label_times      : 覆盖默认 9 个报出时点（HHMMSS 数组）
//   min_event_hhmmss : 只纳入 exchtime>=该时点的逐笔（HHMMSS），默认 0=全部（含竞价段）
//   min_samples      : 每个 (事件×方向) 分布块样本下限，不足则该块分布类因子记 NaN，默认 5
//                      （兼容旧键 min_cancels 作默认值）。
//   session_time     : 默认 true，用交易时间(剔除午休)算窗口与 lifetime。

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
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace cfi::wolverine;

namespace factors {
namespace lifetime {

// 9 个默认报出时点（含收盘 15:00，补出 14:30–15:00 窗口 → 一天 9 行）。
static constexpr int kDefaultLabelTimes[] = {
    93000,  100000, 103000, 110000, 130000,
    133000, 140000, 143000, 150000};

static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A 股当日纳秒的关键时点。午休 11:30–13:00 是唯一"无委托活动"的死区，剔除它以免
// 人为拉长跨午休委托的 lifetime。集合竞价(09:15–09:25)与连续竞价段保留真实时间戳。
static constexpr int64_t kAmClose = 41400LL * 1000000000LL; // 11:30:00
static constexpr int64_t kPmOpen = 46800LL * 1000000000LL;  // 13:00:00
static constexpr int64_t kLunch = kPmOpen - kAmClose;       // 午休 90min
static constexpr int64_t kPmClose = 54000LL * 1000000000LL; // 15:00:00
// cs-mbo 没有 15:00 outer callback；15:01 才 flush 14:57 后的委托和收盘成交。
static constexpr int64_t kCloseFlush = 54060LL * 1000000000LL; // 15:01:00

// 把当日 exchtime 映射为"剔除午休后的交易时间"（当日纳秒）：
//   • 竞价/上午(<11:30)：恒等；午休[11:30,13:00)：钉在 11:30；下午(>=13:00)：整体左移 90min。
static inline int64_t session_ns(int64_t e)
{
  if (e < kPmOpen) return (e > kAmClose) ? kAmClose : e;
  return e - kLunch;
}

// 7 个分位点。
static constexpr int kNProbs = 7;
static constexpr double kProbs[kNProbs] = {0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95};

// 秒撤单两个阈值（秒）。
static constexpr double kFleetThr[2] = {3.0, 6.0};

// ---- 序列命名 / 索引（避免手写 76 项枚举）----
// 每个 (事件×方向) 分布块 16 个统计量。
static constexpr int kNStat = 16;
static const char *kStat[kNStat] = {
    "loglife_q05", "loglife_q10", "loglife_q25", "loglife_q50",
    "loglife_q75", "loglife_q90", "loglife_q95",
    "loglife_skew", "loglife_bimod",
    "vw_loglife_q05", "vw_loglife_q10", "vw_loglife_q25", "vw_loglife_q50",
    "vw_loglife_q75", "vw_loglife_q90", "vw_loglife_q95"};
static const char *kEventName[2] = {"cancel", "fill"};
static const char *kSideName[2] = {"buy", "sell"};

static constexpr int kFleetBase = 64;    // 分布块占 0..63，fleet 占 64..75
static constexpr int N_SERIES = 76;

// 分布块基址：event∈{0=cancel,1=fill}，side∈{0=buy,1=sell}。
static constexpr int dist_base(int ev, int sd) { return (ev * 2 + sd) * kNStat; }
// fleet 索引：thr∈{0=3s,1=6s}，kind∈{0=cancel,1=order}，col∈{0=buy,1=sell,2=imb}。
static constexpr int fleet_idx(int thr, int kind, int col)
{
  return kFleetBase + thr * 6 + kind * 3 + col;
}

// ---- 分位数 / 矩 / imb 小工具 ----

// 等权分位（sorted 升序，n>=1），线性插值（numpy 默认）。
static double quantile_lin(const std::vector<double> &s, double q)
{
  const std::size_t n = s.size();
  if (n == 1) return s[0];
  const double h = q * (static_cast<double>(n) - 1.0);
  const std::size_t lo = static_cast<std::size_t>(std::floor(h));
  if (lo + 1 >= n) return s[n - 1];
  return s[lo] + (h - static_cast<double>(lo)) * (s[lo + 1] - s[lo]);
}

// 量加权分位（vw 按 value 升序，权重>0，totw>0），Hazen 位置线性插值。
static double weighted_quantile(const std::vector<std::pair<double, double>> &vw,
                                double totw, double q)
{
  const std::size_t n = vw.size();
  if (n == 1) return vw[0].first;
  double cum = 0.0;
  double prev_p = 0.0, prev_v = vw[0].first;
  for (std::size_t i = 0; i < n; ++i) {
    cum += vw[i].second;
    const double p = (cum - 0.5 * vw[i].second) / totw;
    if (q <= p) {
      if (i == 0) return vw[0].first;
      const double dp = p - prev_p;
      if (dp <= 0.0) return vw[i].first;
      return prev_v + (q - prev_p) / dp * (vw[i].first - prev_v);
    }
    prev_p = p;
    prev_v = vw[i].first;
  }
  return vw[n - 1].first;
}

// 归一化不对称：(rb−rs)/(rb+rs) ∈ [−1,1]（第 6 点约定）。
static inline double norm_imb(double rb, double rs)
{
  if (std::isfinite(rb) && std::isfinite(rs) && (rb + rs) > 1e-12)
    return (rb - rs) / (rb + rs);
  return kNaN;
}

// 填一个分布块的 16 个槽（等权 7 分位 + skew + bimod + 量加权 7 分位）。
// ll/vw 会被就地排序；样本不足 min_n 则整块留 NaN。
static void dist_block(std::vector<double> &ll,
                       std::vector<std::pair<double, double>> &vw,
                       std::array<double, N_SERIES> &a, int base, std::size_t min_n)
{
  const std::size_t n = ll.size();
  if (n < min_n) return;

  std::sort(ll.begin(), ll.end());
  for (int j = 0; j < kNProbs; ++j) a[base + j] = quantile_lin(ll, kProbs[j]);

  // 偏度 / 双峰（总体中心矩）；需 n>=3 且方差>eps。
  double mean = 0;
  for (double v : ll) mean += v;
  mean /= static_cast<double>(n);
  double m2 = 0, m3 = 0, m4 = 0;
  for (double v : ll) {
    const double d = v - mean;
    const double d2 = d * d;
    m2 += d2; m3 += d2 * d; m4 += d2 * d2;
  }
  m2 /= n; m3 /= n; m4 /= n;
  if (n >= 3 && m2 > 1e-12) {
    const double skew = m3 / std::pow(m2, 1.5);
    a[base + 7] = skew;
    const double kurt = m4 / (m2 * m2); // 总体峰度（正态=3）
    if (kurt > 1e-12) a[base + 8] = (skew * skew + 1.0) / kurt;
  }

  // 量加权分位（权=vol）。
  double totw = 0;
  for (const auto &p : vw) totw += p.second;
  if (totw > 0.0) {
    std::sort(vw.begin(), vw.end(),
              [](const auto &x, const auto &y) { return x.first < y.first; });
    for (int j = 0; j < kNProbs; ++j)
      a[base + 9 + j] = weighted_quantile(vw, totw, kProbs[j]);
  }
}

class Signal {
public:
  Signal() {}

  void initialize(const Config *root);
  void set_apis(SignalApis apis) { apis_ = apis; }
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_mbo(const CsMboEvent *ev);

private:
  struct OrderRec {
    int64_t arrival;   // 当前 residual 进入 order book 的交易时间（session_ns）
    int64_t qty0;      // 当前 resting cohort 的初始量
    int64_t filled;    // 当前 resting cohort 的累计被动成交量
    uint8_t is_buy;    // 1=买(BID) 0=卖(ASK)
    uint64_t plidx;    // 回指 plwin_ 的绝对下标（O(1) 标 aggressive/秒撤）
  };
  struct LifeRec {     // 撤单寿命(cwin) 与 被动成交片段寿命(fwin) 共用
    int64_t s;         // 事件时刻的交易时间（撤单/该次成交时刻），窗口裁剪键
    float loglife;     // log(max(lifetime_sec, 1e-3))
    float life_sec;    // 交易时长秒数（撤单侧判秒撤单阈值用）
    float vol;         // cancel: remaining=qty0−filled；fill: 本次实际成交量
    uint8_t is_buy;
  };
  struct PlacedRec {   // cohort 挂单窗口，按到达排序
    int64_t arrival;   // 当前 residual 真正进入 order book 的交易时间
    float vol;         // residual qty（fleet_order 分母 = 实际挂簿量）
    uint8_t is_buy;
    uint8_t aggressive; // 纯主动全成交、没有 residual resting 阶段时置 1
    uint8_t fc_mask;    // bit th 置位表示该单被以 life<kFleetThr[th] 撤单
    float fc_vol;       // 秒撤时的 remaining=qty0−filled（fleet_order 分子 = 实际撤走量）
  };

  inline int64_t tclock(int64_t e) const
  {
    return use_session_ ? session_ns(e) : e;
  }
  void trim_windows(int64_t now_s);
  void emit_target(int64_t target, uint64_t localtime);
  std::array<double, N_SERIES> aggregate(int i, int64_t now_s) const;
  static void append_row(std::string &buf, int64_t ex, uint64_t lt,
                         const std::vector<double> &vals);
  void flush_file(const char *series, const std::string &buf,
                  std::uint32_t date) const;

  SignalApis apis_ = {nullptr};

  // 配置
  std::string output_dir_ = "output/factors";
  std::string prefix_ = "lifetime";
  int64_t window_ns_ = 1800LL * 1000000000LL;
  int64_t tau_ns_ = 6LL * 1000000000LL;
  int64_t min_event_ns_ = std::numeric_limits<int64_t>::min();
  std::size_t min_samples_ = 5;
  bool use_session_ = true;

  // 每日状态
  std::vector<int64_t> targets_;
  std::size_t next_ = 0;
  std::uint16_t ins_nr_ = 0;
  std::string sym_hdr_;
  std::array<std::string, N_SERIES> series_names_;

  std::vector<std::unordered_map<uint64_t, OrderRec>> live_; // [ins] order_id→rec
  std::vector<std::deque<LifeRec>> cwin_;                    // [ins] 撤单寿命窗口
  std::vector<std::deque<LifeRec>> fwin_;                    // [ins] 成交寿命窗口
  std::vector<std::deque<PlacedRec>> plwin_;                 // [ins] 挂单 cohort 窗口
  std::vector<uint64_t> plbase_;                             // [ins] plwin 已弹出数
  std::vector<uint64_t> plnext_;                             // [ins] plwin 累计推入数
  std::vector<uint8_t> is_sh_;                               // [ins] 沪市=1，深市=0
  uint64_t last_localtime_ = 0;                              // 最近已处理 callback

  // 输出：76 个 buffer + 复用的每标的行向量。
  std::array<std::string, N_SERIES> buf_;
  std::array<std::vector<double>, N_SERIES> rowvals_;
};

void Signal::initialize(const Config *root)
{
  output_dir_ = (*root)["output_dir"].as<std::string>(output_dir_);
  prefix_ = (*root)["prefix"].as<std::string>(prefix_);
  const int window_sec = (*root)["window_sec"].as<int>(1800);
  window_ns_ = static_cast<int64_t>(window_sec) * 1000000000LL;
  const int tau_sec = (*root)["settle_sec"].as<int>(6);
  tau_ns_ = static_cast<int64_t>(tau_sec) * 1000000000LL;
  // min_samples（兼容旧键 min_cancels 作默认）。
  const int min_cancels_compat = (*root)["min_cancels"].as<int>(5);
  min_samples_ =
      static_cast<std::size_t>((*root)["min_samples"].as<int>(min_cancels_compat));
  use_session_ = (*root)["session_time"].as<bool>(true);

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

  if (auto node = (*root)["min_event_hhmmss"])
    min_event_ns_ = time::hhmmss_to_exchtime(node.as<int>());

  // 生成 76 个序列名。
  for (int ev = 0; ev < 2; ++ev)
    for (int sd = 0; sd < 2; ++sd)
      for (int st = 0; st < kNStat; ++st)
        series_names_[dist_base(ev, sd) + st] =
            std::string(kEventName[ev]) + "_" + kSideName[sd] + "_" + kStat[st];
  static const char *thrName[2] = {"3", "6"};
  static const char *kindName[2] = {"cancel", "order"};
  static const char *colName[3] = {"buy", "sell", "imb"};
  for (int th = 0; th < 2; ++th)
    for (int kd = 0; kd < 2; ++kd)
      for (int col = 0; col < 3; ++col)
        series_names_[fleet_idx(th, kd, col)] =
            std::string("fleet") + thrName[th] + "_" + kindName[kd] + "_" +
            colName[col];

  wllog_info("lifetime: prefix={}, window={}s, tau={}s, session_time={}, "
             "{} label times, min_samples={}, {} series, out={}/{}/<series>/<date>/\n",
             prefix_, window_sec, tau_sec, use_session_, targets_.size(),
             min_samples_, int(N_SERIES), output_dir_, prefix_);
}

void Signal::on_sod(const SodEvent *ev)
{
  next_ = 0;
  ins_nr_ = ev->ins_nr;
  last_localtime_ = 0;

  sym_hdr_ = "exchtime,localtime";
  is_sh_.clear();
  is_sh_.reserve(ev->ins_nr);
  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string code{ms->instrument};
    code.erase(std::find(code.begin(), code.end(), '\0'), code.end());
    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());
    is_sh_.push_back(exch == "SH" ? 1 : 0);
    sym_hdr_.push_back(',');
    sym_hdr_ += code;
    sym_hdr_.push_back('.');
    sym_hdr_ += exch;
  }

  live_.assign(ev->ins_nr, {});
  cwin_.assign(ev->ins_nr, {});
  fwin_.assign(ev->ins_nr, {});
  plwin_.assign(ev->ins_nr, {});
  plbase_.assign(ev->ins_nr, 0);
  plnext_.assign(ev->ins_nr, 0);

  for (int s = 0; s < N_SERIES; ++s) {
    buf_[s] = sym_hdr_ + "\n";
    rowvals_[s].assign(ev->ins_nr, kNaN);
  }
  wllog_info("lifetime: date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::trim_windows(int64_t now_s)
{
  const int64_t cutoff = now_s - window_ns_;
  const int64_t plcut = now_s - window_ns_ - tau_ns_;
  for (int i = 0; i < ins_nr_; ++i) {
    auto &cwin = cwin_[i];
    auto &fwin = fwin_[i];
    auto &plwin = plwin_[i];
    while (!cwin.empty() && cwin.front().s < cutoff) cwin.pop_front();
    while (!fwin.empty() && fwin.front().s < cutoff) fwin.pop_front();
    while (!plwin.empty() && plwin.front().arrival <= plcut) {
      plwin.pop_front();
      ++plbase_[i];
    }
  }
}

void Signal::emit_target(int64_t target, uint64_t localtime)
{
  const int64_t target_s = tclock(target);
  trim_windows(target_s);
  for (int i = 0; i < ins_nr_; ++i) {
    const auto a = aggregate(i, target_s);
    for (int s = 0; s < N_SERIES; ++s) rowvals_[s][i] = a[s];
  }
  for (int s = 0; s < N_SERIES; ++s)
    append_row(buf_[s], target, localtime, rowvals_[s]);
}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  using O = CsMboEvent::OrderFldType;
  using C = CsMboEvent::CancelFldType;
  using T = CsMboEvent::TradeFldType;
  const int64_t ex = ev->exchtime;
  const auto ins_nr = ev->ins_nr;

  // callback 已越过普通目标：先用上一批处理完成后的状态输出，不能让当前批穿越目标。
  // 唯一例外是 15:01 的收盘 flush：它承载 14:57–15:00 数据，必须先处理再报 15:00。
  while (next_ < targets_.size() && ex > targets_[next_] &&
         !(targets_[next_] == kPmClose && ex == kCloseFlush)) {
    emit_target(targets_[next_],
                last_localtime_ ? last_localtime_ : ev->localtime);
    ++next_;
  }
  const bool close_flush =
      next_ < targets_.size() && targets_[next_] == kPmClose &&
      ex == kCloseFlush;
  const int64_t process_now_s =
      tclock(close_flush ? targets_[next_] : ex);
  trim_windows(process_now_s);

  // 取三流字段指针（各字段可能为 null，逐一判空）。
  const uint32_t *o_cnt = ev->orders ? CsMboUtils::get_fld<O::Cnt>(ev) : nullptr;
  const int64_t *const *o_ext =
      ev->orders ? CsMboUtils::get_fld<O::Exchtime>(ev) : nullptr;
  const int64_t *const *o_qty =
      ev->orders ? CsMboUtils::get_fld<O::Qty>(ev) : nullptr;
  const Side *const *o_side =
      ev->orders ? CsMboUtils::get_fld<O::Side>(ev) : nullptr;
  const uint64_t *const *o_oid =
      ev->orders ? CsMboUtils::get_fld<O::OrderId>(ev) : nullptr;

  const uint32_t *t_cnt = ev->trades ? CsMboUtils::get_fld<T::Cnt>(ev) : nullptr;
  const int64_t *const *t_ext =
      ev->trades ? CsMboUtils::get_fld<T::Exchtime>(ev) : nullptr;
  const int64_t *const *t_qty =
      ev->trades ? CsMboUtils::get_fld<T::Qty>(ev) : nullptr;
  const Side *const *t_side =
      ev->trades ? CsMboUtils::get_fld<T::Side>(ev) : nullptr;
  const uint64_t *const *t_bid =
      ev->trades ? CsMboUtils::get_fld<T::BidId>(ev) : nullptr;
  const uint64_t *const *t_ask =
      ev->trades ? CsMboUtils::get_fld<T::AskId>(ev) : nullptr;

  const uint32_t *c_cnt = ev->cancels ? CsMboUtils::get_fld<C::Cnt>(ev) : nullptr;
  const int64_t *const *c_ext =
      ev->cancels ? CsMboUtils::get_fld<C::Exchtime>(ev) : nullptr;
  const uint64_t *const *c_oid =
      ev->cancels ? CsMboUtils::get_fld<C::OrderId>(ev) : nullptr;

  for (int i = 0; i < ins_nr; ++i) {
    auto &live = live_[i];
    auto &cwin = cwin_[i];
    auto &fwin = fwin_[i];
    auto &plwin = plwin_[i];

    // 1) Order：建记录 + push PlacedRec（先于 Trade/Cancel 处理，保证同批因果顺序）。
    if (o_cnt && o_ext && o_qty && o_side && o_oid) {
      const auto cnt = o_cnt[i];
      const auto *ext = o_ext[i];
      const auto *qty = o_qty[i];
      const auto *side = o_side[i];
      const auto *oid = o_oid[i];
      if (ext && qty && side && oid) {
        for (uint32_t k = 0; k < cnt; ++k) {
          const int64_t arr = ext[k];
          if (arr < min_event_ns_) continue;
          const int64_t arr_s = tclock(arr);
          const int64_t q = qty[k];
          const uint8_t is_buy = (side[k] == Side::BID) ? 1 : 0;
          const uint64_t idx = plnext_[i]++;
          live[oid[k]] = OrderRec{arr_s, q, 0, is_buy, idx};
          plwin.push_back(
              PlacedRec{arr_s, static_cast<float>(q), is_buy, 0, 0, 0});
        }
      }
    }

    // 2) Trade：taker 腿只负责把原始委托转换成 residual resting cohort；
    //    maker 腿才记录 fill lifetime。
    if (t_cnt && t_ext && t_qty && t_side && t_bid && t_ask) {
      const auto cnt = t_cnt[i];
      const auto *text = t_ext[i];
      const auto *qty = t_qty[i];
      const auto *tside = t_side[i];
      const auto *bid = t_bid[i];
      const auto *ask = t_ask[i];
      if (text && qty && tside && bid && ask) {
        for (uint32_t k = 0; k < cnt; ++k) {
          const int64_t q = qty[k];
          if (q <= 0) continue;

          // BID/TRADED_BID→bid 是 taker、ask 是 maker；ASK 方向反之。
          const Side sd = tside[k];
          uint64_t taker = 0, maker = 0;
          if (sd == Side::BID || sd == Side::TRADED_BID) {
            taker = bid[k];
            maker = ask[k];
          } else if (sd == Side::ASK || sd == Side::TRADED_ASK) {
            taker = ask[k];
            maker = bid[k];
          } else {
            continue; // 主动方向未知时无法唯一确定 maker，宁缺毋滥。
          }

          const int64_t fill_s = tclock(text[k]);

          // taker：主动成交本身没有 resting lifetime。若成交后有 residual，则把原始
          // cohort 就地重置为从本次主动成交结束时开始的 resting cohort。
          if (auto it = live.find(taker); it != live.end()) {
            OrderRec &r = it->second;
            const int64_t before = std::max<int64_t>(r.qty0 - r.filled, 0);
            // 沪市 Order.qty 已是主动成交后实际挂簿的 residual；深市 Order.qty 是
            // 原始委托量，须扣除本次主动成交。
            const int64_t residual =
                is_sh_[i] ? before : std::max<int64_t>(before - q, 0);

            PlacedRec *p = nullptr;
            if (r.plidx >= plbase_[i]) {
              const std::size_t pos = r.plidx - plbase_[i];
              if (pos < plwin.size()) p = &plwin[pos];
            }

            if (residual <= 0) {
              if (p) p->aggressive = 1; // 纯主动全成交，不进入 resting 分母。
              live.erase(it);
            } else {
              r.arrival = fill_s;
              r.qty0 = residual;
              r.filled = 0;
              if (p) {
                p->arrival = fill_s;
                p->vol = static_cast<float>(residual);
                p->aggressive = 0;
                p->fc_mask = 0;
                p->fc_vol = 0;
              }
            }
          }

          // maker：当前 resting cohort 被动成交；部分成交片段均记 lifetime。
          auto it = live.find(maker);
          if (it == live.end()) continue;
          OrderRec &r = it->second;
          const int64_t remaining_before =
              std::max<int64_t>(r.qty0 - r.filled, 0);
          const int64_t fill_vol = std::min(q, remaining_before);
          if (fill_vol <= 0) continue;

          int64_t life_ns = fill_s - r.arrival;
          if (life_ns < 0) life_ns = 0;
          const double life_sec = static_cast<double>(life_ns) * 1e-9;
          const double loglife = std::log(std::max(life_sec, 1e-3));
          fwin.push_back(LifeRec{fill_s, static_cast<float>(loglife),
                                 static_cast<float>(life_sec),
                                 static_cast<float>(fill_vol), r.is_buy});

          r.filled += fill_vol;
          if (r.filled >= r.qty0) {
            live.erase(it);
          }
        }
      }
    }

    // 3) Cancel：当前 live 均代表 resting residual；算 lifetime + remaining。
    if (c_cnt && c_ext && c_oid) {
      const auto cnt = c_cnt[i];
      const auto *ext = c_ext[i];
      const auto *oid = c_oid[i];
      if (ext && oid) {
        for (uint32_t k = 0; k < cnt; ++k) {
          const int64_t cex = ext[k];
          if (cex < min_event_ns_) continue;
          auto it = live.find(oid[k]);
          if (it == live.end()) continue; // 无对应挂单（跨市/竞价残留等）→ 跳过
          OrderRec &r = it->second;
          const int64_t remaining = r.qty0 - r.filled;
          if (remaining <= 0) { live.erase(it); continue; } // 已全成交
          const int64_t cex_s = tclock(cex);
          int64_t life_ns = cex_s - r.arrival; // r.arrival 已是交易时间 → 剔除午休
          if (life_ns < 0) life_ns = 0;
          const double life_sec = static_cast<double>(life_ns) * 1e-9;
          const double loglife = std::log(std::max(life_sec, 1e-3));
          cwin.push_back(LifeRec{cex_s, static_cast<float>(loglife),
                                 static_cast<float>(life_sec),
                                 static_cast<float>(remaining), r.is_buy});
          uint8_t mask = 0;
          for (int th = 0; th < 2; ++th)
            if (life_sec < kFleetThr[th]) mask |= static_cast<uint8_t>(1u << th);
          if (mask && r.plidx >= plbase_[i]) {
            const std::size_t pos = r.plidx - plbase_[i];
            if (pos < plwin.size()) {
              plwin[pos].fc_mask |= mask;
              plwin[pos].fc_vol = static_cast<float>(remaining);
            }
          }
          live.erase(it);
        }
      }
    }
  }

  // 精确命中目标时，当前 callback 属于该窗口，处理后输出。
  // 15:01 收盘 flush 也在处理后标记为 15:00 输出。
  while (next_ < targets_.size() &&
         (ex == targets_[next_] ||
          (targets_[next_] == kPmClose && ex == kCloseFlush))) {
    emit_target(targets_[next_], ev->localtime);
    ++next_;
  }
  last_localtime_ = ev->localtime;
}

std::array<double, N_SERIES> Signal::aggregate(int i, int64_t now_s) const
{
  std::array<double, N_SERIES> a;
  a.fill(kNaN);

  // ---- 4 个分布块（cancel/fill × buy/sell）+ 顺带累计 fleet_cancel ----
  const std::deque<LifeRec> *wins[2] = {&cwin_[i], &fwin_[i]}; // 0=cancel 1=fill
  double cvol[2] = {0, 0};             // [side] 被动撤单总量
  double fv[2][2] = {{0, 0}, {0, 0}};  // [thr][side] 秒撤单量
  for (int ev = 0; ev < 2; ++ev) {
    std::vector<double> ll_b, ll_s;
    std::vector<std::pair<double, double>> vw_b, vw_s;
    for (const auto &r : *wins[ev]) {
      if (r.is_buy) {
        ll_b.push_back(r.loglife);
        vw_b.emplace_back(r.loglife, static_cast<double>(r.vol));
      } else {
        ll_s.push_back(r.loglife);
        vw_s.emplace_back(r.loglife, static_cast<double>(r.vol));
      }
      if (ev == 0) { // 撤单侧 → fleet_cancel 累计
        const int sd = r.is_buy ? 0 : 1;
        cvol[sd] += r.vol;
        for (int th = 0; th < 2; ++th)
          if (r.life_sec < kFleetThr[th]) fv[th][sd] += r.vol;
      }
    }
    dist_block(ll_b, vw_b, a, dist_base(ev, 0), min_samples_);
    dist_block(ll_s, vw_s, a, dist_base(ev, 1), min_samples_);
  }

  // fleet_cancel 比率 + imb（不受 min_samples 限制，只要分母>0）。
  for (int th = 0; th < 2; ++th) {
    const double rb = (cvol[0] > 0) ? fv[th][0] / cvol[0] : kNaN;
    const double rs = (cvol[1] > 0) ? fv[th][1] / cvol[1] : kNaN;
    a[fleet_idx(th, 0, 0)] = rb;
    a[fleet_idx(th, 0, 1)] = rs;
    a[fleet_idx(th, 0, 2)] = norm_imb(rb, rs);
  }

  // ---- fleet_order：cohort 扫描（到达∈(lb,ub]，剔除主动单；分母=qty0挂单量，分子=remaining实际撤走量）----
  const int64_t lb = now_s - window_ns_ - tau_ns_;
  const int64_t ub = now_s - tau_ns_;
  double denom[2] = {0, 0};            // [side] 被动挂单量(qty0)
  double num[2][2] = {{0, 0}, {0, 0}}; // [thr][side] cohort内秒撤单实际撤走量(remaining)
  for (const auto &p : plwin_[i]) {
    if (p.arrival > ub) break; // plwin 按到达升序 → 其余更新，跳出
    // 裁剪已保证 arrival > lb；此处 arrival <= ub。
    if (p.aggressive) continue;
    const int sd = p.is_buy ? 0 : 1;
    denom[sd] += p.vol;
    for (int th = 0; th < 2; ++th)
      if (p.fc_mask & static_cast<uint8_t>(1u << th)) num[th][sd] += p.fc_vol;
  }
  for (int th = 0; th < 2; ++th) {
    const double rb = (denom[0] > 0) ? num[th][0] / denom[0] : kNaN;
    const double rs = (denom[1] > 0) ? num[th][1] / denom[1] : kNaN;
    a[fleet_idx(th, 1, 0)] = rb;
    a[fleet_idx(th, 1, 1)] = rs;
    a[fleet_idx(th, 1, 2)] = norm_imb(rb, rs);
  }
  return a;
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
      buf += "nan";
  }
  buf.push_back('\n');
}

void Signal::flush_file(const char *series, const std::string &buf,
                        std::uint32_t date) const
{
  const std::string date_s = fmt::format("{}", date);
  const std::string dir =
      output_dir_ + "/" + prefix_ + "/" + series + "/" + date_s;
  std::error_code fec;
  std::filesystem::create_directories(dir, fec);
  if (fec) {
    wllog_error("lifetime: cannot create dir {}: {}\n", dir, fec.message());
    return;
  }
  const std::string path = dir + "/" + series + "-" + date_s + ".csv";
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) {
    wllog_error("lifetime: cannot open {}\n", path);
    return;
  }
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

void Signal::on_eod(const EodEvent *ev)
{
  if (next_ < targets_.size())
    wllog_warn("lifetime: only {}/{} time points emitted on {}\n", next_,
               targets_.size(), ev->date);
  for (int s = 0; s < N_SERIES; ++s)
    flush_file(series_names_[s].c_str(), buf_[s], ev->date);
  wllog_info("lifetime: {} done, wrote {} factors ({} rows each)\n", ev->date,
             int(N_SERIES), next_);
}

} // namespace lifetime
} // namespace factors

using factors::lifetime::Signal;

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

    .on_cs_mbo = [](void *hdl, const CsMboEvent *ev) -> void
    { reinterpret_cast<Signal *>(hdl)->on_cs_mbo(ev); },
};

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
