#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/fmt/core.h>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/rapidcsv/rapidcsv.h>
#include <wolverine/signal.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string_view>

using namespace cfi::wolverine;
namespace stdfs = std::filesystem;

namespace nickchenyj {
namespace readcsv {

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
  stdfs::path m_daily_data_dir{};
  SignalApis m_apis = {nullptr};
  size_t m_cnt = 0;
};

// function definitions

Signal::Signal() {}

void Signal::initialize(const Config *root) {
  m_daily_data_dir = (*root)["daily_data_dir"].as<std::string>();
}

void Signal::set_apis(SignalApis apis) { m_apis = apis; }

void Signal::load_state(const std::string &indir) {
  wllog_info("loading state from dir {}\n", indir);
}

void Signal::save_state(const std::string &outdir) {
  wllog_info("saving state to dir {}\n", outdir);
}

void Signal::on_sod(const SodEvent *ev) {
  m_cnt = 0;

  wllog_info("ins_nr={}\n", ev->ins_nr);
  std::vector<std::string> universe;
  universe.reserve(ev->ins_nr);
  for (decltype(ev->ins_nr) i = 0; i < ev->ins_nr; ++i) {
    const auto *ms = ev->ms[i];
    std::string ins{ms->instrument};
    ins.erase(std::find(ins.begin(), ins.end(), '\0'), ins.end());

    std::string exch{ms->exchange};
    exch.erase(std::find(exch.begin(), exch.end(), '\0'), exch.end());

    const std::string symbol = ins + "." + exch;
    universe.emplace_back(symbol);
  }
  {
    const stdfs::path daily_file = m_daily_data_dir / std::to_string(ev->date) /
                                   "dailyfiles" / "daily.csv";
    wllog_info("reading {}\n", daily_file);

    // read daily.csv -> a well-indexed csv
    // LabelParams(A, B)
    // A - the Ath row will be used as column names
    // B - the Bth column will be used as row names
    // here we use the more complex version in order to specify
    // ConverterParams(true), which will allow empty cells to be auto-converted
    // to NAN(double) or 0(integer)
    rapidcsv::Document doc{daily_file.string(), rapidcsv::LabelParams(0, 0),
                           rapidcsv::SeparatorParams(),
                           rapidcsv::ConverterParams(true)};

    // GetCell supports lookup by either index or name
    // eg: GetCell<double>("FstIndustryCSI", "000001.SZ")
    // eg: GetCell<double>(0, "000001.SZ")
    // eg: GetCell<double>("FstIndustryCSI", 2)
    // eg: GetCell<double>(0, 2)

    // if we want to use lookup by names multiple times, we can speed up by
    // caching the col_idx first
    const int col_idx = doc.GetColumnIdx("FstIndustryCSI");

    // or even better, we can also cache the mappings from ins to row idx
    std::map<std::string, int> ins2rowidx;
    {
      const auto rows = doc.GetRowNames();
      // iterate all the rows, and populate the mappings
      for (size_t i = 0; i < rows.size(); ++i) {
        ins2rowidx[rows[i]] = i;
      }
    }
    for (const auto &ins : universe) {
      const auto it = ins2rowidx.find(ins);
      if (it == ins2rowidx.end()) {
        // not found in csv
        // blabla
        continue;
      }
      const int row_idx = it->second;
      const int double_val = doc.GetCell<double>(col_idx, row_idx);
      const int int_val = doc.GetCell<int>(col_idx, row_idx);
      // blabla
    }
  }

  {
    const stdfs::path sec_file =
        m_daily_data_dir / std::to_string(ev->date) / "dailyfiles" / "sec.csv";
    wllog_info("reading {}\n", sec_file);

    // read sec.csv -> a SecuCode may map to multiple values, hence we parse it
    // using column header only
    rapidcsv::Document doc{sec_file.string(), rapidcsv::LabelParams(0, -1)};

    std::map<std::string, std::set<std::string>> ins2concepts;
    std::map<std::string, std::set<std::string>> concepts2ins;
    const int secu_colidx = doc.GetColumnIdx("SecuCode");
    const int code_colidx = doc.GetColumnIdx("ConceptCode");
    for (size_t row_idx = 0; row_idx < doc.GetRowCount(); ++row_idx) {
      const auto ins = doc.GetCell<std::string>(secu_colidx, row_idx);
      const auto concept = doc.GetCell<std::string>(code_colidx, row_idx);
      ins2concepts[ins].insert(concept);
      concepts2ins[concept].insert(ins);
    }

    // you may use ins2concepts and concepts2ins from here...
  }
}

void Signal::on_eod(const EodEvent *ev) {
  wllog_info("{} updates received\n", m_cnt);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {}

} // namespace readcsv
} // namespace nickchenyj

using nickchenyj::readcsv::Signal;

C_DECLARATION_BEGIN;

static const SignalOps my_ops = {
    .initialize = [](void *hdl, const Config *root) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->initialize(root);
    },

    .set_apis = [](void *hdl, SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->set_apis(apis);
    },

    .load_state = [](void *hdl, const std::string &indir) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->load_state(indir);
    },

    .save_state = [](void *hdl, const std::string &outdir) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->save_state(outdir);
    },

    .on_sod = [](void *hdl, const SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_sod(ev);
    },

    .on_eod = [](void *hdl, const EodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_eod(ev);
    },

    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },
};

void on_create(void **ptr, SignalOps *ops) {
  *ptr = new Signal{};
  *ops = my_ops;
}

C_DECLARATION_END;
