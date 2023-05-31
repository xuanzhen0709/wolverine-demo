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
namespace csstock {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(uint32_t date, const SodEvent *ev);
  void on_snapshot(const SnapshotEvent *ev);
  void on_bar(const BarEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);
  void on_eod(uint32_t date);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
};

// function definitions

Signal::Signal() { on_sod(0, nullptr); }

void Signal::initialize(const Config *root) {
  // subscribe data
  {
    const auto &md_cfg = root->getChildNode("marketdata");
    const size_t md_nr = md_cfg.getSize();
    for (size_t md_idx = 0; md_idx < md_nr; ++md_idx) {
      const auto &this_cfg = md_cfg.getChildNodeByIdx(md_idx);
      const auto type = this_cfg.getValue<std::string>("type");

      std::vector<std::string> fields;
      {
        const auto fields_cfg = this_cfg.getChildNode("fields");
        for (size_t fld_idx = 0; fld_idx < fields_cfg.getSize(); ++fld_idx) {
          fields.push_back(fields_cfg.getChildElement<std::string>(fld_idx));
        }
      }

      std::vector<std::string> symbols;
      {
        const auto symbols_cfg = this_cfg.getChildNode("symbols");
        for (size_t sym_idx = 0; sym_idx < symbols_cfg.getSize(); ++sym_idx) {
          symbols.push_back(symbols_cfg.getChildElement<std::string>(sym_idx));
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
  LOG_INFO("exchtime:{},ins_nr:{}\n", ev->exchtime, ev->ins_nr);
  // for (int i = 0; i < static_cast<int>(MdFld::_MAX); ++i) {
  //   const auto &fld = ev->flds[i];
  //   LOG_INFO_CONT("{},{}\n", i, fmt::ptr(fld.void_ptr));
  // }
  ++m_cnt;
  // we use m_cnt as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->ins_nr, sigs.data());
}

} // namespace csstock
} // namespace nickchenyj

using namespace nickchenyj::csstock;

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
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
