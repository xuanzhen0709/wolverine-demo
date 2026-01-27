#include <map>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string_view>
#include "zpp_helper.hpp"

using namespace cfi::wolverine;

namespace nickchenyj {
namespace checkpoint {

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

private:
  SignalApis apis_ = {nullptr};
  size_t cnt_ = 0;
  std::vector<double> vec_;
  size_t total_cnt_ = 0;
  std::vector<std::vector<double>> d2vec_;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
  const auto dir = std::filesystem::path(indir);
  deserialize(dir / "d2vec.bin", d2vec_);
  wllog_info("d2vec_: loaded {},{}/{},{}/{}\n", d2vec_.size(), d2vec_.front().front(), d2vec_.front().back(), d2vec_.back().front(), d2vec_.back().back());
  deserialize(dir / "total_cnt.bin", total_cnt_);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
  const auto dir = std::filesystem::path(outdir);
  serialize(dir / "d2vec.bin", d2vec_);
  wllog_info("d2vec_: saved {},{}/{},{}/{}\n", d2vec_.size(), d2vec_.front().front(), d2vec_.front().back(), d2vec_.back().front(), d2vec_.back().back());
  serialize(dir / "total_cnt.bin", total_cnt_);
}

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
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

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  wllog_debug("exchtime:{},ins_nr:{}\n", ev->exchtime, ev->ins_nr);
  ++cnt_;
  ++total_cnt_;
  // we use cnt_ as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(cnt_) + total_cnt_);
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime, ev->ins_nr,
                      sigs.data());
  d2vec_.push_back(sigs);
  if (d2vec_.size() > 30) {
    d2vec_.erase(d2vec_.begin());
  }
}

} // namespace checkpoint
} // namespace nickchenyj

using nickchenyj::checkpoint::Signal;

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
