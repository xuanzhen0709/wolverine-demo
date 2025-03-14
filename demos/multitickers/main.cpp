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
namespace multitickers {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_snapshot(const SnapshotEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  // assume we only want to do arb on two symbols
  const MdStatic *m_ms0{nullptr};
  const MdStatic *m_ms1{nullptr};
  std::vector<double> m_sigval;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) { m_sigval.assign(2, NAN); }

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  // NOTE:
  if (ev->src_type == MdSrcType::Snapshot ||
      ev->src_type == MdSrcType::FullSnapshot) {
    // for snapshot/full_snapshot marketdata, only one symbol is provided at a
    // time
    const auto *ms = ev->ms[0];
    const std::string ticker = ms->ticker;
    const std::string exch = ms->exchange;
    wllog_info("ins_nr:{},ticker:{},exch:{}\n", ev->ins_nr, ticker, exch);
    if (ticker == "BIANUM") {
      m_ms0 = ms;
    } else {
      m_ms1 = ms;
    }
  } else {
    wllog_fatal("unsupported update type:{}\n", static_cast<int>(ev->src_type));
  }
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_snapshot(const SnapshotEvent *ev)
{
  ++m_cnt;
  const auto *ss = ev->snapshot;
  if (ev->ms == m_ms0) {
    // tgt 0
    m_sigval[0] = ss->ap[0];
  } else {
    m_sigval[2] = ss->ap[0];
  }
  m_apis.update_signal(m_apis.token, ss->localtime, ss->exchtime, 2,
                       m_sigval.data());
}

} // namespace multitickers
} // namespace nickchenyj

using nickchenyj::multitickers::Signal;

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
