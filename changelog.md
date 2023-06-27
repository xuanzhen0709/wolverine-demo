# 20230627
* MAJOR (CsSnapshotEvent): reworked subscription mechanism - cs-snapshot module now accepts a "levels" options. instead of specifying ap1/ap2/av1/av2 etc, users should specify ap/av and set levels=xxx.
* MAJOR (Signal API): reworked api for market by order events
* MINOR: added demos for mbo/cythonization, users may use .pyx files (in cython syntax) to speed up loops

# 20230617
* MAJOR (Signal API): added load_state()/save_state() which allow users to preserve states across multiple runs
* MAJOR (MarketData): renamed "section" marketdata to "cs-snapshot", which better aligns with other modules

# 20230615
* MAJOR (Signal API): users don't need to call subscribe() and set_targets() manually, and they are handled automatically
* MAJOR (Config): the main config no longer holds a marketdata section
* MAJOR (Config): sigcfg.yml now requires a "targets" section as well as a "marketdata" section. use the "options" field to override the default behavior
* MAJOR (Config): the signal section in the main config now uses a different format
* MINOR (Config): no need to specify priority when merging multiple marketdata streams
* MINOR (Config): the secmaster section has been renamed to refdata
* MINOR (Utils): re-worked interfaces for cfi::wolverine::Config

# 20230606
* added support for priority-based ordering of multiple marketdata sources. a larger number indicates a lower priority. for example, when using csmbp(event based market by price) with cross sectional data at the same time, giving csmbp a higher priority guarantees that csmbp delivers a callback before cross sectional md does even if they have the same timestamp.
* updated default input location of md-section
* update_signal: updated signature to require both exchtime and localtime

# 20230601

* added order event APIs
* C++ API: minor changes to API ordering
* Python API: simplify main.py imports, some callback stubs will be automatically provided.
