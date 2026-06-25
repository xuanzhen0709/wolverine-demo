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
namespace opsmbo {

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
  SignalApis apis_ = {nullptr};
  size_t cnt_ = 0;
  cfi::Mbo mbo1_;
  cfi::Mbo mbo2_;
  cfi::Mbo mbo3_;
  cfi::Mbo mbo4_;

  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> sigs;
  int mbo_cost = 0;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  mbo1_.initialize("root=sum_agg(@trade_price * @trade_qty * @trade_side)",
                    "m1");
  mbo2_.initialize("root=sum_agg(@trade_price*@trade_qty - "
                    "@trade_price*@trade_qty*@trade_side)",
                    "m2");
  mbo3_.initialize("root=sum_agg(@trade_side == 0)", "m3");
  mbo4_.initialize("root=sum_agg(@trade_side == 1)", "m4");

  x.resize(1000, 0);
  y.resize(1000, 0);
}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  wllog_info("ins_nr={}\n", ev->ins_nr);
  mbo1_.on_day_begin(ev->date, ev->ins_nr);
  mbo2_.on_day_begin(ev->date, ev->ins_nr);
  mbo3_.on_day_begin(ev->date, ev->ins_nr);
  mbo4_.on_day_begin(ev->date, ev->ins_nr);
  mbo_cost = 0;
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", cnt_);
  mbo1_.on_day_end(ev->date);
  mbo2_.on_day_end(ev->date);
  mbo3_.on_day_end(ev->date);
  mbo4_.on_day_end(ev->date);
  std::cout << "mbo_cost = " << mbo_cost / 1000 << " ms\n";
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  cnt_++;
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
      auto st = mbo1_.update(inputs, cnt, i);
      auto bt = mbo2_.update(inputs, cnt, i);

      inputs[0] = y.data();
      auto bc = mbo3_.update(inputs, cnt, i);
      auto sc = mbo4_.update(inputs, cnt, i);

      sigs.emplace_back(st + bt + bc + sc);

    } else {
      sigs.emplace_back(0);
    }
  }
  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  mbo_cost += elapsed.count();
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
}

} // namespace opsmbo
} // namespace nickchenyj

using nickchenyj::opsmbo::Signal;

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
