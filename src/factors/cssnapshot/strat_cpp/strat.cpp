#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace csstock {

class Strat {
public:
  Strat();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void load_state(const std::string &indir);
  void save_state(const std::string &outdir);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_cs_snapshot(const CsSnapshotEvent *ev);
  void on_cs_mbo(const CsMboEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
};

// function definitions

Strat::Strat() {}

void Strat::initialize(const Config *root) {}

void Strat::set_apis(SignalApis apis) { m_apis = apis; }

void Strat::load_state(const std::string &indir) {
  wllog_info("loading state from dir {}\n", indir);
}

void Strat::save_state(const std::string &outdir) {
  wllog_info("saving state to dir {}\n", outdir);
}

void Strat::on_sod(const SodEvent *ev) { m_cnt = 0; }

void Strat::on_eod(const EodEvent *ev) {
  wllog_info("{} updates received\n", m_cnt);
}

void Strat::on_cs_snapshot(const CsSnapshotEvent *ev) {
  m_cnt++;
  int width;
  int factor_nr;
  const double *factors = m_apis.get_factors(m_apis.token, &width, &factor_nr);
  wllog_info("{},{},factors ready,widht={},factor_nr={}\n", ev->exchtime,
             ev->localtime, width, factor_nr);
  for (int factor_idx = 0; factor_idx < factor_nr; ++factor_idx) {
    wllog_info("factor[{}]", factor_idx + 1);
    const int offset = width * factor_idx;
    for (int i = 0; i < std::min(10, width); ++i) {
      wllog_info_cont(",{}", factors[offset + i]);
    }
    wllog_info_cont("\n");
  }
}

void Strat::on_cs_mbo(const CsMboEvent *ev) {}

} // namespace csstock
} // namespace nickchenyj

using nickchenyj::csstock::Strat;

C_DECLARATION_BEGIN;

static const SignalOps my_ops = {
    .initialize = [](void *hdl, const Config *root) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->set_apis(apis);
    },

    .load_state = [](void *hdl, const std::string &indir) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->load_state(indir);
    },

    .save_state = [](void *hdl, const std::string &outdir) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->save_state(outdir);
    },

    .on_sod = [](void *hdl, const SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->on_sod(ev);
    },

    .on_eod = [](void *hdl, const EodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->on_eod(ev);
    },

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_cs_mbo = [](void *hdl, const CsMboEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Strat *>(hdl);
      ptr->on_cs_mbo(ev);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Strat{};
  *ops = my_ops;
}

C_DECLARATION_END;
