- [System Environment Setup](#system-environment-setup)
- [Development Package Setup](#development-package-setup)
- [Quick Note](#quick-note)
- [Docs and Types](#docs-and-types)
  - [Timestamp](#timestamp)
- [Sim Config](#sim-config)
  - [Demo](#demo)

-----

# System Environment Setup
<mark>Do not use conda!</mark>  
<mark>Do not use conda!!</mark>  
<mark>Do not use conda!!!</mark>  
- make sure the following lines are added to your ~/.bashrc

  ```
  export PATH=$PATH:~/.local/bin
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/.local/lib
  ```
  
  re-login after making the changes, and verify the variable are successfully updated by running

  ```
  echo $PATH
  echo $LD_LIBRARY_PATH
  ```

- gcc-11 is required. check the gcc version by

  ```
  gcc -v
  ```
  
  if you are using an older version, locate the following file (or a file named 'enable' of a similar path)

  ```
  /opt/rh/gcc-toolset-11/enable

  or

  /opt/rh/devtoolset-11/enable
  ```

  then add the following line to your ~/.bashrc

  ```
  source ##THE_ENABLE_FILE_YOU_JUST_FOUND##
  ```

  re-login and check the gcc version.

- check that you are using at least python3.8

  ```
  python3 -V
  ```

  if it's an older version, try 'python3.8 -v' instead. if it's not installed, try enable

  ```
  /opt/rh/rh-python38/enable
  ```

- since wlsim currently requires a very specific setup, which may conflict with system-provided python3, users may wrap the above env-enablement statements in a bash function

  -  enable rh-python38 and devtoolset-11 temporarily, run in the console
      ```
      source /opt/rh/rh-python38/enable
      source /opt/rh/devtoolset-11/enable
      ```
  -  create virtualenv for wolverine
      ```
      python3 -m venv ~/venv/wlsim
      ```
  -  Add the following lines to the ~/.bashrc file
      ```
      # in ~/.bashrc
      # the following two lines can be enabled unconditionally
      export PATH=$PATH:~/.local/bin
      export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/.local/lib

      # a wrapper that enables wlsim specific python3 and gcc
      function enable_wlsim_env()
      {
          local plat=$(uname -r)

          if [[ $plat =~ el8 ]]; then
              if shopt -q login_shell; then
                  echo "el8, enabling gcc-toolset-11"
              fi
              source /opt/rh/gcc-toolset-11/enable
          elif [[ $plat =~ el7 ]]; then
            if shopt -q login_shell; then
                echo "el7, enabling devtoolset-11"
            fi
            source /opt/rh/devtoolset-11/enable

            if shopt -q login_shell; then
                echo "el7, enabling rh-python38"
            fi
            source /opt/rh/rh-python38/enable

            if shopt -q login_shell; then
                echo "el7, enabling wlsim python38 venv
            fi
            source ~/venv/wlsim/bin/activate
          fi
      }           
      ```

  users can run in the console

  ```
  enable_wlsim_env
  ```

  to manually activate the environemnt before using wlsim.

- please install the following python packages (this needs to done only once)

  ```
  python3.8 -m pip install --user Cython wheel "numpy>=1.23.4" -i https://pypi.tuna.tsinghua.edu.cn/simple
  ```

- on some Linux distributions, adding the above 'enable xxx' lines to ~/.bashrc doesn't guarantee auto-enabling them in new terminals.
  you may try and validate whether the changes persist by opening a new terminal. if not, you may add the following lines to your ~/.profile

  ```
  if [ -n "${BASH_VERSION}" ]; then
    if [ -f "${HOME}/.bashrc" ]; then
        source "${HOME}/.bashrc"
    fi
  fi
  ```

---

# Development Package Setup

NOTE: before installing the packages and using the framework, do check that the required python/gcc versions are already enabled.
It will lead to incompatibility/missing components/various other issues otherwise.

- ask the maintainer for a copy of the runtime and development packages, the packages are usually placed under
  - /mnt/nas-3/homes/nickchenyj/wlsim/packages
  - /mnt/nas-i/homes/nickchenyj/wlsim/packages

- run install_runtime.py to install the package. please view the commandline help message "-h" before running it.

  do make sure to specify "-P" if your default python interpreter is not 'python3' (for example, python3.8 /python3.9 etc).

- build this projects, <mark>if you are an intsern, please use `intern` branch</mark>

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

- at the project root dir, run:

  ```
  wl-sim src/csstock/wlsim.yml
  ```

---

# Quick Note

- Build System

  - CMake is used throughout the project, and most functionalities have been encapsulated in helper functions.

  - when adding new implementations, simply call add_wolverine_library() as seen in the demos

- there are two key data structures that deal with the communication between the system and user-defined implementation.

  - SignalApis: this structure holds all the necessary information needed by users when users want to request certain functionalities from the system

    - update_signal(exchtime, localtime, sigs: np.ndarray) is used to update signal values (note that sigs should always be a vector whose values correspond to the trading targets). set_targets() thus needs to be called before upate_signal(). for single-target trading, users may call set_targets() during initialization. for cross-sectional research where the list of stocks may change inter-day, users may choose to set_targets() on sod, as long as update_signal() uses a numpy array of the right length during that trading day.

  - SignalOps: this structure describes all the available event callbacks that users can receive, and users should create an instance of it and pass back to the system on creation. and the system will callback into user implementations accordingly.
  
    - on_sod: this callback is triggered during start of day, and it provides ins_nr (number of instruments), and the static data for each of the instruments.

    - other callbacks: see examples and notes.

- python version

  - main.py MUST exist and contains the pysig_* functions.

  - when calling update_signal(), please make sure a contiguous array (signal array) is provided. Sometimes we might not always get a contiguous array as expected, especially when the calculation involves complicated merging/concatenation of dataframes, and the underlying buffer is not contiguous.
  in that case, simply call df.copy().values or alternatively, pre-allocate a buffer and fill in the calculated values.

  - to adpat to various python environments, the correct python runtime library must be specified in the "env/python_runtime" variable of the config file. change the version according to your python version.

---

# Docs and Types

the detailed documentation/definition of various data types used can be easily found by following the "included headers" or "imported modules".

for c++, the headers are located under ~/.local/include/wolverine.

for python, the modules can be found under ~/.local/lib/python3.X/site-packages/cfi/wolverine

## Timestamp

all the timestamps are in nanosecond precision, although their meaning may vary.

- exchtime(int64_t): nanoseconds since the LOGICAL start of day. 9AM is represented as 9 *NS_PER_HOUR, and 21:30PM as -2.5* NS_PER_HOUR. we consider a LOGICAL trading day as a continuous 24-hour span, hence exchtime always sits in (-24 *NS_PER_HOUR, 24* NS_PER_HOUR).

- localtime(uint64_t): nanoseconds since epoch.

the time.hpp header that comes with the development package provides some useful utility functions.

for python users, you may use any of the methods below to convert localtime to human-readable format.

```python
pd.to_datetime(df["localtime"], utc=True).apply(lambda x: x.tz_convert("Asia/Shanghai").tz_localize(None))
# the methods below will lose nanosecond precision
pd.Timestamp.fromtimestamp(x/1e9)
datetime.datetime.fromtimestamp(x/1e9)   
```

do bear in mind that time strinifications are extremely slow by nature, and do not use them in any hot paths.

---

# Sim Config

we recommend using "wlsim.yml" as the config file name, and the file, as the extension indicates, follows the YAML standard.

the config file has a hierarchical layout - and a few principles are followed

- any parameter or configuration items applicable to a module should be placed under the config section

- signal specific config items, along with marketdata subscription, and trading target selection, are placed together

## Demo

```yaml
env:
  # python_runtime: libpython3.8.so
# location of the calendar file
# date range
start: 20230101
end: 20230103

# refdata section
# refdata is the module that provides reference data (aka static data)
refdata:
  # uncomment to override data_dir
  # data_dir: /global/wlsim/data/refdata

signal:
  name: csstock-py
  module: py
  output:
    module: csv
    config:
      output_dir: output
  config:
    module: nickchenyj.csstock
    config:
        # blabla
```

```yaml
env:
  # python_runtime: libpython3.8.so
# date range
start: 20230101
end: 20230103

# refdata section
# refdata is the module that provides reference data (aka static data)
refdata:
  # uncomment to override data_dir
  # data_dir: /global/wlsim/data/refdata

signal:
  name: csstock-py
  module: nickchenyj-csstock
  output:
    module: csv
    config:
      output_dir: output
  config:
    # blabla
```
