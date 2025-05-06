#include <cfi_ops/feature.h>
#include <cfi_ops/types.h>
#include <traits/cs_snapshot_traits.hpp>
#include <type_traits>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <cmath>

using namespace cfi::wolverine;

namespace ops {
namespace feature {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void load_state(const std::string &indir);
  void save_state(const std::string &outdir);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_cs_snapshot(const CsSnapshotEvent *ev);

  void handle_data();

  template <CsSnapshotEvent::FldType Fld, bool need_index = false>
  void copy_data(const CsSnapshotEvent *ev, int index, int i);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  cfi::Feature m_f1;

  //  for lack data factory
  std::vector<std::string> m_data_names;
  std::vector<const cfi::item_t *> m_data_ptrs;
  std::vector<std::vector<cfi::item_t>> m_ops_data_;
  std::vector<std::function<void(const CsSnapshotEvent *)>> m_copy_data;
};

Signal::Signal() {}


void Signal::initialize(const Config *root)
{
  auto formula = (*root)["formula"].as<std::string>();
  //   wllog_info("config={}\n formula={}\n", root->to_string(), formula);
  m_f1.initialize(formula, "m_f1");
  m_data_names = m_f1.get_datanames();
  m_ops_data_.resize(m_data_names.size());
  m_data_ptrs.resize(m_data_names.size());
  handle_data();
}

template <CsSnapshotEvent::FldType Fld, bool need_index>
void Signal::copy_data(const CsSnapshotEvent *ev, int index, int i)
{
  auto arr = CsSnapshotUtils::get_fld<Fld>(ev);
  if constexpr (need_index) {

    if constexpr (std::is_same_v<typename CsSnapshotTraits<Fld>::type,
                                 cfi::item_t>) {
      m_data_ptrs[i] = arr[index];
    } else {
      std::copy_n(arr[index], ev->ins_nr, m_ops_data_[i].begin());
      m_data_ptrs[i] = m_ops_data_[i].data();
    }
  } else {
    if constexpr (std::is_same_v<typename CsSnapshotTraits<Fld>::type,
                                 cfi::item_t>) {
      m_data_ptrs[i] = arr;
    } else {
      std::copy_n(arr, ev->ins_nr, m_ops_data_[i].begin());
      m_data_ptrs[i] = m_ops_data_[i].data();
    }
  }
}

void Signal::handle_data()
{
  using FldType = CsSnapshotEvent::FldType;
  int i = 0;
  for (auto &name : m_data_names) {
    size_t num_pos = name.find_first_of("0123456789");
    std::string prefix = name.substr(0, num_pos);
    int index = -1;

    if (num_pos != std::string::npos) {
      if (auto tmp = name.find("_cnt"); tmp != std::string::npos) {
        index = std::stoi(name.substr(num_pos, tmp)) - 1; // 0-based
      } else {
        index = std::stoi(name.substr(num_pos)) - 1; // 0-based
      }
    }

    if (prefix == "bid") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::bid_cnt, true>(ev, index, i); });
    } else if (prefix == "ask") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::ask_cnt, true>(ev, index, i); });
    } else if (prefix == "av") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::av, true>(ev, index, i); });
    } else if (prefix == "ap") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::ap, true>(ev, index, i); });
    } else if (prefix == "bp") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::bp, true>(ev, index, i); });
    } else if (prefix == "bv") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::bv, true>(ev, index, i); });
    } else if (name == "exchtime") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::exchtime>(ev, index, i); });
    } else if (name == "localtime") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::localtime>(ev, index, i); });
    } else if (name == "last") {
      m_copy_data.push_back([=](const CsSnapshotEvent *ev)
                            { copy_data<FldType::last_price>(ev, index, i); });
    } else if (name == "total_ask_cnt") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_cnt>(ev, index, i); });
    } else if (name == "total_bid_cnt") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_cnt>(ev, index, i); });
    } else if (name == "total_ask_qty") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_qty>(ev, index, i); });
    } else if (name == "total_bid_qty") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_qty>(ev, index, i); });
    } else if (name == "total_ask_lvl") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_lvl>(ev, index, i); });
    } else if (name == "total_bid_lvl") {
      m_copy_data.push_back(
          [=](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_lvl>(ev, index, i); });

    } else {
      wllog_fatal("Invalid data name {}\n", name);
    }
    ++i;
  }
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
  m_f1.load_checkpoint(indir);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
  m_f1.save_checkpoint(outdir);
}

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  for (auto &vec : m_ops_data_) {
    vec.resize(ev->ins_nr);
  }
  // if uv is expended, internal data will be expanded automatically
  m_f1.on_day_begin(ev->date, ev->ins_nr);
  wllog_info("date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  m_f1.on_day_end(ev->date);
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  //   handle_data(ev);
  for (auto &f : m_copy_data) {
    f(ev);
  }

  const auto ret = m_f1.update(m_data_ptrs, ev->ins_nr);
  ++m_cnt;
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       ret);
}

} // namespace feature
} // namespace ops

using ops::feature::Signal;

C_DECLARATION_BEGIN;

static const SignalOps my_ops = {
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

    .load_state = [](void *hdl, const std::string &indir) -> void
    {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->load_state(indir);
    },

    .save_state = [](void *hdl, const std::string &outdir) -> void
    {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->save_state(outdir);
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
};

void on_create(void **ptr, SignalOps *ops)
{
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
