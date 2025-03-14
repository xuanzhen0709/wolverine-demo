#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace csstock {

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
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::load_state(const std::string &indir)
{
  wllog_info("loading state from dir {}\n", indir);
}

void Signal::save_state(const std::string &outdir)
{
  wllog_info("saving state to dir {}\n", outdir);
}

void Signal::on_sod(const SodEvent *ev)
{
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

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  wllog_debug("exchtime:{},ins_nr:{},level_nr:{}\n", ev->exchtime, ev->ins_nr,
              ev->level_nr);
  using FldType = CsSnapshotEvent::FldType;
  // to access any field, use
  // CsSnapshotUtils::get_fld<FldType::field_name>(ev);
  //
  // for level-based fields, it will return a two dimension array
  // bp[level][insidx]
  const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);
  // for non-level-based fields
  // volume[insidx]
  const auto *volume = CsSnapshotUtils::get_fld<FldType::volume>(ev);

  ++m_cnt;
  // we use m_cnt as the sig value for each target
  std::vector<double> sigs(ev->ins_nr, double(m_cnt));
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs.data());
}

} // namespace csstock
} // namespace nickchenyj

using nickchenyj::csstock::Signal;

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
