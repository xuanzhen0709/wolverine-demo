#include <cfi_ops/feature.h>
#include <cfi_ops/types.h>
#include <traits/cs_snapshot_traits.hpp>
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
  void copy_data(const CsSnapshotEvent *ev, int index);

private:
  SignalApis apis_ = {nullptr};
  size_t cnt_ = 0;
  cfi::Feature f1_;
  std::vector<cfi::feature_variant> inputs_;
  //  for lack data factory
  std::vector<std::function<void(const CsSnapshotEvent *)>> construct_data_;
};

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  auto formula = (*root)["formula"].as<std::string>();
  f1_.initialize(formula, "f1_");
  handle_data();
}

template <CsSnapshotEvent::FldType Fld, bool need_index>
void Signal::copy_data(const CsSnapshotEvent *ev, int index)
{
  auto arr = CsSnapshotUtils::get_fld<Fld>(ev);
  if constexpr (need_index) {
    inputs_.emplace_back(arr[index]);
  } else {
    inputs_.emplace_back(arr);
  }
}

void Signal::handle_data()
{
  const auto &datanames = f1_.get_datanames();

  inputs_.reserve(datanames.size());
  construct_data_.reserve(datanames.size());
  using FldType = CsSnapshotEvent::FldType;
  for (auto &name : datanames) {
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
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::bid_cnt, true>(ev, index); });
    } else if (prefix == "ask") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::ask_cnt, true>(ev, index); });
    } else if (prefix == "av") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::av, true>(ev, index); });
    } else if (prefix == "ap") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::ap, true>(ev, index); });
    } else if (prefix == "bp") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::bp, true>(ev, index); });
    } else if (prefix == "bv") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::bv, true>(ev, index); });
    } else if (name == "exchtime") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::exchtime>(ev, index); });
    } else if (name == "localtime") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::localtime>(ev, index); });
    } else if (name == "last") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::last_price>(ev, index); });
    } else if (name == "last_price") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::last_price>(ev, index); });
    } else if (name == "volume") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::volume>(ev, index); });
    } else if (name == "turnover") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev)
                                 { copy_data<FldType::turnover>(ev, index); });
    } else if (name == "open_interest") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::open_interest>(ev, index); });
    } else if (name == "total_ask_cnt") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_cnt>(ev, index); });
    } else if (name == "total_bid_cnt") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_cnt>(ev, index); });
    } else if (name == "total_ask_qty") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_qty>(ev, index); });
    } else if (name == "total_bid_qty") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_qty>(ev, index); });
    } else if (name == "total_ask_lvl") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_ask_lvl>(ev, index); });
    } else if (name == "total_bid_lvl") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::total_bid_lvl>(ev, index); });
    } else if (name == "bid_volume") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::bid_volume>(ev, index); });
    } else if (name == "ask_volume") {
      construct_data_.push_back(
          [=, this](const CsSnapshotEvent *ev)
          { copy_data<FldType::ask_volume>(ev, index); });
    } else {
      wllog_fatal("Invalid data name {}\n", name);
    }
  }
}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
  f1_.load_checkpoint(indir);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
  f1_.save_checkpoint(outdir);
}

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  // if uv is expended, internal data will be expanded automatically
  f1_.on_day_begin(ev->date, ev->ins_nr);
  wllog_info("date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  f1_.on_day_end(ev->date);
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  inputs_.clear();
  for (auto &f : construct_data_) {
    f(ev);
  }
  const auto ret = f1_.update(inputs_, ev->ins_nr);
  ++cnt_;
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime, ev->ins_nr,
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
