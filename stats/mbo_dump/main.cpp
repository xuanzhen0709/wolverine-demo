// stats/mbo_dump —— 逐笔 MBO 数据 dump 工具
//
// 订阅 cs-mbo，把某一天的逐笔 Order / Cancel / Trade 三流按原样写成 CSV，
// 便于人工检查数据。**不走 update_signal 框架**：自己开一个内存 buffer 累积，
// 超阈值就刷到 ofstream，on_eod 收尾。
//
// 输出：<output_dir>/<date>/mbo-<date>.csv，统一 17 列（见 kHeader）。
//   - Order/Cancel 行：order_id + bid/ask 盘口一档快照有值，bid_id/ask_id 空
//   - Trade 行：bid_id/ask_id 有值，order_id 与盘口列空
//
// 注意：逐笔的某个流（ev->orders/cancels/trades）或某个字段（flds[fld]）都可能
// 未被数据源填充而为 null——本工具对每个字段单独判空，缺失字段输出空列。
//
// 可选配置（signal.config）：
//   output_dir   : 输出根目录，默认 output/mbo_dump
//   symbols      : 只 dump 这些标的（"600000" 或 "600000.SH" 均可匹配）；
//                  留空 = 全市场（⚠ 单日全市场逐笔可能数十 GB，建议先填几只）
//   streams      : 只 dump 指定流，取值 order/cancel/trade 的子集；默认三者全开
//   start_hhmmss : 只保留 exchtime >= 该时点的逐笔（HHMMSS 整数），默认不限
//   end_hhmmss   : 只保留 exchtime <= 该时点的逐笔，默认不限
//   human_time   : 是否输出可读的 exchtime_str 列，默认 true
//   flush_bytes  : buffer 刷盘阈值（字节），默认 8MB

#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/format.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/marketdata/types.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace cfi::wolverine;

namespace stats {
namespace mbo_dump {

// 统一列头（17 列）。Order/Cancel 与 Trade 共用，无对应字段的位置留空。
static constexpr const char *kHeader =
    "msg,symbol,ins_idx,exchtime,exchtime_str,localtime,price,qty,side,"
    "order_id,bid_id,ask_id,bid_price,bid_size,ask_price,ask_size,"
    "trading_phase\n";

static const char *side_str(Side s)
{
  switch (s) {
  case Side::BID: return "B";          // 买 / bid（BUY 同值别名）
  case Side::ASK: return "S";          // 卖 / ask（SELL 同值别名）
  case Side::TRADED_BID: return "TB";  // 成交-主动买
  case Side::TRADED_ASK: return "TA";  // 成交-主动卖
  case Side::NO_SIDE: return "";
  default: return "U";                 // UNKNOWN 等
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
  void flush_buf();
  bool in_window(int64_t ex) const { return ex >= start_ns_ && ex <= end_ns_; }

  // 逐格写入 buffer 的 helper：字段指针为 null 时写空列。每个都以逗号结尾，
  // 一行写完后把末尾逗号替换为换行。
  void put_sep() { buf_.push_back(','); }
  void put_raw(std::string_view s)
  {
    buf_.append(s.data(), s.size());
    buf_.push_back(',');
  }
  void put_idx(int v)
  {
    fmt::format_to(std::back_inserter(buf_), "{}", v);
    buf_.push_back(',');
  }
  void put_dbl(const double *const *f, int i, int k)
  {
    if (f) fmt::format_to(std::back_inserter(buf_), "{}", f[i][k]);
    buf_.push_back(',');
  }
  void put_i64(const int64_t *const *f, int i, int k)
  {
    if (f) fmt::format_to(std::back_inserter(buf_), "{}", f[i][k]);
    buf_.push_back(',');
  }
  void put_u64(const uint64_t *const *f, int i, int k)
  {
    if (f) fmt::format_to(std::back_inserter(buf_), "{}", f[i][k]);
    buf_.push_back(',');
  }
  void put_u8(const uint8_t *const *f, int i, int k)
  {
    if (f) fmt::format_to(std::back_inserter(buf_), "{}", static_cast<int>(f[i][k]));
    buf_.push_back(',');
  }
  void put_side(const Side *const *f, int i, int k)
  {
    if (f) buf_ += side_str(f[i][k]);
    buf_.push_back(',');
  }
  void put_time(const int64_t *const *f, int i, int k)
  {
    if (f && human_time_) buf_ += time::exchtime_to_str(f[i][k]);
    buf_.push_back(',');
  }
  void end_row() { buf_.back() = '\n'; } // 把最后一个逗号改成换行

  SignalApis apis_ = {nullptr};

  // 配置
  std::string output_dir_ = "output/mbo_dump";
  std::unordered_set<std::string> allowed_; // 空 = 全市场
  bool dump_order_ = true, dump_cancel_ = true, dump_trade_ = true;
  bool human_time_ = true;
  int64_t start_ns_ = std::numeric_limits<int64_t>::min();
  int64_t end_ns_ = std::numeric_limits<int64_t>::max();
  size_t flush_bytes_ = 8u << 20; // 8MB

  // 每日状态
  std::ofstream out_;
  std::string buf_;
  std::vector<char> keep_;           // keep_[ins]：该标的是否 dump
  std::vector<std::string> symbols_; // symbols_[ins]："code.exch"
  size_t n_order_ = 0, n_cancel_ = 0, n_trade_ = 0;
};

void Signal::initialize(const Config *root)
{
  output_dir_ = (*root)["output_dir"].as<std::string>(output_dir_);
  human_time_ = (*root)["human_time"].as<bool>(human_time_);
  flush_bytes_ = static_cast<size_t>(
      (*root)["flush_bytes"].as<int64_t>(static_cast<int64_t>(flush_bytes_)));

  if (auto node = (*root)["symbols"]) {
    for (const auto &s : node.as<std::vector<std::string>>()) allowed_.insert(s);
  }
  if (auto node = (*root)["streams"]) {
    auto v = node.as<std::vector<std::string>>();
    auto has = [&](const char *k) {
      return std::find(v.begin(), v.end(), k) != v.end();
    };
    dump_order_ = has("order");
    dump_cancel_ = has("cancel");
    dump_trade_ = has("trade");
  }
  if (auto node = (*root)["start_hhmmss"])
    start_ns_ = time::hhmmss_to_exchtime(node.as<int>());
  if (auto node = (*root)["end_hhmmss"])
    end_ns_ = time::hhmmss_to_exchtime(node.as<int>());

  buf_.reserve(flush_bytes_ + (1u << 20));
}

void Signal::on_sod(const SodEvent *ev)
{
  // 新的一天：收尾上一天（正常情况下 on_eod 已处理，这里是安全兜底）。
  if (out_.is_open()) {
    flush_buf();
    out_.close();
  }
  buf_.clear();
  n_order_ = n_cancel_ = n_trade_ = 0;

  // 建立每标的的 symbol 字符串与 keep 标记（顺序即列式数组下标）。
  keep_.assign(ev->ins_nr, 0);
  symbols_.assign(ev->ins_nr, {});
  size_t n_keep = 0;
  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string code{ms->instrument};
    code.erase(std::find(code.begin(), code.end(), '\0'), code.end());
    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    symbols_[i] = code + "." + exch;
    // 用户可写 "600000" 或 "600000.SH"，两种都匹配。
    bool keep =
        allowed_.empty() || allowed_.count(symbols_[i]) || allowed_.count(code);
    keep_[i] = keep ? 1 : 0;
    n_keep += keep ? 1 : 0;
  }

  // 打开当日输出文件。
  const std::string date = fmt::format("{}", ev->date);
  const std::string dir = output_dir_ + "/" + date;
  std::error_code fec;
  std::filesystem::create_directories(dir, fec);
  if (fec) {
    wllog_error("mbo_dump: cannot create dir {}: {}\n", dir, fec.message());
    return;
  }
  const std::string path = dir + "/mbo-" + date + ".csv";
  out_.open(path, std::ios::out | std::ios::trunc);
  if (!out_) {
    wllog_error("mbo_dump: cannot open {}\n", path);
    return;
  }
  out_ << kHeader;
  wllog_info("mbo_dump: writing {} ({}/{} instruments)\n", path, n_keep,
             ev->ins_nr);
}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  if (!out_.is_open()) return;
  using O = CsMboEvent::OrderFldType;
  using C = CsMboEvent::CancelFldType;
  using T = CsMboEvent::TradeFldType;
  const auto ins_nr = ev->ins_nr;

  // 每个流、每个字段都可能为 null（数据源未填充）——逐格判空。
  if (dump_order_ && ev->orders) {
    const auto *cnt = CsMboUtils::get_fld<O::Cnt>(ev);
    if (cnt) {
      const auto *ext = CsMboUtils::get_fld<O::Exchtime>(ev);
      const auto *lt = CsMboUtils::get_fld<O::Localtime>(ev);
      const auto *px = CsMboUtils::get_fld<O::Price>(ev);
      const auto *qty = CsMboUtils::get_fld<O::Qty>(ev);
      const auto *sd = CsMboUtils::get_fld<O::Side>(ev);
      const auto *oid = CsMboUtils::get_fld<O::OrderId>(ev);
      const auto *bp = CsMboUtils::get_fld<O::BidPrice>(ev);
      const auto *bs = CsMboUtils::get_fld<O::BidSize>(ev);
      const auto *ap = CsMboUtils::get_fld<O::AskPrice>(ev);
      const auto *as = CsMboUtils::get_fld<O::AskSize>(ev);
      const auto *ph = CsMboUtils::get_fld<O::TradingPhase>(ev);
      for (int i = 0; i < ins_nr; ++i) {
        if (i >= static_cast<int>(keep_.size()) || !keep_[i]) continue;
        for (std::uint32_t k = 0; k < cnt[i]; ++k) {
          if (ext && !in_window(ext[i][k])) continue;
          buf_.push_back('O');
          put_sep();
          put_raw(symbols_[i]);
          put_idx(i);
          put_i64(ext, i, k);
          put_time(ext, i, k);
          put_u64(lt, i, k);
          put_dbl(px, i, k);
          put_i64(qty, i, k);
          put_side(sd, i, k);
          put_u64(oid, i, k);
          put_sep(); // bid_id
          put_sep(); // ask_id
          put_dbl(bp, i, k);
          put_i64(bs, i, k);
          put_dbl(ap, i, k);
          put_i64(as, i, k);
          put_u8(ph, i, k);
          end_row();
          ++n_order_;
        }
      }
    }
  }

  if (dump_cancel_ && ev->cancels) {
    const auto *cnt = CsMboUtils::get_fld<C::Cnt>(ev);
    if (cnt) {
      const auto *ext = CsMboUtils::get_fld<C::Exchtime>(ev);
      const auto *lt = CsMboUtils::get_fld<C::Localtime>(ev);
      const auto *px = CsMboUtils::get_fld<C::Price>(ev);
      const auto *qty = CsMboUtils::get_fld<C::Qty>(ev);
      const auto *sd = CsMboUtils::get_fld<C::Side>(ev);
      const auto *oid = CsMboUtils::get_fld<C::OrderId>(ev);
      const auto *bp = CsMboUtils::get_fld<C::BidPrice>(ev);
      const auto *bs = CsMboUtils::get_fld<C::BidSize>(ev);
      const auto *ap = CsMboUtils::get_fld<C::AskPrice>(ev);
      const auto *as = CsMboUtils::get_fld<C::AskSize>(ev);
      const auto *ph = CsMboUtils::get_fld<C::TradingPhase>(ev);
      for (int i = 0; i < ins_nr; ++i) {
        if (i >= static_cast<int>(keep_.size()) || !keep_[i]) continue;
        for (std::uint32_t k = 0; k < cnt[i]; ++k) {
          if (ext && !in_window(ext[i][k])) continue;
          buf_.push_back('C');
          put_sep();
          put_raw(symbols_[i]);
          put_idx(i);
          put_i64(ext, i, k);
          put_time(ext, i, k);
          put_u64(lt, i, k);
          put_dbl(px, i, k);
          put_i64(qty, i, k);
          put_side(sd, i, k);
          put_u64(oid, i, k);
          put_sep(); // bid_id
          put_sep(); // ask_id
          put_dbl(bp, i, k);
          put_i64(bs, i, k);
          put_dbl(ap, i, k);
          put_i64(as, i, k);
          put_u8(ph, i, k);
          end_row();
          ++n_cancel_;
        }
      }
    }
  }

  if (dump_trade_ && ev->trades) {
    const auto *cnt = CsMboUtils::get_fld<T::Cnt>(ev);
    if (cnt) {
      const auto *ext = CsMboUtils::get_fld<T::Exchtime>(ev);
      const auto *lt = CsMboUtils::get_fld<T::Localtime>(ev);
      const auto *px = CsMboUtils::get_fld<T::Price>(ev);
      const auto *qty = CsMboUtils::get_fld<T::Qty>(ev);
      const auto *sd = CsMboUtils::get_fld<T::Side>(ev);
      const auto *bid = CsMboUtils::get_fld<T::BidId>(ev);
      const auto *ask = CsMboUtils::get_fld<T::AskId>(ev);
      const auto *ph = CsMboUtils::get_fld<T::TradingPhase>(ev);
      for (int i = 0; i < ins_nr; ++i) {
        if (i >= static_cast<int>(keep_.size()) || !keep_[i]) continue;
        for (std::uint32_t k = 0; k < cnt[i]; ++k) {
          if (ext && !in_window(ext[i][k])) continue;
          buf_.push_back('T');
          put_sep();
          put_raw(symbols_[i]);
          put_idx(i);
          put_i64(ext, i, k);
          put_time(ext, i, k);
          put_u64(lt, i, k);
          put_dbl(px, i, k);
          put_i64(qty, i, k);
          put_side(sd, i, k);
          put_sep(); // order_id
          put_u64(bid, i, k);
          put_u64(ask, i, k);
          put_sep(); // bid_price
          put_sep(); // bid_size
          put_sep(); // ask_price
          put_sep(); // ask_size
          put_u8(ph, i, k);
          end_row();
          ++n_trade_;
        }
      }
    }
  }

  if (buf_.size() >= flush_bytes_) flush_buf();
}

void Signal::on_eod(const EodEvent *ev)
{
  if (!out_.is_open()) return;
  flush_buf();
  out_.close();
  wllog_info("mbo_dump: {} done. order={} cancel={} trade={} (total {})\n",
             ev->date, n_order_, n_cancel_, n_trade_,
             n_order_ + n_cancel_ + n_trade_);
}

void Signal::flush_buf()
{
  if (buf_.empty()) return;
  out_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
  buf_.clear();
}

} // namespace mbo_dump
} // namespace stats

using stats::mbo_dump::Signal;

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
