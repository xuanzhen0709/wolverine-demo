// stats/mid —— 全市场 mid price 标签生成器
//
// 在 8 个固定的日内时间点（09:30 / 10:00 / 10:30 / 11:00 / 13:00 / 13:30 /
// 14:00 / 14:30）各输出一次全市场股票的 mid price，供后续计算横截面因子的
// label（未来收益）使用。
//
// 数据源：cs-snapshot（横截面订单簿快照），只需最优一档（levels: 1）。
// mid = (最优买价 bp[0] + 最优卖价 ap[0]) / 2。

#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace cfi::wolverine;

namespace stats {
namespace mid {

// 默认 8 个截面时间点（HHMMSS，交易所时间）。
// exchtime 为“当日纳秒”，与 time::hhmmss_to_exchtime 的换算结果同一量纲，
// 因此可直接与 ev->exchtime 比较。
static constexpr int kDefaultLabelTimes[] = {
    93000,  100000, 103000, 110000,  // 上午 09:30 / 10:00 / 10:30 / 11:00
    130000, 133000, 140000, 143000}; // 下午 13:00 / 13:30 / 14:00 / 14:30

class Signal {
public:
  Signal() {}

  void initialize(const Config *root);
  void set_apis(SignalApis apis) { apis_ = apis; }
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

private:
  // 任一边缺失（涨/跌停单边报价、停牌 / 集合竞价前无报价）直接记为 NaN。
  SignalApis apis_ = {nullptr};

  std::vector<int64_t> targets_; // 已排序的目标 exchtime（当日纳秒）
  size_t next_ = 0;              // 下一个待输出的目标下标
};

void Signal::initialize(const Config *root)
{
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
}

void Signal::on_sod(const SodEvent *ev)
{
  next_ = 0;
}

void Signal::on_eod(const EodEvent *ev)
{
  if (next_ < targets_.size())
    wllog_warn("mid label: only {}/{} time points emitted today\n", next_,
               targets_.size());
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  if (next_ >= targets_.size()) return;
  if (ev->exchtime < targets_[next_]) return;

  using FldType = CsSnapshotEvent::FldType;
  const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev); // bp[level][ins]
  const auto *ap = CsSnapshotUtils::get_fld<FldType::ap>(ev); // ap[level][ins]
  constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> mid(ev->ins_nr, kNaN);
  for (std::uint16_t i = 0; i < ev->ins_nr; ++i) {
    const double b = bp[0][i];
    const double a = ap[0][i];
    if (b > 0.0 && a > 0.0) {
      mid[i] = (b + a) / 2;
    }
  }

  while (
    next_ < targets_.size() &&
    ev->exchtime >= targets_[next_]
  ) {
    apis_.update_signal(apis_.token, ev->exchtime, ev->localtime, ev->ins_nr, mid.data());
    ++next_;
  }
}

} // namespace mid
} // namespace stats

using stats::mid::Signal;

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
