// factors/mbo/queue-jump —— 最优档一步改善（queue jump）焦虑因子
//
// 研究问题：
//   当买卖价差允许被动改善一个 tick 时，新委托是在原最优价排到队尾（JOIN），
//   还是多付一个 tick 建立新最优价（STEP1）？买方比卖方更频繁地 STEP1，表示
//   买方更急于获得成交优先级；卖方更频繁则相反。
//
// ================================ 事件定义 =================================
//
// 仅在连续竞价、旧 BBO 双边有效、spread >= 2 ticks 时形成二元选择 cohort：
//
//   Buy JOIN  : order_px == old_bid
//   Buy STEP1 : order_px == old_bid + 1 tick && order_px < old_ask
//
//   Sell JOIN  : order_px == old_ask
//   Sell STEP1 : order_px == old_ask - 1 tick && order_px > old_bid
//
// 深档单、主动/可成交单、多 tick 改善单、一档价差下的订单均不进入该 cohort。
// 这样分母只比较“排旧队尾”和“付 1 tick 抢新队首”，不会把 SZ 主动单或没有改善
// 机会的一档价差状态机械计成 0。
//
// ============================= pre/post BBO 语义 =============================
//
// 本数据源中 Order/Cancel 自带的 BidPrice/BidSize/AskPrice/AskSize 是该消息处理后的
// post-event BBO，不能把当前 Order 行的 BBO 当成 old BBO。每股缓存上一条可信的
// post-event BBO，当前 Order 必须通过严格状态转移校验才进入 cohort：
//
//   JOIN  : post_best == old_best，post_size == old_size + order_qty，
//           对手盘价量不变；
//   STEP1 : post_best == order_px，post_size == order_qty，
//           对手盘价量不变。
//
// JOIN 的 queue_ahead 近似等于 old best size；STEP1 的新价位 ahead=0，它绕过的
// 队列量等于 old best size。
//
// Trade 没有 post BBO。逐股按 (Exchtime, Localtime) 合并三流；若某组 Trade 的
// 主动 order_id 不在同组 Order 中（沪市缺失 taker 的常见情形），缓存 BBO 标 dirty。
// dirty 后第一条 Order/Cancel 只用于重同步，不参与分类。所有未通过状态转移校验的
// 订单同样只更新 post BBO，不进入分子或分母。
//
// =============================== 30 分钟输出 ================================
//
// 每个已验证的 JOIN/STEP1 进入 (t-window, t] 的交易时钟窗口，维护以下量：
//
//   count_rate_s  = STEP1_count_s / eligible_count_s
//   volume_rate_s = STEP1_qty_s   / eligible_qty_s
//   urgency_s     = sum_STEP1 qty*log1p(old_best_qty/qty) / eligible_qty_s
//
//   *_asym = *_buy - *_sell
//
// 正的 asym 表示买方更愿意付一个 tick 抢优先级。另输出 Jeffreys 0.5 平滑的
// count log-odds asym，以及 eligible/jump count 诊断序列。
//
// 因子只统计连续竞价 [09:30,11:30) 与 [13:00,14:57)，但竞价期 O/C 仍可用于
// 初始化 post BBO。默认使用剔除午休的 session clock 维护 30 分钟窗口，并在
// 15:00 输出最后一行；该行覆盖 (14:30,15:00]，其中 14:57 后的收盘集合竞价
// 不进入 queue-jump cohort。
//
// 输出目录：
//   <output_dir>/<prefix>/<series>/<date>/<series>-<date>.csv
//
// 默认：
//   output/factors/queue_jump/<series>/<date>/...

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
#include <stdexcept>
#include <string>
#include <vector>

using namespace cfi::wolverine;

namespace factors {
namespace queue_jump {

static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
static constexpr int64_t kNsPerSec = 1000000000LL;

static constexpr int64_t kAmOpen = 34200LL * kNsPerSec;   // 09:30
static constexpr int64_t kAmClose = 41400LL * kNsPerSec;  // 11:30
static constexpr int64_t kPmOpen = 46800LL * kNsPerSec;   // 13:00
static constexpr int64_t kCloseAuction = 53820LL * kNsPerSec; // 14:57
static constexpr int64_t kDayClose = 54000LL * kNsPerSec; // 15:00
static constexpr int64_t kLunch = kPmOpen - kAmClose;

static constexpr int kDefaultLabelTimes[] = {
    93000,  100000, 103000, 110000,
    130000, 133000, 140000, 143000, 150000};

static inline int64_t session_ns(int64_t e)
{
  if (e < kPmOpen) return (e > kAmClose) ? kAmClose : e;
  return e - kLunch;
}

static inline bool in_continuous_session(int64_t e)
{
  return (e >= kAmOpen && e < kAmClose) ||
         (e >= kPmOpen && e < kCloseAuction);
}

enum Series {
  COUNT_RATE_BUY,
  COUNT_RATE_SELL,
  COUNT_RATE_ASYM,
  VOLUME_RATE_BUY,
  VOLUME_RATE_SELL,
  VOLUME_RATE_ASYM,
  URGENCY_BUY,
  URGENCY_SELL,
  URGENCY_ASYM,
  LOG_ODDS_ASYM,
  ELIGIBLE_COUNT_BUY,
  ELIGIBLE_COUNT_SELL,
  JUMP_COUNT_BUY,
  JUMP_COUNT_SELL,
  N_SERIES
};

static const char *kSeriesName[N_SERIES] = {
    "count_rate_buy",
    "count_rate_sell",
    "count_rate_asym",
    "volume_rate_buy",
    "volume_rate_sell",
    "volume_rate_asym",
    "urgency_buy",
    "urgency_sell",
    "urgency_asym",
    "log_odds_asym",
    "eligible_count_buy",
    "eligible_count_sell",
    "jump_count_buy",
    "jump_count_sell"};

struct EventKey {
  int64_t ex = std::numeric_limits<int64_t>::max();
  uint64_t lt = std::numeric_limits<uint64_t>::max();
};

static inline bool key_less(const EventKey &a, const EventKey &b)
{
  return a.ex < b.ex || (a.ex == b.ex && a.lt < b.lt);
}

static inline bool key_equal(const EventKey &a, const EventKey &b)
{
  return a.ex == b.ex && a.lt == b.lt;
}

class Signal {
public:
  void initialize(const Config *root);
  void set_apis(SignalApis apis) { apis_ = apis; }
  void on_sod(const SodEvent *ev);
  void on_cs_mbo(const CsMboEvent *ev);
  void on_eod(const EodEvent *ev);

private:
  struct TopState {
    int64_t bid_tick = 0;
    int64_t ask_tick = 0;
    int64_t bid_size = 0;
    int64_t ask_size = 0;
    bool clean = false;
  };

  struct Choice {
    int64_t s = 0;
    int64_t qty = 0;
    double urgency_num = 0.0;
    uint8_t is_buy = 0;
    uint8_t is_jump = 0;
  };

  struct SideSums {
    int64_t eligible_count = 0;
    int64_t jump_count = 0;
    int64_t eligible_qty = 0;
    int64_t jump_qty = 0;
    double urgency_num = 0.0;
  };

  struct RollingState {
    std::deque<Choice> choices;
    SideSums buy;
    SideSums sell;
  };

  struct OrderCols {
    const uint32_t *cnt = nullptr;
    const int64_t *const *ex = nullptr;
    const uint64_t *const *lt = nullptr;
    const double *const *px = nullptr;
    const int64_t *const *qty = nullptr;
    const Side *const *side = nullptr;
    const uint64_t *const *oid = nullptr;
    const double *const *bp = nullptr;
    const int64_t *const *bs = nullptr;
    const double *const *ap = nullptr;
    const int64_t *const *as = nullptr;
  };

  struct CancelCols {
    const uint32_t *cnt = nullptr;
    const int64_t *const *ex = nullptr;
    const uint64_t *const *lt = nullptr;
    const double *const *bp = nullptr;
    const int64_t *const *bs = nullptr;
    const double *const *ap = nullptr;
    const int64_t *const *as = nullptr;
  };

  struct TradeCols {
    const uint32_t *cnt = nullptr;
    const int64_t *const *ex = nullptr;
    const uint64_t *const *lt = nullptr;
    const Side *const *side = nullptr;
    const uint64_t *const *bid = nullptr;
    const uint64_t *const *ask = nullptr;
  };

  inline int64_t tclock(int64_t e) const
  {
    return use_session_ ? session_ns(e) : e;
  }

  bool price_to_tick(std::size_t ins, double px, int64_t &out) const;
  bool sync_top(std::size_t ins, double bp, int64_t bs,
                double ap, int64_t as);
  void mark_dirty(std::size_t ins);
  void add_choice(std::size_t ins, const Choice &c);
  void prune(std::size_t ins, int64_t cutoff);
  void handle_order(std::size_t ins, int64_t ex, double px, int64_t qty,
                    Side side, double post_bp, int64_t post_bs,
                    double post_ap, int64_t post_as);
  void process_instrument(std::size_t ins, const OrderCols &o,
                          const CancelCols &c, const TradeCols &t,
                          int64_t outer_ex);

  std::array<double, N_SERIES> aggregate(std::size_t ins) const;
  void emit(int64_t ex, uint64_t lt, bool valid);
  static void append_row(std::string &buf, int64_t ex, uint64_t lt,
                         const std::vector<double> &vals);
  void flush_file(const char *series, const std::string &buf,
                  std::uint32_t date) const;
  static std::string make_symbol(const MdStatic *ms);

  SignalApis apis_ = {nullptr};

  // Config.
  std::string output_dir_ = "output/factors";
  std::string prefix_ = "queue_jump";
  int64_t window_ns_ = 1800LL * kNsPerSec;
  int64_t min_orders_ = 20;
  bool use_session_ = true;
  bool exclude_limits_ = true;

  // Daily state.
  std::uint32_t date_ = 0;
  std::uint16_t ins_nr_ = 0;
  bool day_valid_ = true;
  uint64_t last_localtime_ = 0;
  int64_t last_outer_ex_ = 0;
  std::vector<int64_t> targets_;
  std::size_t next_target_ = 0;
  std::string symbol_header_;

  std::vector<double> tick_size_;
  std::vector<int64_t> limit_up_tick_;
  std::vector<int64_t> limit_down_tick_;
  std::vector<uint8_t> instrument_valid_;
  std::vector<TopState> top_;
  std::vector<RollingState> rolling_;

  std::array<std::string, N_SERIES> buffers_;
  std::array<std::vector<double>, N_SERIES> row_values_;

  // Daily diagnostics.
  uint64_t n_orders_ = 0;
  uint64_t n_candidates_ = 0;
  uint64_t n_verified_ = 0;
  uint64_t n_jumps_ = 0;
  uint64_t n_post_mismatch_ = 0;
  uint64_t n_dirty_trade_groups_ = 0;
  uint64_t n_invalid_bbo_ = 0;
  uint64_t n_invalid_message_ = 0;
  uint64_t n_future_inner_groups_ = 0;
  uint64_t n_missed_targets_ = 0;
};

void Signal::initialize(const Config *root)
{
  output_dir_ = (*root)["output_dir"].as<std::string>(output_dir_);
  prefix_ = (*root)["prefix"].as<std::string>(prefix_);
  use_session_ = (*root)["session_time"].as<bool>(use_session_);
  exclude_limits_ = (*root)["exclude_limits"].as<bool>(exclude_limits_);

  const int window_sec = (*root)["window_sec"].as<int>(1800);
  min_orders_ = (*root)["min_orders"].as<int64_t>(20);
  if (window_sec <= 0) throw std::invalid_argument("queue_jump: window_sec must be > 0");
  if (min_orders_ <= 0) throw std::invalid_argument("queue_jump: min_orders must be > 0");
  window_ns_ = static_cast<int64_t>(window_sec) * kNsPerSec;

  std::vector<int> hhmmss;
  if (auto node = (*root)["label_times"]) {
    hhmmss = node.as<std::vector<int>>();
  } else {
    hhmmss.assign(std::begin(kDefaultLabelTimes), std::end(kDefaultLabelTimes));
  }
  if (hhmmss.empty()) throw std::invalid_argument("queue_jump: label_times is empty");

  targets_.clear();
  targets_.reserve(hhmmss.size());
  for (int t : hhmmss) targets_.push_back(time::hhmmss_to_exchtime(t));
  std::sort(targets_.begin(), targets_.end());
  if (std::adjacent_find(targets_.begin(), targets_.end()) != targets_.end())
    throw std::invalid_argument("queue_jump: duplicate label_times");

  wllog_info("queue_jump: prefix={}, window={}s, min_orders={}, session_time={}, "
             "exclude_limits={}, targets={}, out={}/{}/<series>/<date>/\n",
             prefix_, window_sec, min_orders_, use_session_, exclude_limits_,
             targets_.size(), output_dir_, prefix_);
}

bool Signal::price_to_tick(std::size_t ins, double px, int64_t &out) const
{
  if (ins >= tick_size_.size()) return false;
  const double tick = tick_size_[ins];
  if (!(std::isfinite(px) && px > 0.0 && std::isfinite(tick) && tick > 0.0))
    return false;

  const double scaled = px / tick;
  if (!(scaled >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        scaled <= static_cast<double>(std::numeric_limits<int64_t>::max())))
    return false;

  const int64_t rounded = static_cast<int64_t>(std::llround(scaled));
  const double reconstructed = static_cast<double>(rounded) * tick;
  const double tolerance = std::max(1e-8, tick * 1e-5);
  if (std::abs(px - reconstructed) > tolerance) return false;
  out = rounded;
  return true;
}

bool Signal::sync_top(std::size_t ins, double bp, int64_t bs,
                      double ap, int64_t as)
{
  int64_t bt = 0, at = 0;
  if (ins >= top_.size() || bs <= 0 || as <= 0 ||
      !price_to_tick(ins, bp, bt) || !price_to_tick(ins, ap, at) || bt >= at) {
    if (ins < top_.size()) top_[ins].clean = false;
    ++n_invalid_bbo_;
    return false;
  }

  top_[ins] = TopState{bt, at, bs, as, true};
  return true;
}

void Signal::mark_dirty(std::size_t ins)
{
  if (ins < top_.size()) top_[ins].clean = false;
}

void Signal::add_choice(std::size_t ins, const Choice &c)
{
  auto &r = rolling_[ins];
  r.choices.push_back(c);
  SideSums &s = c.is_buy ? r.buy : r.sell;
  ++s.eligible_count;
  s.eligible_qty += c.qty;
  if (c.is_jump) {
    ++s.jump_count;
    s.jump_qty += c.qty;
    s.urgency_num += c.urgency_num;
  }
}

void Signal::prune(std::size_t ins, int64_t cutoff)
{
  auto &r = rolling_[ins];
  while (!r.choices.empty() && r.choices.front().s <= cutoff) {
    const Choice c = r.choices.front();
    r.choices.pop_front();
    SideSums &s = c.is_buy ? r.buy : r.sell;
    --s.eligible_count;
    s.eligible_qty -= c.qty;
    if (c.is_jump) {
      --s.jump_count;
      s.jump_qty -= c.qty;
      s.urgency_num -= c.urgency_num;
      if (s.urgency_num < 0.0 && s.urgency_num > -1e-6)
        s.urgency_num = 0.0;
    }
  }
}

void Signal::handle_order(std::size_t ins, int64_t ex, double px, int64_t qty,
                          Side side, double post_bp, int64_t post_bs,
                          double post_ap, int64_t post_as)
{
  ++n_orders_;
  int64_t p_tick = 0, post_bid_tick = 0, post_ask_tick = 0;
  const bool post_valid =
      post_bs > 0 && post_as > 0 &&
      price_to_tick(ins, post_bp, post_bid_tick) &&
      price_to_tick(ins, post_ap, post_ask_tick) &&
      post_bid_tick < post_ask_tick;

  if (qty <= 0 || (side != Side::BID && side != Side::ASK) ||
      !price_to_tick(ins, px, p_tick)) {
    ++n_invalid_message_;
    if (post_valid)
      top_[ins] = TopState{post_bid_tick, post_ask_tick, post_bs, post_as, true};
    else
      mark_dirty(ins);
    return;
  }

  const TopState old = top_[ins];
  bool candidate = false;
  bool jump = false;
  bool buy = false;

  if (old.clean && post_valid && in_continuous_session(ex) &&
      old.ask_tick - old.bid_tick >= 2) {
    if (side == Side::BID && p_tick < old.ask_tick) {
      if (p_tick == old.bid_tick) {
        candidate = true;
        buy = true;
      } else if (p_tick == old.bid_tick + 1) {
        candidate = true;
        jump = true;
        buy = true;
      }
    } else if (side == Side::ASK && p_tick > old.bid_tick) {
      if (p_tick == old.ask_tick) {
        candidate = true;
      } else if (p_tick == old.ask_tick - 1) {
        candidate = true;
        jump = true;
      }
    }
  }

  if (candidate && exclude_limits_) {
    if ((buy && limit_up_tick_[ins] > 0 && p_tick >= limit_up_tick_[ins]) ||
        (!buy && limit_down_tick_[ins] > 0 && p_tick <= limit_down_tick_[ins]))
      candidate = false;
  }

  if (candidate) {
    ++n_candidates_;
    bool own_transition = false;
    bool opposite_unchanged = false;
    if (buy) {
      const bool add_fits =
          old.bid_size <= std::numeric_limits<int64_t>::max() - qty;
      own_transition =
          post_bid_tick == p_tick &&
          (jump ? post_bs == qty
                : add_fits && post_bs == old.bid_size + qty);
      opposite_unchanged =
          post_ask_tick == old.ask_tick && post_as == old.ask_size;
    } else {
      const bool add_fits =
          old.ask_size <= std::numeric_limits<int64_t>::max() - qty;
      own_transition =
          post_ask_tick == p_tick &&
          (jump ? post_as == qty
                : add_fits && post_as == old.ask_size + qty);
      opposite_unchanged =
          post_bid_tick == old.bid_tick && post_bs == old.bid_size;
    }

    if (own_transition && opposite_unchanged) {
      const int64_t bypass = buy ? old.bid_size : old.ask_size;
      const double urgency =
          jump ? static_cast<double>(qty) *
                     std::log1p(static_cast<double>(bypass) /
                                static_cast<double>(qty))
               : 0.0;
      add_choice(ins, Choice{tclock(ex), qty, urgency,
                             static_cast<uint8_t>(buy),
                             static_cast<uint8_t>(jump)});
      ++n_verified_;
      if (jump) ++n_jumps_;
    } else {
      ++n_post_mismatch_;
    }
  }

  if (post_valid)
    top_[ins] = TopState{post_bid_tick, post_ask_tick, post_bs, post_as, true};
  else {
    mark_dirty(ins);
    ++n_invalid_bbo_;
  }
}

void Signal::process_instrument(std::size_t ins, const OrderCols &o,
                                const CancelCols &c, const TradeCols &t,
                                int64_t outer_ex)
{
  const uint32_t on = o.cnt ? o.cnt[ins] : 0;
  const uint32_t cn = c.cnt ? c.cnt[ins] : 0;
  const uint32_t tn = t.cnt ? t.cnt[ins] : 0;

  const bool order_ok =
      on == 0 || (o.ex && o.lt && o.px && o.qty && o.side && o.oid &&
                  o.bp && o.bs && o.ap && o.as &&
                  o.ex[ins] && o.lt[ins] && o.px[ins] && o.qty[ins] &&
                  o.side[ins] && o.oid[ins] && o.bp[ins] && o.bs[ins] &&
                  o.ap[ins] && o.as[ins]);
  const bool cancel_ok =
      cn == 0 || (c.ex && c.lt && c.bp && c.bs && c.ap && c.as &&
                  c.ex[ins] && c.lt[ins] && c.bp[ins] && c.bs[ins] &&
                  c.ap[ins] && c.as[ins]);
  const bool trade_ok =
      tn == 0 || (t.ex && t.lt && t.side && t.bid && t.ask &&
                  t.ex[ins] && t.lt[ins] && t.side[ins] &&
                  t.bid[ins] && t.ask[ins]);

  if (!order_ok || !cancel_ok || !trade_ok) {
    instrument_valid_[ins] = 0;
    mark_dirty(ins);
    ++n_invalid_message_;
    return;
  }

  uint32_t oi = 0, ci = 0, ti = 0;
  std::vector<uint64_t> group_order_ids;
  EventKey previous_key;
  bool have_previous_key = false;
  auto okey = [&](uint32_t k) -> EventKey {
    return k < on ? EventKey{o.ex[ins][k], o.lt[ins][k]} : EventKey{};
  };
  auto ckey = [&](uint32_t k) -> EventKey {
    return k < cn ? EventKey{c.ex[ins][k], c.lt[ins][k]} : EventKey{};
  };
  auto tkey = [&](uint32_t k) -> EventKey {
    return k < tn ? EventKey{t.ex[ins][k], t.lt[ins][k]} : EventKey{};
  };

  while (oi < on || ci < cn || ti < tn) {
    EventKey key = okey(oi);
    const EventKey ck = ckey(ci);
    const EventKey tk = tkey(ti);
    if (key_less(ck, key)) key = ck;
    if (key_less(tk, key)) key = tk;

    uint32_t oe = oi, ce = ci, te = ti;
    while (oe < on && key_equal(okey(oe), key)) ++oe;
    while (ce < cn && key_equal(ckey(ce), key)) ++ce;
    while (te < tn && key_equal(tkey(te), key)) ++te;

    const bool invalid_key =
        key.ex == std::numeric_limits<int64_t>::max() ||
        (have_previous_key && key_less(key, previous_key));
    const bool future_inner = !invalid_key && key.ex > outer_ex;
    if (invalid_key || future_inner) {
      mark_dirty(ins);
      ++n_invalid_message_;
      if (future_inner) ++n_future_inner_groups_;
      oi = oe;
      ci = ce;
      ti = te;
      continue;
    }
    previous_key = key;
    have_previous_key = true;

    group_order_ids.clear();
    group_order_ids.reserve(static_cast<std::size_t>(oe - oi));
    for (uint32_t k = oi; k < oe; ++k) {
      if (o.oid[ins][k] != 0) group_order_ids.push_back(o.oid[ins][k]);
    }

    bool unseen_active_trade = false;
    for (uint32_t k = ti; k < te; ++k) {
      const Side side = t.side[ins][k];
      uint64_t active_id = 0;
      if (side == Side::TRADED_BID || side == Side::BID)
        active_id = t.bid[ins][k];
      else if (side == Side::TRADED_ASK || side == Side::ASK)
        active_id = t.ask[ins][k];
      else
        unseen_active_trade = true;

      if (active_id == 0 ||
          std::find(group_order_ids.begin(), group_order_ids.end(), active_id) ==
              group_order_ids.end())
        unseen_active_trade = true;
    }
    if (unseen_active_trade) {
      mark_dirty(ins);
      ++n_dirty_trade_groups_;
    }

    // Order/Cancel contain authoritative post-event BBO. For a dirty pre-state,
    // the first valid row only resynchronizes; handle_order naturally skips
    // classification because TopState::clean is false, then installs post BBO.
    for (uint32_t k = oi; k < oe; ++k) {
      handle_order(ins, o.ex[ins][k], o.px[ins][k], o.qty[ins][k],
                   o.side[ins][k], o.bp[ins][k], o.bs[ins][k],
                   o.ap[ins][k], o.as[ins][k]);
    }
    for (uint32_t k = ci; k < ce; ++k) {
      sync_top(ins, c.bp[ins][k], c.bs[ins][k],
               c.ap[ins][k], c.as[ins][k]);
    }

    oi = oe;
    ci = ce;
    ti = te;
  }
}

std::array<double, N_SERIES> Signal::aggregate(std::size_t ins) const
{
  std::array<double, N_SERIES> out;
  out.fill(kNaN);
  if (!day_valid_ || ins >= rolling_.size() || !instrument_valid_[ins])
    return out;

  const RollingState &r = rolling_[ins];
  out[ELIGIBLE_COUNT_BUY] = static_cast<double>(r.buy.eligible_count);
  out[ELIGIBLE_COUNT_SELL] = static_cast<double>(r.sell.eligible_count);
  out[JUMP_COUNT_BUY] = static_cast<double>(r.buy.jump_count);
  out[JUMP_COUNT_SELL] = static_cast<double>(r.sell.jump_count);

  const bool buy_ok =
      r.buy.eligible_count >= min_orders_ && r.buy.eligible_qty > 0;
  const bool sell_ok =
      r.sell.eligible_count >= min_orders_ && r.sell.eligible_qty > 0;

  if (buy_ok) {
    out[COUNT_RATE_BUY] =
        static_cast<double>(r.buy.jump_count) /
        static_cast<double>(r.buy.eligible_count);
    out[VOLUME_RATE_BUY] =
        static_cast<double>(r.buy.jump_qty) /
        static_cast<double>(r.buy.eligible_qty);
    out[URGENCY_BUY] =
        std::max(0.0, r.buy.urgency_num) /
        static_cast<double>(r.buy.eligible_qty);
  }
  if (sell_ok) {
    out[COUNT_RATE_SELL] =
        static_cast<double>(r.sell.jump_count) /
        static_cast<double>(r.sell.eligible_count);
    out[VOLUME_RATE_SELL] =
        static_cast<double>(r.sell.jump_qty) /
        static_cast<double>(r.sell.eligible_qty);
    out[URGENCY_SELL] =
        std::max(0.0, r.sell.urgency_num) /
        static_cast<double>(r.sell.eligible_qty);
  }
  if (buy_ok && sell_ok) {
    out[COUNT_RATE_ASYM] = out[COUNT_RATE_BUY] - out[COUNT_RATE_SELL];
    out[VOLUME_RATE_ASYM] = out[VOLUME_RATE_BUY] - out[VOLUME_RATE_SELL];
    out[URGENCY_ASYM] = out[URGENCY_BUY] - out[URGENCY_SELL];

    const double jb = static_cast<double>(r.buy.jump_count);
    const double js = static_cast<double>(r.sell.jump_count);
    const double join_b =
        static_cast<double>(r.buy.eligible_count - r.buy.jump_count);
    const double join_s =
        static_cast<double>(r.sell.eligible_count - r.sell.jump_count);
    out[LOG_ODDS_ASYM] =
        std::log((jb + 0.5) / (join_b + 0.5)) -
        std::log((js + 0.5) / (join_s + 0.5));
  }
  return out;
}

void Signal::emit(int64_t ex, uint64_t lt, bool valid)
{
  for (int s = 0; s < N_SERIES; ++s)
    std::fill(row_values_[s].begin(), row_values_[s].end(), kNaN);

  if (valid && day_valid_) {
    for (std::size_t i = 0; i < rolling_.size(); ++i) {
      const auto values = aggregate(i);
      for (int s = 0; s < N_SERIES; ++s) row_values_[s][i] = values[s];
    }
  }
  for (int s = 0; s < N_SERIES; ++s)
    append_row(buffers_[s], ex, lt, row_values_[s]);
}

void Signal::on_sod(const SodEvent *ev)
{
  date_ = ev->date;
  ins_nr_ = ev->ins_nr;
  day_valid_ = true;
  last_localtime_ = 0;
  last_outer_ex_ = 0;
  next_target_ = 0;

  n_orders_ = n_candidates_ = n_verified_ = n_jumps_ = 0;
  n_post_mismatch_ = n_dirty_trade_groups_ = n_invalid_bbo_ = 0;
  n_invalid_message_ = n_future_inner_groups_ = n_missed_targets_ = 0;

  symbol_header_ = "exchtime,localtime";
  tick_size_.assign(ev->ins_nr, kNaN);
  limit_up_tick_.assign(ev->ins_nr, 0);
  limit_down_tick_.assign(ev->ins_nr, 0);
  instrument_valid_.assign(ev->ins_nr, 1);
  top_.assign(ev->ins_nr, {});
  rolling_.assign(ev->ins_nr, {});

  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    const MdStatic *ms = ev->ms[i];
    symbol_header_.push_back(',');
    symbol_header_ += make_symbol(ms);
    if (!ms || !(std::isfinite(ms->tick_size) && ms->tick_size > 0.0)) {
      instrument_valid_[i] = 0;
      continue;
    }
    tick_size_[i] = ms->tick_size;
    int64_t tick = 0;
    if (price_to_tick(i, ms->limitup, tick)) limit_up_tick_[i] = tick;
    if (price_to_tick(i, ms->limitdown, tick)) limit_down_tick_[i] = tick;
  }

  for (int s = 0; s < N_SERIES; ++s) {
    buffers_[s] = symbol_header_ + "\n";
    row_values_[s].assign(ev->ins_nr, kNaN);
  }
  wllog_info("queue_jump: date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  if (!day_valid_) return;
  if (ev->ins_nr != ins_nr_) {
    wllog_error("queue_jump: MBO ins_nr={} differs from SOD ins_nr={} on {}\n",
                ev->ins_nr, ins_nr_, date_);
    day_valid_ = false;
    return;
  }

  last_localtime_ = ev->localtime;
  const int64_t outer_ex = ev->exchtime;

  // A missed/misaligned target is normally written as NaN before touching the
  // later callback. The sole exception is 15:00: cs-mbo may jump directly from
  // 14:59:57 to a post-close callback, while this factor stops accepting new
  // cohort events at 14:57. The previous state is therefore complete for the
  // close window and contains no information after the target.
  while (next_target_ < targets_.size() &&
         targets_[next_target_] < outer_ex) {
    const int64_t target = targets_[next_target_];
    const bool valid_close_target =
        day_valid_ && target == kDayClose &&
        last_outer_ex_ >= kCloseAuction && last_outer_ex_ < target;
    if (valid_close_target) {
      for (std::size_t i = 0; i < rolling_.size(); ++i)
        prune(i, tclock(target) - window_ns_);
      emit(target, last_localtime_, true);
    } else {
      emit(target, last_localtime_, false);
      ++n_missed_targets_;
    }
    ++next_target_;
  }
  last_outer_ex_ = std::max(last_outer_ex_, outer_ex);

  using O = CsMboEvent::OrderFldType;
  using C = CsMboEvent::CancelFldType;
  using T = CsMboEvent::TradeFldType;

  OrderCols o;
  CancelCols c;
  TradeCols t;
  if (ev->orders) {
    o.cnt = CsMboUtils::get_fld<O::Cnt>(ev);
    o.ex = CsMboUtils::get_fld<O::Exchtime>(ev);
    o.lt = CsMboUtils::get_fld<O::Localtime>(ev);
    o.px = CsMboUtils::get_fld<O::Price>(ev);
    o.qty = CsMboUtils::get_fld<O::Qty>(ev);
    o.side = CsMboUtils::get_fld<O::Side>(ev);
    o.oid = CsMboUtils::get_fld<O::OrderId>(ev);
    o.bp = CsMboUtils::get_fld<O::BidPrice>(ev);
    o.bs = CsMboUtils::get_fld<O::BidSize>(ev);
    o.ap = CsMboUtils::get_fld<O::AskPrice>(ev);
    o.as = CsMboUtils::get_fld<O::AskSize>(ev);
  }
  if (ev->cancels) {
    c.cnt = CsMboUtils::get_fld<C::Cnt>(ev);
    c.ex = CsMboUtils::get_fld<C::Exchtime>(ev);
    c.lt = CsMboUtils::get_fld<C::Localtime>(ev);
    c.bp = CsMboUtils::get_fld<C::BidPrice>(ev);
    c.bs = CsMboUtils::get_fld<C::BidSize>(ev);
    c.ap = CsMboUtils::get_fld<C::AskPrice>(ev);
    c.as = CsMboUtils::get_fld<C::AskSize>(ev);
  }
  if (ev->trades) {
    t.cnt = CsMboUtils::get_fld<T::Cnt>(ev);
    t.ex = CsMboUtils::get_fld<T::Exchtime>(ev);
    t.lt = CsMboUtils::get_fld<T::Localtime>(ev);
    t.side = CsMboUtils::get_fld<T::Side>(ev);
    t.bid = CsMboUtils::get_fld<T::BidId>(ev);
    t.ask = CsMboUtils::get_fld<T::AskId>(ev);
  }

  for (std::size_t i = 0; i < ev->ins_nr; ++i) {
    if (!instrument_valid_[i]) continue;
    process_instrument(i, o, c, t, outer_ex);
    prune(i, tclock(outer_ex) - window_ns_);
  }

  if (next_target_ < targets_.size() &&
      targets_[next_target_] == outer_ex) {
    emit(outer_ex, ev->localtime, true);
    ++next_target_;
  }
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
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    wllog_error("queue_jump: cannot create {}: {}\n", dir, ec.message());
    return;
  }
  const std::string path = dir + "/" + series + "-" + date_s + ".csv";
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) {
    wllog_error("queue_jump: cannot open {}\n", path);
    return;
  }
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  if (!out) wllog_error("queue_jump: failed writing {}\n", path);
}

void Signal::on_eod(const EodEvent *ev)
{
  while (next_target_ < targets_.size()) {
    const int64_t target = targets_[next_target_];

    // cs-mbo 日末通常停在 15:00 前最后一个 3 秒 callback，不会精确触发
    // 15:00。此时全天数据已经处理完，且 queue-jump 在 14:57 后不再纳入
    // 新事件，因此可以安全地按 15:00 cutoff 裁剪并输出最后一个窗口。
    const bool valid_close_target =
        day_valid_ && target == kDayClose &&
        last_outer_ex_ >= kCloseAuction && last_outer_ex_ <= target;
    if (valid_close_target) {
      for (std::size_t i = 0; i < rolling_.size(); ++i)
        prune(i, tclock(target) - window_ns_);
      emit(target, last_localtime_, true);
    } else {
      emit(target, last_localtime_, false);
      ++n_missed_targets_;
    }
    ++next_target_;
  }

  for (int s = 0; s < N_SERIES; ++s)
    flush_file(kSeriesName[s], buffers_[s], ev->date);

  wllog_info(
      "queue_jump: {} done rows={}, orders={}, candidates={}, verified={}, "
      "jumps={}, post_mismatch={}, dirty_trade_groups={}, invalid_bbo={}, "
      "invalid_message={}, future_inner_groups={}, missed_targets={}\n",
      ev->date, targets_.size(), n_orders_, n_candidates_, n_verified_,
      n_jumps_, n_post_mismatch_, n_dirty_trade_groups_, n_invalid_bbo_,
      n_invalid_message_, n_future_inner_groups_, n_missed_targets_);
}

std::string Signal::make_symbol(const MdStatic *ms)
{
  if (!ms) return "UNKNOWN";
  std::string code{ms->instrument};
  code.erase(std::find(code.begin(), code.end(), '\0'), code.end());
  std::string exchange{ms->exchange};
  exchange.erase(std::find(exchange.begin(), exchange.end(), '\0'),
                 exchange.end());
  return code + "." + exchange;
}

} // namespace queue_jump
} // namespace factors

using factors::queue_jump::Signal;

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

    .on_destroy = [](void *hdl) -> void
    { delete reinterpret_cast<Signal *>(hdl); },
};

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
