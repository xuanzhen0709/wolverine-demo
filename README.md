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

* check that you are using python3.8 by running
  ```
  python3 -V
  ```
  if it's an older version, try 'python3.8 -v' instead. if it's not installed, try enable 
  ```
  /opt/rh/rh-python38/enable
  ```
  just as in the gcc case.

---

# Development Paackage Setup

* ask the maintainer for a copy of the runtime and development packages, the packages are usually placed under
  * /global/wlsim/packages, or
  * /localdata/wlsim/packages

* run install_runtime.py to install the package. please view the commandline help message "-h" before running it.

  do make sure to specify "-P" if your default python3 interpreter is not python3.8.

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

# Usage
* there are two key data structures that deal with the communication between the system and user-defined implementation.

  * SignalApis: this structure holds all the necessary information needed by users when users want to request certain functionalities from the system

    * users need to call subscribe(type: str, fields: vector[str], symbols: vector[str]) during initialization. 'fields' is marketdata-type dependent. for example, the 'section' md loader which loads cross-sectional snapshots for stocks allow specifying a list of fields to load, while other loaders don't handle 'fields' at all for now.

    * set_targets(symbols: vector[str]) is used to set the targets that users intend to trade. and update_signal(exchtime, sigs: np.ndarray) is used to update signal values (note that sigs should always be a vector whose values correspond to the trading targets). set_targets() thus needs to be called before upate_signal(). for single-target trading, users may call set_targets() during initialization. for cross-sectional research where the list of stocks may change inter-day, users may choose to set_targets() on sod, as long as update_signal() uses a numpy array of the right length during that trading day.

  * SignalOps: this structure describes all the available event callbacks that users can receive, and users should create an instance of it and pass back to the system on creation. and the system will callback into user implementations accordingly.
  
    * on_sod: this callback is triggered during start of day, and it provides ins_nr (number of instruments), and the static data for each of the instruments.

    * other callbacks: see examples and notes.

---

# Signal CMakeLists.txt

* NAME should be the signal name

* USER should be the username

* SRCS should be all the source files. 

* for the python version specifically, all the pysig_* functions MUST be defined in the main.py

---

# Caveats

* some dataset is not accessible from our test servers (s15/s19/s23 etc) and dev servers (dev VMs) due to permission constraints. please refer to the "input_dir" fields in the config file and uncomment the relevant lines. you may also need to slightly change the path of the directory to '/localdata/xxx' on some servers.