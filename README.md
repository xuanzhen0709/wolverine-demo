- [System Environment Setup](#system-environment-setup)
- [Development Paackage Setup](#development-paackage-setup)
- [Quick Note](#quick-note)
- [Data Types](#data-types)
- [Sim Configs](#sim-configs)
  - [Demo](#demo)

-----

# System Environment Setup

* make sure the following lines are added to your ~/.bashrc

  ```
  export PATH=$PATH:~/.local/bin
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/.local/lib
  ```
  
  re-login after making the changes, and verify the variable are successfully updated by running
  ```
  echo $PATH
  echo $LD_LIBRARY_PATH
  ```

* gcc-11 is required. check the gcc version by

  ```
  gcc -v
  ```
  
  if you are using an older version, locate the following file (or a file named 'enable' of a similar path)
  ```
  /opt/rh/gcc-toolset-11/enable
  ```
  then add the following line to your ~/.bashrc
  ```
  source ##THE_ENABLE_FILE_YOU_JUST_FOUND##
  ```
  re-login and check the gcc version.

* check that you are using at least python3.8
  ```
  python3 -V
  ```
  if it's an older version, try 'python3.8 -v' instead. if it's not installed, try enable 
  ```
  /opt/rh/rh-python38/enable
  ```
  in this case, please install Cython the after enabling python 3.8 the first time
  ```
  python3.8 -m pip install Cython --user
  ```

* on some Linux distributions, adding the above 'enable xxx' lines to ~/.bashrc doesn't guarantee auto-enabling them in new terminals.
  you may try and validate whether the changes persist by opening a new terminal. if not, you may add the following lines to your ~/.profile
  ```
  if [ -n "${BASH_VERSION}" ]; then
    if [ -f "${HOME}/.bashrc" ]; then
        source "${HOME}/.bashrc"
    fi
  fi
  ```


---

# Development Paackage Setup

NOTE: before installing the packages and using the framework, do check that the required python/gcc versions are already enabled.
It will lead to incompatibility/missing components/various other issues otherwise.

* ask the maintainer for a copy of the runtime and development packages, the packages are usually placed under
  * /mnt/nas-3/homes/nickchenyj/wlsim/packages, or
  * /localdata/wlsim/packages, or
  * /global/wlsim/packages

* run install_runtime.py to install the package. please view the commandline help message "-h" before running it.

  do make sure to specify "-P" if your default python interpreter is not 'python3' (for example, python3.8 /python3.9 etc).

* build this projects
  ```
  mkdir -p build/Debug
  pushd build/Debug
  cmake -DCMAKE_BUILD_TYPE=Debug ../../
  # or Release build
  # mkdir -p build/Release
  # pushd build/Release
  # cmake -DCMAKE_BUILD_TYPE=Release ../../

  make -j8 install
  popd
  ```

* at the project root dir, run:
  ```
  wl-sim src/csstock/wlsim.yml
  ```

---

# Quick Note

* Build System

  * CMake is used throughout the project, and most functionalities have been encapsulated in helper functions.

  * when adding new implementations, simply call add_wolverine_library() as seen in the demos

* there are two key data structures that deal with the communication between the system and user-defined implementation.

  * SignalApis: this structure holds all the necessary information needed by users when users want to request certain functionalities from the system

    * users need to call subscribe(type: str, fields: vector[str], symbols: vector[str]) during initialization. 'fields' is marketdata-type dependent. for example, the 'section' md loader which loads cross-sectional snapshots for stocks allow specifying a list of fields to load, while other loaders don't handle 'fields' at all for now.

    * set_targets(symbols: vector[str]) is used to set the targets that users intend to trade. and update_signal(exchtime, sigs: np.ndarray) is used to update signal values (note that sigs should always be a vector whose values correspond to the trading targets). set_targets() thus needs to be called before upate_signal(). for single-target trading, users may call set_targets() during initialization. for cross-sectional research where the list of stocks may change inter-day, users may choose to set_targets() on sod, as long as update_signal() uses a numpy array of the right length during that trading day.

  * SignalOps: this structure describes all the available event callbacks that users can receive, and users should create an instance of it and pass back to the system on creation. and the system will callback into user implementations accordingly.
  
    * on_sod: this callback is triggered during start of day, and it provides ins_nr (number of instruments), and the static data for each of the instruments.

    * other callbacks: see examples and notes.

* python version

  * main.py MUST exist and contains the pysig_* functions.

  * when calling update_signal(), please make sure a contiguous array (signal array) is provided. Sometimes we might not always get a contiguous array as expected, especially when the calculation involves complicated merging/concatenation of dataframes, and the underlying buffer is not contiguous.
  in that case, simply call df.copy().values or alternatively, pre-allocate a buffer and fill in the calculated values.

  * to adpat to various python environments, the correct python runtime library must be specified in the "pylib" section of the config file. change the version according to your python version.

---

# Data Types

the detailed documentation/definition of various data types used can be easily found by following the "included headers" or "imported modules".

for c++, the headers are located under ~/.local/include/wolverine

for python, the modules can be found under ~/.local/lib/python3.X/site-packages/cfi/wolverine

---

# Sim Configs

(for now) each test case requires at two config files:

* wlsim.yml (or a different name): used as the main config, and contains all the signal-independent config items.

* sig.yml (or a different name): referenced in the main config, and contains signal-internal config items.

the two-config layout serves multifolded purposes:

* allow expanding existing signals to new instruments/universes. for example, simple alter sig.yml to test new instruments
* allow testing various sets of parameters during the research process (with the help of some simple userside batch generator of config files)
* allow "plugging" signal modules to other research platforms by segregating signal-specific and signal-independent config items.


## Demo

```yaml
# location of the calendar file
calendar: scripts/ChinaTradingDates.txt
# date range
start: 20230101
end: 20230103

# Secmaster section
# secmaster is the module that provides reference data (aka static data)
secmaster:
  config:
    # change input_dir to override the default data dir
    # input_dir: /global/wlsim/data/secmaster

# marketdata section
# multiple market data sources can be defined as a list
marketdata:
  # each market data source has a 'type' - which is the unique name across the whole system, specified by the user
  # module: is the name of the underlying data modules
  # priority[optional]: used when we need to arbitrate between multiple data sources if they happen to have the same timestamp. a lower value indicates a higher priority and thus the callback will be received earlier
  - type: section
    module: section
    priority: 0
    config:
    # input_dir: /global/wlsim/data/stock_section

signal:
  module: py
  config:
    name: csstock-py
    module: nickchenyj.csstock
    pylib: libpython3.8.so
    pypath:
      - build/Debug/src/csstock-py/
    config_file: src/csstock-py/sig.yml
    output:
      module: csv
      config:
        output_dir: output
```