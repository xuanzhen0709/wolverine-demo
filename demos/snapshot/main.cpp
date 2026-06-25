#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <array>
#include <cmath>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace snapshot {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_snapshot(const SnapshotEvent *ev);

private:
  SignalApis apis_ = {nullptr};
  size_t cnt_ = 0;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day
  wllog_info("ins_nr={}\n", ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_snapshot(const SnapshotEvent *ev)
{
  ++cnt_;
  const auto ss = ev->snapshot;
  wllog_info("{},{}/{},{}/{}, {},{},{},{}@{},{}@{},{},{}\n", cnt_,
             ss->localtime, cfi::wolverine::time::epoch_to_str(ss->localtime),
             ss->exchtime, cfi::wolverine::time::exchtime_to_str(ss->exchtime),
             ss->last_price, ss->total_volume, ss->total_turnover, ss->bv[0],
             ss->bp[0], ss->av[0], ss->ap[0], ss->total_bid_vol,
             ss->total_ask_vol);
}

} // namespace snapshot
} // namespace nickchenyj

C_DECLARATION_BEGIN;

void on_create(void **ptr, SignalOps *ops)
{
  using nickchenyj::snapshot::Signal;
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

      .on_snapshot = [](void *hdl, const SnapshotEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_snapshot(ev);
      },

  };
}

C_DECLARATION_END;
