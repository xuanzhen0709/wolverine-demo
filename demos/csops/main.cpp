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
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  std::string m_f1;
  std::string m_f2;
  cfi::FactorNode m_node1;
  cfi::FactorNode m_node2;

  std::vector<double> m_av1;
  std::vector<double> m_ret;
};

Signal::Signal() {}

void Signal::initialize(const Config *root)
{
  // get user config construct formula
  int window = 2;
  fmt::format_to(std::back_inserter(m_f1), "ts_inner_product(@bp1,@bp2,{})", window);
  fmt::format_to(std::back_inserter(m_f2), "ts_sum(@av1,{})", window);
  m_node1.initialize(m_f1, "f1");
  m_node2.initialize(m_f2, "f2");
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
  m_node1.load_checkpoint(indir);
  m_node2.load_checkpoint(indir);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
  m_node1.save_checkpoint(outdir);
  m_node2.save_checkpoint(outdir);
}

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  m_av1.reserve(ev->ins_nr);
  m_ret.resize(ev->ins_nr);
  // if uv is expended, internal data will be expanded automatically
  m_node1.on_day_begin(ev->date, ev->ins_nr);
  m_node2.on_day_begin(ev->date, ev->ins_nr);
  wllog_info("date={}, ins_nr={}\n", ev->date, ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  m_node1.on_day_end(ev->date);
  m_node2.on_day_end(ev->date);
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  using FldType = CsSnapshotEvent::FldType;
  auto bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);
  const auto ret1 = m_node1.update(bp[0], bp[1], ev->ins_nr);

  // for non-double fields, convert to double first
  auto av = CsSnapshotUtils::get_fld<FldType::av>(ev);
  std::copy_n(av[0], ev->ins_nr, m_av1.begin());
  const auto ret2 = m_node2.update(m_av1.data(), ev->ins_nr);

  for (size_t i = 0; i < ev->ins_nr; ++i) {
    m_ret[i] = ret1[i] + ret2[i];
  }

  ++m_cnt;
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr, ret1);
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
