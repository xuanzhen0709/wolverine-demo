# 20230606
* added support for priority-based ordering of multiple marketdata sources. a larger number indicates a lower priority. for example, when using csmbp(event based market by price) with cross sectional data at the same time, giving csmbp a higher priority guarantees that csmbp delivers a callback before cross sectional md does even if they have the same timestamp.
* updated default input location of md-section

# 20230601

* added order event APIs
* C++ API: minor changes to API ordering
* Python API: simplify main.py imports, some callback stubs will be automatically provided.
