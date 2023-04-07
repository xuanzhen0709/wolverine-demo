#include "wolverine/common.hpp"
#include "wolverine/config.hpp"
#include "wolverine/marketdata.hpp"
#include "wolverine/signal.hpp"
#include <array>
#include <cmath>
#include <fmt/core.h>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace demo14 {

class MyFeature {
public:
  MyFeature();

  void initialize(const Config *root);
  void initialize(std::string_view config_file);

  void on_sod(uint32_t date, const MdStatic *ms);
  double on_snapshot(const MdStatic *ms, const MdSnapshot *md);
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

void MyFeature::on_sod(uint32_t date, const MdStatic *ms) {
  m_cnt = 0;
  m_sum = 0;
}

double MyFeature::on_snapshot(const MdStatic *ms, const MdSnapshot *md) {
  // market data update
  const auto size = m_volumes.size();
  const auto idx = (m_cnt++) % size;

  const auto old_val = m_volumes[idx];
  m_volumes[idx] = md->volume;

  if ((likely(m_cnt > size))) {
    m_sum = m_sum - old_val + md->volume;
    return m_sum;
  } else if (m_cnt == size) {
    m_sum = m_sum + md->volume;
    return m_sum;
  } else {
    m_sum = m_sum + md->volume;
    return NAN;
  }
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

static void on_sod(void *hdl, uint32_t date, const MdStatic *ms) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p},{:d}\n", __func__, hdl, date);
  ptr->on_sod(date, ms);
}

static double on_snapshot(void *hdl, const cfi::wolverine::MdStatic *ms,
                          const cfi::wolverine::MdSnapshot *md) {
  auto *ptr = reinterpret_cast<MyFeature *>(hdl);
  // DEBUG_LOG("nickchenyj::snapshot1::{:s},{:p}\n", __func__, hdl);
  return ptr->on_snapshot(ms, md);
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
    .on_eod = on_eod,
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new MyFeature{};
  *ops = my_ops;
}

C_DECLARATION_END;