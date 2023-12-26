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

    void clear() {
      cnt = 0;
      last = 0;
    }
  };
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev) {
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
}

void Signal::on_eod(const EodEvent *ev) {
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {
  // wllog_info("exchtime:{},ins_nr:{},level_nr:{}\n", ev->exchtime, ev->ins_nr,
  //          ev->level_nr);
  // to access level-based fields
  // const TYPE* arr = ev->flds[fld].xxx_ptrs[lvl];
  // where arr is a pointer to an array of length ins_nr

  // to access non-level-based fields
  // const TYPE* arr = ev->flds[fld].xxx_ptr;
  // where arr is a pointer to an array of length ins_nr

  // for (int lvl = 0; lvl < ev->level_nr; ++lvl) {
  //   const auto &ap_ptrs =
  //       ev->flds[static_cast<int>(CsSnapshotEvent::FldType::ap)].double_ptrs;
  //   const auto *data = ap_ptrs[lvl];
  //   wllog_info("lvl {}", lvl);
  //   for (int idx = 0; idx < 5; ++idx) {
  //     wllog_info_cont(",{}", data[idx]);
  //   }
  //   wllog_info_cont("\n");
  // }
  ++m_cnt;
  // we use m_cnt as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
}

void Signal::on_cs_mbo(const CsMboEvent *ev) {
  // wllog_info("exchtime:{},localtime:{},ins_nr:{}\n", ev->exchtime,
  // ev->localtime,
  //          ev->ins_nr);
  // {
  //   const auto &orders = ev->orders;
  //   for (int ins = 0; ins < 5; ++ins) {
  //     const auto &cnt = orders->cnt[ins];
  //     const auto &px = orders->price[ins];
  //     const auto &size = orders->qty[ins];
  //     wllog_info("ORDER,ins:{},cnt:{}\n", ins, cnt);
  //     for (int evidx = 0; evidx < cnt && evidx < 5; ++evidx) {
  //       wllog_info("\t{},{}@{}\n", evidx, size[evidx], px[evidx]);
  //     }
  //   }
  // }

  // {
  //   const auto &cancels = ev->cancels;
  //   for (int ins = 0; ins < 5; ++ins) {
  //     const auto &cnt = cancels->cnt[ins];
  //     const auto &px = cancels->price[ins];
  //     const auto &size = cancels->qty[ins];
  //     wllog_info("CANCELS,ins:{},cnt:{}\n", ins, cnt);
  //     for (int evidx = 0; evidx < cnt && evidx < 5; ++evidx) {
  //       wllog_info("\t{},{}@{}\n", evidx, size[evidx], px[evidx]);
  //     }
  //   }
  // }

  {
    const auto &trades = ev->trades;
    for (int ins = 0; ins < 5; ++ins) {
      const auto &cnt = trades->cnt[ins];
      const auto &bidid = trades->bidid[ins];
      const auto &askid = trades->askid[ins];
      wllog_info("TRADES,ins:{},cnt:{}\n", ins, cnt);
      for (int evidx = 0; evidx < cnt && evidx < 5; ++evidx) {
        wllog_info("\t{},{},{}\n", evidx, bidid[evidx], askid[evidx]);
      }
    }
  }
  {
    // a quick demo to calculate per-stock vwap
    const auto &trades = ev->cancels;
    for (int ins = 0; ins < ev->ins_nr; ++ins) {
      const auto &cnt = trades->cnt[ins];
      const auto &px = trades->price[ins];
      const auto &size = trades->qty[ins];

      uint32_t vol = 0;
      for (int evidx = 0; evidx < cnt; ++evidx) {
        vol += size[evidx];
      }

      double turnover = 0;
      for (int evidx = 0; evidx < cnt; ++evidx) {
        turnover += size[evidx] * px[evidx];
      }

      const double vwap = turnover / vol;
      // wllog_info("ins:{},turnover:{},volume:{},vwap:{}\n", ins, turnover,
      // vol,
      //          vwap);
    }
  }
}

} // namespace csmbo
} // namespace nickchenyj

using nickchenyj::csmbo::Signal;

C_DECLARATION_BEGIN;

static SignalOps my_ops = {
    .initialize = [](void *hdl, const Config *root) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->set_apis(apis);
    },

    .on_sod = [](void *hdl, const SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_sod(ev);
    },

    .on_eod = [](void *hdl, const EodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_eod(ev);
    },

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_cs_mbo = [](void *hdl, const CsMboEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_mbo(ev);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
