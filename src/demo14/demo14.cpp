#include <limits>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

#include <array>
#include <cmath>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace demo14 {

class MyFeature {
public:
  MyFeature();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(uint32_t date, const SodEvent *ev);
  void on_snapshot(const SnapshotEvent *ev);
  void on_bar(const BarEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);
  void on_eod(uint32_t date);

private:
  SignalApis m_apis = {nullptr};
  uint32_t m_cnt = 0;
  std::vector<double> m_volumes = {};
  double m_sum = {0};
};

// function definitions

MyFeature::MyFeature() { on_sod(0, nullptr); }

void MyFeature::initialize(const Config *root) {
  const auto freq = root->getValue<uint16_t>("freq");
  m_volumes.resize(freq + 1);

  // set trading targets
  {
    const auto &tgt_cfg = root->getChildNode("targets");
    const size_t tgt_nr = tgt_cfg.getSize();
    std::vector<std::string> targets;
    for (size_t i = 0; i < tgt_nr; ++i) {
      targets.push_back(tgt_cfg.getChildElement<std::string>(i));
    }
    m_apis.set_targets(m_apis.token, targets);
  }
  // subscribe data
  {
    const auto &md_cfg = root->getChildNode("marketdata");
    const size_t md_nr = md_cfg.getSize();
    for (size_t md_idx = 0; md_idx < md_nr; ++md_idx) {
      const auto &this_cfg = md_cfg.getChildNodeByIdx(md_idx);
      const auto type = this_cfg.getValue<std::string>("type");
      const auto symbols_cfg = this_cfg.getChildNode("symbols");
      std::vector<std::string> symbols;
      for (size_t sym_idx = 0; sym_idx < symbols_cfg.getSize(); ++sym_idx) {
        symbols.push_back(symbols_cfg.getChildElement<std::string>(sym_idx));
      }
      m_apis.subscribe(m_apis.token, type, symbols);
    }
  }
}

void MyFeature::set_apis(SignalApis apis) { m_apis = apis; }

void MyFeature::on_sod(uint32_t date, const SodEvent *ev) {
  m_cnt = 0;
  m_sum = 0;
  if (!ev) {
    return;
  }
  fmt::print("MyFeature on_sod ins_nr={},", ev->ins_nr);
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    if (i) {
      fmt::print(";{}", ms->instrument);
    } else {
      fmt::print("{}", ms->instrument);
    }
  }
  fmt::print("\n");
}

void MyFeature::on_snapshot(const SnapshotEvent *ev) {
  // market data update
  fmt::print("received {},{},{}\n", ev->snapshot->exchtime,
             ev->snapshot->localtime, ev->ms->instrument);
  const auto size = m_volumes.size();
  const auto idx = (m_cnt++) % size;

  const auto old_val = m_volumes[idx];
  m_volumes[idx] = ev->snapshot->volume;

  if ((likely(m_cnt > size))) {
    m_sum = m_sum - old_val + ev->snapshot->volume;
    m_apis.update_signal(m_apis.token, ev->snapshot->exchtime, 1, &m_sum);
  } else if (m_cnt == size) {
    m_sum = m_sum + ev->snapshot->volume;
    m_apis.update_signal(m_apis.token, ev->snapshot->exchtime, 1, &m_sum);
  } else {
    m_sum = m_sum + ev->snapshot->volume;
    const double sig = std::numeric_limits<double>::quiet_NaN();
    m_apis.update_signal(m_apis.token, ev->snapshot->exchtime, 1, &sig);
  }
}

void MyFeature::on_bar(const BarEvent *ev) { fmt::print("on_bar\n"); }

void MyFeature::on_cs_snapshot(const CsSnapshotEvent *ev) {
  fmt::print("on_cs_snapshot,exchtime:{},ins_nr:{},fld_nr:{}\n", ev->exchtime,
             ev->ins_nr, ev->fld_nr);
}

void MyFeature::on_eod(uint32_t date) {
  // eod
}

} // namespace demo14
} // namespace nickchenyj

using namespace nickchenyj::demo14;

C_DECLARATION_BEGIN;

static SignalOps my_ops = {
    .initialize = [](void *hdl, const cfi::wolverine::Config *root) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, cfi::wolverine::SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->set_apis(apis);
    },

    .on_sod = [](void *hdl, uint32_t date,
                 const cfi::wolverine::SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->on_sod(date, ev);
    },

    .on_snapshot = [](void *hdl,
                      const cfi::wolverine::SnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->on_snapshot(ev);
    },

    .on_bar = [](void *hdl, const cfi::wolverine::BarEvent *ev) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->on_bar(ev);
    },

    .on_cs_snapshot = [](void *hdl,
                         const cfi::wolverine::CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_eod = [](void *hdl, uint32_t date) -> void {
      auto *ptr = reinterpret_cast<MyFeature *>(hdl);
      ptr->on_eod(date);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new MyFeature{};
  *ops = my_ops;
}

C_DECLARATION_END;
