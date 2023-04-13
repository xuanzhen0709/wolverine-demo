#include "wolverine/common.hpp"
#include "wolverine/config.hpp"
#include "wolverine/fmt/core.h"
#include "wolverine/marketdata.hpp"
#include "wolverine/signal.hpp"
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
  void initialize(std::string_view config_file);

  void on_sod(uint32_t date, const SodEvent *ev);
  double on_snapshot(const SnapshotEvent *ev);
  double on_bar(const BarEvent *ev);
  void on_eod(uint32_t date);

private:
  uint32_t m_cnt;
  std::vector<double> m_volumes;
  double m_sum;
};

// function definitions

MyFeature::MyFeature() { on_sod(0, nullptr); }

void MyFeature::initialize(const Config *root) {
  const auto freq = root->getValue<uint16_t>("freq");
  m_volumes.resize(freq + 1);
}

void MyFeature::initialize(std::string_view config_file) {
  // or use my own cfg parsing logic
  Config cfg{config_file};
  initialize(&cfg);
}

void MyFeature::on_sod(uint32_t date, const SodEvent *ev) {
  m_cnt = 0;
  m_sum = 0;
}

double MyFeature::on_snapshot(const SnapshotEvent *ev) {
  // market data update
  // fmt::print("received {},{},{}\n", ev->snapshot->exchtime,
  //            ev->snapshot->localtime, ev->ms->instrument);
  const auto size = m_volumes.size();
  const auto idx = (m_cnt++) % size;

  const auto old_val = m_volumes[idx];
  m_volumes[idx] = ev->snapshot->volume;

  if ((likely(m_cnt > size))) {
    m_sum = m_sum - old_val + ev->snapshot->volume;
    return m_sum;
  } else if (m_cnt == size) {
    m_sum = m_sum + ev->snapshot->volume;
    return m_sum;
  } else {
    m_sum = m_sum + ev->snapshot->volume;
    return NAN;
  }
}

double MyFeature::on_bar(const BarEvent *ev) {
  fmt::print("on_bar\n");
  return NAN;
}

void MyFeature::on_eod(uint32_t date) {
  // eod
}

} // namespace demo14
} // namespace nickchenyj

using namespace nickchenyj::demo14;

C_DECLARATION_BEGIN;

static void on_init_from_file(void *hdl, std::string_view config_file) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p},{:s}\n", __func__, hdl,
  //           config_file);
  ptr->initialize(config_file);
}

static void on_init_from_cfg(void *hdl, const cfi::wolverine::Config *root) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p}\n", __func__, hdl);
  ptr->initialize(root);
}

static void on_sod(void *hdl, uint32_t date,
                   const cfi::wolverine::SodEvent *ev) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p},{:d}\n", __func__, hdl, date);
  ptr->on_sod(date, ev);
}

static double on_snapshot(void *hdl, const cfi::wolverine::SnapshotEvent *ev) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p}\n", __func__, hdl);
  return ptr->on_snapshot(ev);
}

static double on_bar(void *hdl, const cfi::wolverine::BarEvent *ev) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p}\n", __func__, hdl);
  return ptr->on_bar(ev);
}

static void on_eod(void *hdl, uint32_t date) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p},{:d}\n", __func__, hdl, date);
  ptr->on_eod(date);
}

static SignalOps my_ops = {
    .init_from_cfg = on_init_from_cfg,
    .init_from_file = on_init_from_file,
    .on_sod = on_sod,
    .on_snapshot = on_snapshot,
    .on_bar = on_bar,
    .on_eod = on_eod,
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new MyFeature{};
  *ops = my_ops;
}

C_DECLARATION_END;
