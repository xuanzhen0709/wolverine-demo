#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

#include <array>
#include <cmath>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace crossref {

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
  std::vector<double> sigval_;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) { sigval_.assign(2, NAN); }

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  if (ev->src_type == MdSrcType::Snapshot ||
      ev->src_type == MdSrcType::FullSnapshot) {
    // for snapshot/full_snapshot marketdata, only one symbol is provided at a
    // time
    const auto *ms = ev->ms[0];
    wllog_info("ins_nr:{},ticker:{},exch:{}\n", ev->ins_nr, ms->ticker,
               ms->exchange);
  } else {
    wllog_fatal("unsupported update type:{}\n", static_cast<int>(ev->src_type));
  }
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_snapshot(const SnapshotEvent *ev)
{
  ++cnt_;
  const auto *ss = ev->snapshot;
  // route the two legs of the pair by ticker: IF/IF1 (any IF* ticker) → slot 1,
  // the other leg (e.g. IC) → slot 0. Comparing the MdStatic pointer is
  // unreliable across on_sod / on_snapshot; the ticker is stable.
  const std::string ticker = ev->ms->ticker;
  if (ticker.rfind("IF", 0) == 0) {
    sigval_[1] = ss->ap[0];
  } else {
    sigval_[0] = ss->ap[0];
  }
  // update_signal(token, exchtime, localtime, ins_nr, sigs)
  apis_.update_signal(apis_.token, ss->exchtime, ss->localtime, 2,
                       sigval_.data());
}

} // namespace crossref
} // namespace nickchenyj

using nickchenyj::crossref::Signal;

C_DECLARATION_BEGIN;

static SignalOps my_ops = {
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

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
