#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <cmath>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace multitickers {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_full_snapshot(const FullSnapshotEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day
  wllog_info("ins_nr={}\n", ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_full_snapshot(const FullSnapshotEvent *ev)
{
  ++m_cnt;
  const auto ss = ev->snapshot;
  wllog_info("{},{}/{},{}/{}, {},{},{},{} levels\n", m_cnt, ss->localtime,
             cfi::wolverine::time::epoch_to_str(ss->localtime), ss->exchtime,
             cfi::wolverine::time::exchtime_to_str(ss->exchtime),
             ss->last_price, ss->total_volume, ss->total_turnover,
             ss->level_nr);
  for (size_t i = 0; i < ss->level_nr; ++i) {
    const auto &lvl = ss->levels[i];
    wllog_info_cont("\t{},{}@{},{}%{}\n", i + 1, lvl.bv, lvl.bp, lvl.av,
                    lvl.ap);
  }
}

} // namespace multitickers
} // namespace nickchenyj

C_DECLARATION_BEGIN;

void on_create(void **ptr, SignalOps *ops)
{
  using nickchenyj::multitickers::Signal;
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

      .on_full_snapshot = [](void *hdl, const FullSnapshotEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_full_snapshot(ev);
      },

  };
}

C_DECLARATION_END;
