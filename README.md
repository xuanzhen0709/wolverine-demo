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
  if it's an older version, try 'python3.8 -v' instead.

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

  * SignalOps: this structure describes all the available event callbacks that users can receive, and users should create an instance of it and pass back to the system on creation. and the system will callback into user implementations accordingly.

* a likely implementation involves 

  * some boilerplate code (SignalOps instantiation and callback forwarding at the bottom of the file, and an "on_create" function)

  * (mostly likely) a user-defined signal class that actually handles all the requests.

  * in the implementation, users will need to explicitly call subscribe() and then set_targets() during initialization, and call update_signal() to update signal values.
