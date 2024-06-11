# 20240611 - wlsim Release v1.4.0
* added sample configs which contains specs of all the usable config items. the examples will be installed to ~/.local/wlsim.examples
* added support for re-usable python and cpp util libraries. sample cmakefiles will also be installed to ~/.local/wlsim.examples
* new config items required for crypto reader - please see the corresponding example config file.

# 20240522 - wlsim Release v1.3.5
* python_runtime is auto detected and should not be manually specified.

# 20240520 - wlmd Release v1.3.4
* fixed timestamp display issue for crypto exchanges

# 20240401 - Release v1.3.3 (Stock and Crypto)
* (MAJOR): split BINANCE into three exchanges: BIANCM(Binance Coin-Margined), BIANUM(Binance USD-Margined), BIANSP(Binance Spot). Please use the new exchange codes.

# 20240319 - Release v1.3.0 (Stock and Crypto)
* (MAJOR): reworked marketdata subscription mechanism, please refer to the changed demo config files and use the new config structure
* (MAJOR): for cs_snapshot data, a new utility function is provided to access the data fields safely. please refer to the related demo for updated usage.
* (MINOR): added a demo using signal reader to read signal values generated offline

# 20240301 - Release crypto-v1.0.0
* (MAJOR): added support for crypto data, use the release for wlsim and wlmd from the crypto branches

# 20240301 - Release v1.2.0
* (MAJOR): re-orged MarketData structure, both the Python and C++ APIs are impacted.
  * MdSnapshot level[10] was replaced by 4 separate arrays: bp[10] bv[10] ap[10] av[10].

# 20240226 - Release v1.1.0
* (MAJOR): improved support for checkpoint
* added wlsim.example.yml (installed to ~/.local/bin along with wlsim) to show all usable config items

# 20240117 - Release 8b85b874
* (MAJOR): added support for new MBO fields
* (MAJOR): improved detection of nas locations

# 20231209 - Release 20231209.3fbcc26
* MAJOR (Config): added section "env"
* MAJOR (Config): made section "calendar" optional, it's encouraged to not define the calendar for best flexibility.
* MAJOR (Config): added option env/python_runtime to replace the original "pylib" config items
* MAJOR (Config): added support for python strat-factor mode

# 20231109
* MINOR (Config): the "refdata" section no longer needs a "config" sub-section - all the config items shoul be put directly under "refdata".

# 20230920
* MINOR(c++ signal module name): the module name in config files now follows the unified notation ${USER}.${name}. py signals have always been following this notation and only c++ signals are affected (instead of ${USER}-${name}).

# 20230903
* MAJOR(Functionality): added (limited) support for factors to pysignal

# 20230902
* MINOR(signal/pysignal): some enum fields have been re-organized, please check the relevant header files
* MAJOR(Functionality): added support for pool worker

# 20230901
* MAJOR(signal): signatures of on_sod/on_eod have been updated - the date parameter is removed and SodEvent/EodEvent now contains a "date" field

# 20230725
* MAJOR(config): the framework has switched to a single-config design. instead of using
```python
config_file: xxx/sig.yml
```
please use
```python
config:
  # put contents of sig.yml below
```
* MAJOR(pysig): now pysig requires running 'make install', and the 'pypath' config item has been removed

# 20230720
* MAJOR (logging): c++ implementation now uses a different set of logging functions, use wllog_xxx instead of LOG_XXX

# 20230717
* MAJOR (marketdata): added ic calculator - which requires an update to the runtime packages, please run install_runtime.py again before using

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
