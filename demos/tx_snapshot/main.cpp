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

  void on_tx_snapshot(const TxSnapshotEvent *ev);

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

void Signal::on_tx_snapshot(const TxSnapshotEvent *ev)
{
  using tx_snapshot_t = cfi::wolverine::tx_snapshot_t;
  ++m_cnt;
  // type tx_snapshot_t*
  const auto *ss = ev->snapshot;
  wllog_info(
      "{},{}/{},{}/"
      "{},lp:{},vol:{},tnvr:{},bvol:{},avol:{},flag:{},lvl_nr:{},tx_nr:{}\n",
      m_cnt, ss->localtime, cfi::wolverine::time::epoch_to_str(ss->localtime),
      ss->exchtime, cfi::wolverine::time::epoch_to_str(ss->exchtime),
      ss->last_price, ss->total_volume, ss->total_turnover, ss->total_bid_vol,
      ss->total_ask_vol, ss->flag, ss->level_nr, ss->tx_nr);
  for (int i = 0; i < ss->level_nr; ++i) {
    wllog_info("\tlevel:{},ap:{},av:{},ac:{},bp:{},bv:{},bc:{}\n", i, ss->ap[i],
               ss->av[i], ss->ac[i], ss->bp[i], ss->bv[i], ss->bc[i]);
  }
  for (int i = 0; i < ss->tx_nr; ++i) {
    const auto &tx = ss->txs[i];
    wllog_info("\tevent:{},localts:{},exchts:{},tx_ts:{}\n", i, tx.localtime,
               tx.sending_ts, tx.tx_ts);
    switch (tx.type) {
    case tx_snapshot_t::tx_type_e::TRADE: {
      const auto &trade = tx.trade;
      wllog_info(
          "\t\t{},id:{},side:{},price:{},qty:{},flag:{},bidid:{},askid:{}\n",
          tx.type, trade.id, trade.side, trade.price, trade.qty, trade.flag,
          trade.bid_id, trade.ask_id);
    } break;
    case tx_snapshot_t::tx_type_e::NEW: {
      const auto &add = tx.new_order;
      wllog_info("\t\t{},id:{},side:{},price:{},qty:{}\n", tx.type, add.id,
                 add.side, add.price, add.qty);
    } break;
    case tx_snapshot_t::tx_type_e::MODIFY: {
      const auto &modify = tx.modify_order;
      wllog_info("\t\t{},id:{},side:{},price:{},qty:{}\n", tx.type, modify.id,
                 modify.side, modify.price, modify.qty);
    } break;
    case tx_snapshot_t::tx_type_e::CANCEL: {
      const auto &cancel = tx.cancel_order;
      wllog_info("\t\t{},id:{},side:{},price:{}\n", tx.type, cancel.id,
                 cancel.side, cancel.price);
    } break;
    }
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

      .on_tx_snapshot = [](void *hdl, const TxSnapshotEvent *ev) -> void
      {
        auto *ptr = reinterpret_cast<Signal *>(hdl);
        ptr->on_tx_snapshot(ev);
      },

  };
}

C_DECLARATION_END;
