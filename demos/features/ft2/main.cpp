#include <cstdio>
#include "common.hpp"
#include "config.hpp"
#include "event.hpp"
#include "event_traits.hpp"
#include "feature.hpp"
#include "fmt/core.h"
#include "logging.hpp"
#include "marketdata.hpp"
#include "utils/time.hpp"


#include <array>
#include <cmath>
#include <string_view>

using namespace cfi::wolverine;

namespace {
class Feature {
public:
  Feature(int insidx);

  void initialize(const Config *root);
  void set_apis(FeatureApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);
  void on_cs_mbo(const CsMboEvent *ev);

private:
  const int insidx_;
  FeatureApis apis_ = {nullptr};
  std::vector<double> sigs_;
};

// function definitions

Feature::Feature(int insidx) : insidx_(insidx) {}

void Feature::initialize(const Config *root)
{
  const auto features = (*root)["provides"].as<std::vector<std::string>>();
  sigs_.resize(features.size(), 0);
}

void Feature::set_apis(FeatureApis apis) { apis_ = apis; }

void Feature::on_sod(const SodEvent *ev)
{
  std::fill(sigs_.begin(), sigs_.end(), 0);
}

void Feature::on_eod(const EodEvent *ev) { wllog_debug("eod\n"); }

void Feature::on_cs_snapshot(const CsSnapshotEvent *ev) {}

void Feature::on_cs_mbo(const CsMboEvent *ev)
{
  wllog_debug("exchtime:{}/{},localtime:{}/{}\n", ev->exchtime,
              time::exchtime_to_str(ev->exchtime), ev->localtime,
              time::epoch_to_str(ev->localtime));
  using TradeFldType = CsMboEvent::TradeFldType;

  // int64_t temp_exchtime;
  double total_price = 0;
  double max = 0;
  double min = 0;

  {
    const auto *trade_cnt = CsMboUtils::get_fld<TradeFldType::Cnt>(ev);
    const auto *trade_price = CsMboUtils::get_fld<TradeFldType::Price>(ev);

    const auto &cnt = trade_cnt[insidx_];
    const auto &price = trade_price[insidx_];
    for (int evidx = 0; size_t(evidx) < cnt; ++evidx) {
      total_price += price[evidx];
      max = std::max(max, price[evidx]);
      min = std::min(min, price[evidx]);
    }
    sigs_[0] = total_price / cnt;
    sigs_[1] = max;
    sigs_[2] = min;
  }
  apis_.update(apis_.token, ev->exchtime, ev->localtime, sigs_.data());
}

} // namespace

C_DECLARATION_BEGIN;

static FeatureOps my_ops = {
    .initialize = [](void *hdl, const Config *root) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, FeatureApis apis) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->set_apis(apis);
    },

    .on_sod = [](void *hdl, const SodEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->on_sod(ev);
    },

    .on_eod = [](void *hdl, const EodEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->on_eod(ev);
    },

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->on_cs_snapshot(ev);
    },

    .on_cs_mbo = [](void *hdl, const CsMboEvent *ev) -> void
    {
      auto *ptr = reinterpret_cast<Feature *>(hdl);
      ptr->on_cs_mbo(ev);
    },
};

void on_create(void **ptr, FeatureOps *ops, int insidx)
{
  *ptr = new Feature{insidx};
  *ops = my_ops;
}

C_DECLARATION_END;
