To prepare for production, please follow the steps below

* Contact Yanjun for a personal git repo
* Make sure your libraries are built with 'Release' flag on. If not sure, remove the build directory, and rebuild using 'cmake' or 'cmake -DCMAKE_BUILD_TYPE=Release' (not with -DCMAKE_BUILD_TYPE=Debug)
* use the collect_artifacts.py script to collect the *.so files
* move the collected files to the personal git repo
* copy your signals configuration file(s) and put them along with the so files