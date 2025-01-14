#include <cfi_ops/operators.h>
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
  // initialize with formula directory
  cfi::Operators op1{"ts_inner_product(@x,@y,2)"};
  // initialize with formula later
  cfi::Operators op2;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  // assume we get op config here
  const size_t len = 10;
  std::string formula;
  fmt::format_to(std::back_inserter(formula), "ts_inner_product(@x,@y,{})", len);
  op2.initialize(formula);
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day
  wllog_info("ins_nr={}\n", ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev) { wllog_info("{} updates received\n", m_cnt); }

void Signal::on_snapshot(const SnapshotEvent *ev)
{
  ++m_cnt;
  const auto ss = ev->snapshot;
  const auto ret1 = op1.update(double(ss->bv[0]), ss->bp[0]);
  const auto ret2 = op2.update(double(ss->av[0]), ss->ap[0]);
  wllog_info("{},{}/{},{}/{},bid:{}@{},bid_op:{}/{},ask:{}@{},ask_op:{}/{}\n", m_cnt, ss->localtime,
             cfi::wolverine::time::epoch_to_str(ss->localtime), ss->exchtime,
             cfi::wolverine::time::exchtime_to_str(ss->exchtime), ss->bv[0], ss->bp[0], op1.size(), ret1, ss->av[0],
             ss->ap[0], op2.size(), ret2);
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

      .on_snapshot = [](void *hdl, const SnapshotEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_snapshot(ev);
      },

  };
}

C_DECLARATION_END;
