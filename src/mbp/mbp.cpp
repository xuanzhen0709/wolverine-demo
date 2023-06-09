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
namespace mbp {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(uint32_t date, const SodEvent *ev);
  void on_eod(uint32_t date);

  void on_snapshot(const SnapshotEvent *ev);
  void on_bar(const BarEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

  void on_new_order(const NewOrderEvent *ev);
  void on_cancel_order(const CancelOrderEvent *ev);
  void on_trade(const TradeEvent *ev);
  void on_mbp(const MbpEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  struct Stats {
    size_t cnt = 0;
    int64_t last = 0;

    void clear() {
      cnt = 0;
      last = 0;
    }
  };

  Stats m_new_order_stats = Stats();
  Stats m_cancel_order_stats = Stats();
  Stats m_trade_stats = Stats();
  Stats m_mbp_stats = Stats();
};

// function definitions

Signal::Signal() { on_sod(0, nullptr); }

void Signal::initialize(const Config *root) {
  // subscribe data
  {
    const auto &md_cfg = root->get_child("marketdata");
    const size_t md_nr = md_cfg.size();
    for (size_t md_idx = 0; md_idx < md_nr; ++md_idx) {
      const auto &this_cfg = md_cfg.get_child(md_idx);
      const auto type = this_cfg.get_value<std::string>("type");

      std::vector<std::string> fields;
      {
        const auto fields_cfg = this_cfg.get_child("fields");
        for (size_t fld_idx = 0; fld_idx < fields_cfg.size(); ++fld_idx) {
          fields.push_back(fields_cfg.get_value<std::string>(fld_idx));
        }
      }

      std::vector<std::string> symbols;
      {
        const auto symbols_cfg = this_cfg.get_child("symbols");
        for (size_t sym_idx = 0; sym_idx < symbols_cfg.size(); ++sym_idx) {
          symbols.push_back(symbols_cfg.get_value<std::string>(sym_idx));
        }
      }
      // NOTE:
      // support for "fields" is marketdata-module dependent
      // for cross-sectional data, an empty fields list indicates all fields.
      m_apis.subscribe(m_apis.token, type, fields, symbols);
    }
  }
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(uint32_t date, const SodEvent *ev) {
  m_cnt = 0;
  m_new_order_stats.clear();
  m_cancel_order_stats.clear();
  m_trade_stats.clear();
  m_mbp_stats.clear();

  if (!ev) {
    return;
  }
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day call set_targets() everyday
  std::vector<std::string> targets;
  LOG_INFO("ins_nr={}\n", ev->ins_nr);
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string ins{ms->instrument};
    ins.erase(std::find(ins.begin(), ins.end(), '\0'), ins.end());

    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    std::string symbol = ins + "." + exch;
    targets.emplace_back(symbol);
  }
  // set trading targets
  m_apis.set_targets(m_apis.token, targets);
}

void Signal::on_eod(uint32_t date) { LOG_INFO("{} updates received\n", m_cnt); }

void Signal::on_snapshot(const SnapshotEvent *ev) {
  // market data update
  const auto *ms = ev->ms;
  const auto *ss = ev->snapshot;
  LOG_INFO("{},{},{},{}\n", static_cast<int>(ss->md_type), ss->exchtime,
           ss->localtime, ms->instrument);
  // if (ss->md_type == MdType::Level1) {
  // } else if (ss->md_type == MdType::Level5) {
  // }
}

void Signal::on_bar(const BarEvent *ev) { LOG_INFO("callback received\n"); }

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {
  // LOG_INFO("exchtime:{},ins_nr:{}\n", ev->exchtime, ev->ins_nr);
  // for (int i = 0; i < static_cast<int>(MdFld::_MAX); ++i) {
  //   const auto &fld = ev->flds[i];
  //   LOG_INFO_CONT("{},{}\n", i, fmt::ptr(fld.void_ptr));
  // }
  LOG_INFO(
      "exchtime:{},ins_nr:{},new:{}/{},cancel:{}/{},trade:{}/{},mbp:{}/{}\n",
      ev->exchtime, ev->ins_nr, m_new_order_stats.cnt, m_new_order_stats.last,
      m_cancel_order_stats.cnt, m_cancel_order_stats.last, m_trade_stats.cnt,
      m_trade_stats.last, m_mbp_stats.cnt, m_mbp_stats.last);
  ++m_cnt;
  // we use m_cnt as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
  m_new_order_stats.clear();
  m_cancel_order_stats.clear();
  m_trade_stats.clear();
  m_mbp_stats.clear();
}

void Signal::on_new_order(const NewOrderEvent *ev) {
  const auto *ms = ev->ms;
  const auto *order = ev->order;
  // LOG_INFO("exchtime:{},localtime:{},ins:{}", order->exchtime,
  // order->localtime,
  //          ms->instrument);
  // LOG_INFO_CONT("\n\toid:{},{}@{},side:{},type:{}\n", order->oid, order->qty,
  //               order->price, int(order->side), int(order->type));
  m_new_order_stats.cnt += 1;
  m_new_order_stats.last = std::max(m_new_order_stats.last, order->exchtime);
}

void Signal::on_cancel_order(const CancelOrderEvent *ev) {
  const auto *ms = ev->ms;
  const auto *cancel = ev->cancel;
  // LOG_INFO("exchtime:{},localtime:{},ins:{}", order->exchtime,
  // order->localtime,
  //          ms->instrument);
  // LOG_INFO_CONT("\n\toid:{},{}@{},side:{},type:{}\n", order->oid, order->qty,
  //               order->price, int(order->side), int(order->type));

  m_cancel_order_stats.cnt += 1;
  m_cancel_order_stats.last =
      std::max(m_cancel_order_stats.last, cancel->exchtime);
}

void Signal::on_trade(const TradeEvent *ev) {
  const auto *ms = ev->ms;
  const auto *trade = ev->trade;
  // LOG_INFO("exchtime:{},localtime:{},ins:{}", trade->exchtime,
  // trade->localtime,
  //          ms->instrument);
  // LOG_INFO_CONT("\n\ttrade,{}@{},{}:{},side:{}\n", trade->qty, trade->price,
  //               trade->bid_oid, trade->ask_oid, int(trade->side));

  m_trade_stats.cnt += 1;
  m_trade_stats.last = std::max(m_trade_stats.last, trade->exchtime);
}

void Signal::on_mbp(const MbpEvent *ev) {
  const auto *ms = ev->ms;
  const auto *mbp = ev->mbp;

  // LOG_INFO("exchtime:{},localtime:{},ins:{}", mbp->exchtime, mbp->localtime,
  //          ms->instrument);
  // LOG_INFO_CONT("\n\tmbp,bid:{}@{},ask:{}@{}\n", mbp->bid_qty[0],
  //               mbp->bid_px[0], mbp->ask_qty[0], mbp->ask_px[0]);

  m_mbp_stats.cnt += 1;
  m_mbp_stats.last = std::max(m_mbp_stats.last, mbp->exchtime);
}

} // namespace mbp
} // namespace nickchenyj

using namespace nickchenyj::mbp;

C_DECLARATION_BEGIN;

static SignalOps my_ops = {
    .initialize = [](void *hdl, const cfi::wolverine::Config *root) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, cfi::wolverine::SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->set_apis(apis);
    },

    .on_sod = [](void *hdl, uint32_t date,
                 const cfi::wolverine::SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_sod(date, ev);
    },

    .on_eod = [](void *hdl, uint32_t date) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_eod(date);
    },
    .on_snapshot = [](void *hdl,
                      const cfi::wolverine::SnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_snapshot(ev);
    },

    .on_bar = [](void *hdl, const cfi::wolverine::BarEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_bar(ev);
    },

    .on_cs_snapshot = [](void *hdl,
                         const cfi::wolverine::CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_new_order = [](void *hdl,
                       const cfi::wolverine::NewOrderEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_new_order(ev);
    },

    .on_cancel_order = [](void *hdl,
                          const cfi::wolverine::CancelOrderEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cancel_order(ev);
    },

    .on_trade = [](void *hdl, const cfi::wolverine::TradeEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_trade(ev);
    },

    .on_mbp = [](void *hdl, const cfi::wolverine::MbpEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_mbp(ev);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
