#include <cstdio>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
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
  struct Stats {
    size_t cnt = 0;
    int64_t last = 0;

    void clear()
    {
      cnt = 0;
      last = 0;
    }
  };
  std::vector<double> sigs_;
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
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string ins{ms->instrument};
    ins.erase(std::find(ins.begin(), ins.end(), '\0'), ins.end());

    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    std::string symbol = ins + "." + exch;
  }
  sigs_.assign(ev->ins_nr, 0);
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
}

void Signal::on_cs_mbo(const CsMboEvent *ev)
{
wllog_info("exchtime:{}/{},localtime:{}/{}\n", ev->exchtime,
              time::exchtime_to_str(ev->exchtime), ev->localtime,
              time::epoch_to_str(ev->localtime));
  using TradeFldType = CsMboEvent::TradeFldType;

  //int64_t temp_exchtime;
  uint64_t temp_bidid;
  uint64_t temp_askid;
  double temp_price;
  int64_t temp_qty;

  const auto ins_nr = ev->ins_nr;
  {
    const auto *trade_cnt = CsMboUtils::get_fld<TradeFldType::Cnt>(ev);
    const auto *trade_price = CsMboUtils::get_fld<TradeFldType::Price>(ev);
    const auto *trade_qty = CsMboUtils::get_fld<TradeFldType::Qty>(ev);
    const auto *trade_bidid = CsMboUtils::get_fld<TradeFldType::BidId>(ev);
    const auto *trade_askid = CsMboUtils::get_fld<TradeFldType::AskId>(ev);

    for (int ins = 0; ins < ins_nr; ++ins) {
      const auto &cnt = trade_cnt[ins];
      const auto &price = trade_price[ins];
      const auto &qty = trade_qty[ins];
      const auto &bidid = trade_bidid[ins];
      const auto &askid = trade_askid[ins];
      for (int evidx = 0; evidx < cnt; ++evidx) {
	temp_bidid = bidid[evidx];
	temp_askid = askid[evidx];
	temp_price = price[evidx];
	temp_qty = qty[evidx];
        sigs_[ins] = double(temp_bidid) * temp_askid + temp_price * temp_qty;
      }
    }
  }
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs_.data());
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
