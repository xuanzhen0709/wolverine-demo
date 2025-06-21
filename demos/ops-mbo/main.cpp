#include <cfi_ops/mbo.h>
#include <cfi_ops/types.h>
#include <cstdio>
#include <iostream>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

#include <chrono>
#include <cmath>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace csmbo {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_snapshot(const SnapshotEvent *ev);
  void on_bar(const BarEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

  void on_cs_mbo(const CsMboEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  cfi::Mbo m_mbo1;
  cfi::Mbo m_mbo2;
  cfi::Mbo m_mbo3;
  cfi::Mbo m_mbo4;

  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> sigs;
  int mbo_cost = 0;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  m_mbo1.initialize("root=sum_agg(@trade_price * @trade_qty * @trade_side)",
                    "m1");
  m_mbo2.initialize("root=sum_agg(@trade_price*@trade_qty - "
                    "@trade_price*@trade_qty*@trade_side)",
                    "m2");
  m_mbo3.initialize("root=sum_agg(@trade_side == 0)", "m3");
  m_mbo4.initialize("root=sum_agg(@trade_side == 1)", "m4");

  x.resize(1000, 0);
  y.resize(1000, 0);
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  wllog_info("ins_nr={}\n", ev->ins_nr);
  m_mbo1.on_day_begin(ev->date, ev->ins_nr);
  m_mbo2.on_day_begin(ev->date, ev->ins_nr);
  m_mbo3.on_day_begin(ev->date, ev->ins_nr);
  m_mbo4.on_day_begin(ev->date, ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", m_cnt);
  m_mbo1.on_day_end(ev->date);
  m_mbo2.on_day_end(ev->date);
  m_mbo3.on_day_end(ev->date);
  m_mbo4.on_day_end(ev->date);
  std::cout << "mbo_cost = " << mbo_cost / 1000 << " ms\n";
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  //   wllog_info("cs_snapshot,exchtime:{},localtime:{},ins_nr:{}\n",
  //   ev->exchtime,
  //  ev->localtime, ev->ins_nr);
  //   std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  //   m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime,
  //   ev->ins_nr,
  //    sigs.data());
}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  wllog_info("cs_mbo,exchtime:{},localtime:{},ins_nr:{}\n", ev->exchtime,
             ev->localtime, ev->ins_nr);
  m_cnt++;
  sigs.clear();
  sigs.reserve(ev->ins_nr);

  using TradeFldType = CsMboEvent::TradeFldType;

  const auto *trade_cnt = CsMboUtils::get_fld<TradeFldType::Cnt>(ev);
  const auto *trade_price = CsMboUtils::get_fld<TradeFldType::Price>(ev);
  const auto *trade_qty = CsMboUtils::get_fld<TradeFldType::Qty>(ev);
  const auto *trade_side = CsMboUtils::get_fld<TradeFldType::Side>(ev);

  std::vector<const double *> inputs{3, nullptr};
  auto start = std::chrono::steady_clock::now();
  for (uint16_t i = 0; i < ev->ins_nr; ++i) {
    const auto price = trade_price[i];
    const auto qty = trade_qty[i];
    const auto side = trade_side[i];
    int cnt = trade_cnt[i];

    if (cnt > 0) {

      if (cnt > 1000) {
        x.resize(cnt * 2);
        y.resize(cnt * 2);
      }

      std::copy_n(qty, cnt, x.data());
      std::transform(side, side + cnt, y.begin(), [](Side s)
                     { return static_cast<double>(static_cast<int>(s)); });

      inputs[0] = price;
      inputs[1] = x.data();
      inputs[2] = y.data();
      auto st = m_mbo1.update(inputs, cnt, i);
      auto bt = m_mbo2.update(inputs, cnt, i);

      inputs[0] = y.data();
      auto bc = m_mbo3.update(inputs, cnt, i);
      auto sc = m_mbo4.update(inputs, cnt, i);

      sigs.emplace_back(st + bt + bc + sc);

    } else {
      sigs.emplace_back(0);
    }
  }
  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Time: " << elapsed.count() << " us\n";
  mbo_cost += elapsed.count();
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
}

} // namespace csmbo
} // namespace nickchenyj

using nickchenyj::csmbo::Signal;

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

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_cs_mbo = [](void *hdl, const CsMboEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_mbo(ev);
    },
};

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
