#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

using namespace cfi::wolverine;

namespace yfchang {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_tx_snapshot(const TxSnapshotEvent *ev);

private:
  SignalApis apis_ = {nullptr};
  size_t sampling_idx_ = 0;
  size_t ev_cnt_ = 0;
  size_t smp_cnt_ = 0;
  std::vector<double> cross_sums_;
  int64_t last_exchtime_ = -1;
  uint64_t last_localtime_ = -1;

  class NeumaierAccumulator {
  private:
      double sum_;
      double c_;

  public:
      NeumaierAccumulator() : sum_(0.0), c_(0.0) {}

      void add(double value) {
          double t = sum_ + value;
          if (std::abs(sum_) >= std::abs(value)) {
              c_ += (sum_ - t) + value;  // 小数被大数吞没时的补偿
          } else {
              c_ += (value - t) + sum_;  // 大数被小数吞没时的补偿
          }
          sum_ = t;
      }

      double get_sum() const {
          return sum_ + c_;
      }

      void reset() {
          sum_ = 0.0;
          c_ = 0.0;
      }
  };
  std::vector<NeumaierAccumulator> acc_cross_sums_;

};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  std::vector<std::string> children;
  apis_.get_factor_list(apis_.token, &children);

  const auto n = children.size();
  cross_sums_.resize(n * (n + 1) / 2);
  acc_cross_sums_.resize(n * (n + 1) / 2);
  for (uint i = 0; i < acc_cross_sums_.size(); i++) {
    acc_cross_sums_[i] = NeumaierAccumulator();
  }

  wllog_info("using sampling {}\n", children[0]);
}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  std::fill(cross_sums_.begin(), cross_sums_.end(), 0.0);
  for (uint i = 0; i < acc_cross_sums_.size(); i++) {
    acc_cross_sums_[i].reset();
  }
  ev_cnt_ = 0;
  smp_cnt_ = 0;
  last_exchtime_ = -1;
  last_localtime_ = -1;
}

void Signal::on_eod(const EodEvent *ev)
{ 
  for (uint i = 0; i < acc_cross_sums_.size(); i++) {
    cross_sums_[i] = acc_cross_sums_[i].get_sum();
  }
  apis_.update_signal(apis_.token, last_exchtime_, last_localtime_,
                      cross_sums_.size(), cross_sums_.data());
}

void Signal::on_tx_snapshot(const TxSnapshotEvent *ev)
{
  ++ev_cnt_;
  int ins_nr, factor_nr;
  const auto &vals = apis_.get_factors(apis_.token, &ins_nr, &factor_nr);
  // if (unlikely(ins_nr != 1)) {
  //   wllog_fatal("cannot handle ins_nr:{}\n", ins_nr);
  // }
  bool pass_nan_filter = true;
  for (int i = 0; i < factor_nr; ++i) {
    if (std::isnan(vals[i])) {
      pass_nan_filter = false;
      break;
    }
  }

  if (vals[sampling_idx_] && pass_nan_filter) {
    smp_cnt_++;
    int idx = 0;
    for (int i = 0; i < factor_nr; ++i) {
      for (int j = 0; j <= i; ++j) {
        const auto cs = vals[i] * vals[j];
        if (!std::isnan(cs)) [[likely]] {
          acc_cross_sums_[idx++].add(cs);
        }
      }
    }
    last_exchtime_ = ev->snapshot->exchtime;
    last_localtime_ = ev->snapshot->localtime;
  }

  if (ev_cnt_ % 10000000 == 0) {
    wllog_info("processed ev:{},sample:{}\n", ev_cnt_, acc_cross_sums_[0].get_sum());
  }
}

} // namespace yfchang

C_DECLARATION_BEGIN;

void on_create(void **ptr, SignalOps *ops)
{
  using yfchang::Signal;
  *ptr = new Signal{};
  *ops = SignalOps{
      .initialize = [](void *hdl, const Config *root) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->initialize(root);
      },

      .set_apis = [](void *hdl, SignalApis apis) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->set_apis(apis);
      },

      .on_sod = [](void *hdl, const SodEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_sod(ev);
      },

      .on_eod = [](void *hdl, const EodEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_eod(ev);
      },

      .on_tx_snapshot = [](void *hdl, const TxSnapshotEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_tx_snapshot(ev);
      },

  };
}

C_DECLARATION_END;
