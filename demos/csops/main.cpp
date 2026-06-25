#include <cfi_ops/factornode.h>
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

namespace wjp {
namespace csops {

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
  std::string f1_;
  std::string f2_;
  cfi::FactorNode node1_;
  cfi::FactorNode node2_;

  std::vector<double> av1_;
  std::vector<double> ret_;
};

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  // get user config construct formula
  int window = 2;
  fmt::format_to(std::back_inserter(f1_), "ts_inner_product(@bp1,@bp2,{})",
                 window);
  fmt::format_to(std::back_inserter(f2_), "ts_sum(@av1,{})", window);
  node1_.initialize(f1_, "f1");
  node2_.initialize(f2_, "f2");
}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
  node1_.load_checkpoint(indir);
  node2_.load_checkpoint(indir);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
  node1_.save_checkpoint(outdir);
  node2_.save_checkpoint(outdir);
}

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  av1_.reserve(ev->ins_nr);
  ret_.resize(ev->ins_nr);
  // if uv is expended, internal data will be expanded automatically
  node1_.on_day_begin(ev->date, ev->ins_nr);
  node2_.on_day_begin(ev->date, ev->ins_nr);
  wllog_info("date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  node1_.on_day_end(ev->date);
  node2_.on_day_end(ev->date);
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  using FldType = CsSnapshotEvent::FldType;
  auto bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);
  const auto ret1 = node1_.update(bp[0], bp[1], ev->ins_nr);

  // for non-double fields, convert to double first
  // NOTE: must resize (not just reserve) — copy_n writes ins_nr doubles below
  av1_.resize(ev->ins_nr);
  auto av = CsSnapshotUtils::get_fld<FldType::av>(ev);
  std::copy_n(av[0], ev->ins_nr, av1_.begin());
  const auto ret2 = node2_.update(av1_.data(), ev->ins_nr);

  for (size_t i = 0; i < ev->ins_nr; ++i) {
    ret_[i] = ret1[i] + ret2[i];
  }

  ++cnt_;
  // emit the combined result (ret1 + ret2), not the raw ret1
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       ret_.data());
}

} // namespace csops
} // namespace wjp

using wjp::csops::Signal;

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
