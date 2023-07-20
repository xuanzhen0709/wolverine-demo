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
namespace multitickers {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(uint32_t date, const SodEvent *ev);
  void on_eod(uint32_t date);

  void on_snapshot(const SnapshotEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
};

// function definitions

Signal::Signal() { on_sod(0, nullptr); }

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(uint32_t date, const SodEvent *ev) {
  m_cnt = 0;
  if (!ev) {
    return;
  }
  // NOTE:
  // for now in cross-sectional mode, we get the full list of stock names
  // on start of each day
  wllog_info("ins_nr={}\n", ev->ins_nr);
}

void Signal::on_eod(uint32_t date) {
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_snapshot(const SnapshotEvent *ev) { ++m_cnt; }

} // namespace multitickers
} // namespace nickchenyj

using nickchenyj::multitickers::Signal;

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

    .on_sod = [](void *hdl, uint32_t date, const SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_sod(date, ev);
    },

    .on_eod = [](void *hdl, uint32_t date) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_eod(date);
    },

    .on_snapshot = [](void *hdl, const SnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_snapshot(ev);
    },

};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
