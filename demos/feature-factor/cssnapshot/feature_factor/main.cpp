#include <cstdio>
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/event_traits.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>
#include <wolverine/utils/time.hpp>


#include <array>
#include <cmath>
#include <string_view>

using namespace cfi::wolverine;

namespace nickchenyj {
namespace csmbo {

class Signal {
public:
  Signal();

  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);

  void on_snapshot(const SnapshotEvent *ev);
  void on_bar(const BarEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

  void on_cs_mbo(const CsMboEvent *ev);

private:
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
  struct Stats {
    size_t cnt = 0;
    int64_t last = 0;

    void clear()
    {
      cnt = 0;
      last = 0;
    }
  };
  const std::vector<double> *ft_order_cnt_{nullptr};
  const std::vector<double> *ft_order_pxmean_{nullptr};
  std::vector<double> sigs_;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  m_cnt = 0;
  // we only try to get feature buffer once per day
  if (!ft_order_cnt_) {
    ft_order_cnt_ = m_apis.get_feature_buf(m_apis.token, "order_cnt");
    ft_order_pxmean_ = m_apis.get_feature_buf(m_apis.token, "order_pxmean");
  }
  wllog_info("ins_nr={}\n", ev->ins_nr);
  sigs_.assign(ev->ins_nr, 0);
}

void Signal::on_eod(const EodEvent *ev)
{
  // reset feature buffer
  ft_order_cnt_ = nullptr;
  ft_order_pxmean_ = nullptr;
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  wllog_info("exchtime:{}/{},localtime:{}/{}\n", ev->exchtime,
             time::exchtime_to_str(ev->exchtime), ev->localtime,
             time::epoch_to_str(ev->localtime));
  const auto count = std::min(10, int(ev->ins_nr));
  for (size_t i = 0; i < count; ++i) {
    wllog_info("feature order_cnt,ins:{},val:{}\n", i, (*ft_order_cnt_)[i]);
    wllog_info("feature order_pxmean,ins:{},val:{}\n", i, (*ft_order_pxmean_)[i]);
    sigs_[i] = (*ft_order_pxmean_)[i] * (*ft_order_cnt_)[i];
  }
  m_apis.update_signal(m_apis.token, ev->exchtime, ev->localtime, ev->ins_nr,
                       sigs_.data());
}

} // namespace csmbo
} // namespace nickchenyj

using nickchenyj::csmbo::Signal;

C_DECLARATION_BEGIN;

static SignalOps my_ops = {
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
