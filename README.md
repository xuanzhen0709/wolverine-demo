# wolverine-demo

* install wolverine-runtime and wolverine-dev packages to ~/.local

* then you should be able to build this projects

* at the project root dir, run:
```
wl-sim src/demo01-py/wlsim.yml
# wl-sim src/demo14/wlsim.yml
```

# Usage
* there are two key data structures that deal with the communication between the system and user-defined implementation.

  * SignalApis: this structure holds all the necessary information needed by users when users want to request certain functionalities from the system

  * SignalOps: this structure describes all the available event callbacks that users can receive, and users should create an instance of it and pass back to the system on creation. and the system will callback into user implementations accordingly.

* a likely implementation involves 

  * some boilerplate code (SignalOps instantiation and callback forwarding at the bottom of the file, and an "on_create" function)

  * (mostly likely) a user-defined signal class that actually handles all the requests.

  * in the implementation, users will need to explicitly call subscribe() and then set_targets() during initialization, and call update_signal() to update signal values.
