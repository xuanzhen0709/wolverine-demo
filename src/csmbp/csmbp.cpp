#include <cstdio>
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
namespace csmbp {

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

  void on_cs_mbp(const CsMbpEvent *ev);

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
};

// function definitions

Signal::Signal() { on_sod(0, nullptr); }

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(uint32_t date, const SodEvent *ev) {
  m_cnt = 0;

  if (!ev) {
    return;
  }
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day
  LOG_INFO("ins_nr={}\n", ev->ins_nr);
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string ins{ms->instrument};
    ins.erase(std::find(ins.begin(), ins.end(), '\0'), ins.end());

    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    std::string symbol = ins + "." + exch;
  }
}

void Signal::on_eod(uint32_t date) { LOG_INFO("{} updates received\n", m_cnt); }

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {
  LOG_INFO("exchtime:{},ins_nr:{}\n", ev->exchtime, ev->ins_nr);
  // for (int i = 0; i < static_cast<int>(MdFld::_MAX); ++i) {
  //   const auto &fld = ev->flds[i];
  //   LOG_INFO_CONT("{},{}\n", i, fmt::ptr(fld.void_ptr));
  // }
  ++m_cnt;
  // we use m_cnt as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
}

void Signal::on_cs_mbp(const CsMbpEvent *ev) {
  LOG_INFO("exchtime:{},localtime:{},ins:{}\n", ev->exchtime, ev->localtime,
           ev->ins_nr);
  // for (size_t insidx = 0; insidx < ev->ins_nr; ++insidx) {
  //   const auto *ins_data = ev->ins_data[insidx];
  //   const auto *ms = ins_data->ms;
  //   if (std::string(ms->instrument, 6) != "300342") {
  //     continue;
  //   }
  //   LOG_INFO_CONT("\t{}/{},{},{}\n", insidx + 1, ev->ins_nr, ms->instrument,
  //                 ins_data->msg_nr);
  //   for (size_t msgidx = 0; msgidx < ins_data->msg_nr; ++msgidx) {
  //     const auto *msg = ins_data->msgs[msgidx];
  //     LOG_INFO_CONT("\t\t{}/{},{}/{}\n", msgidx + 1, ins_data->msg_nr,
  //                   fmt::ptr(msg), fmt::ptr(msg->cancel));
  //     switch (msg->type) {
  //     case CsMbpEvent::MsgType::NewOrder: {
  //       const auto *order = msg->order;
  //       LOG_INFO_CONT("\t\t\t{}/{},order,{},{},{},{}\n", msgidx + 1,
  //                     ins_data->msg_nr, order->chan_id, order->seq_num,
  //                     order->exchtime, order->localtime);
  //       LOG_INFO_CONT("\t\t\t{},{}@{},{},{}\n", order->oid, order->qty,
  //                     order->price, int(order->side), int(order->type));
  //     } break;
  //     case CsMbpEvent::MsgType::CancelOrder: {
  //       const auto *cancel = msg->cancel;
  //       LOG_INFO_CONT("\t\t\t{}/{},cancel,{},{},{},{}\n", msgidx + 1,
  //                     ins_data->msg_nr, cancel->chan_id, cancel->seq_num,
  //                     cancel->exchtime, cancel->localtime);
  //       LOG_INFO_CONT("\t\t\t{},{}@{},{},{}\n", cancel->oid, cancel->qty,
  //                     cancel->price, int(cancel->side), int(cancel->type));
  //     } break;
  //     case CsMbpEvent::MsgType::Trade: {
  //       const auto *trade = msg->trade;
  //       LOG_INFO_CONT("\t\t\t{}/{},trade,{},{},{},{}\n", msgidx + 1,
  //                     ins_data->msg_nr, trade->chan_id, trade->seq_num,
  //                     trade->exchtime, trade->localtime);
  //       LOG_INFO_CONT("\t\t\t{}/{},{}@{},{}\n", trade->bid_oid,
  //       trade->ask_oid,
  //                     trade->qty, trade->price, int(trade->side));

  //     } break;
  //     case CsMbpEvent::MsgType::Mbp: {
  //       const auto *mbp = msg->mbp;
  //       LOG_INFO_CONT("\t\t\t{}/{},mbp,{},{},{},{}\n", msgidx + 1,
  //                     ins_data->msg_nr, mbp->chan_id, mbp->seq_num,
  //                     mbp->exchtime, mbp->localtime);
  //       LOG_INFO_CONT("\t\t\t{}@{},{}@{}\n", mbp->bid_qty[0], mbp->bid_px[0],
  //                     mbp->ask_qty[0], mbp->ask_px[0]);

  //     } break;
  //     }
  //   }
  // }
}

} // namespace csmbp
} // namespace nickchenyj

using namespace nickchenyj::csmbp;

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

    .on_cs_snapshot = [](void *hdl,
                         const cfi::wolverine::CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_cs_mbp = [](void *hdl, const cfi::wolverine::CsMbpEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_mbp(ev);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
