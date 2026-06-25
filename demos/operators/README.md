# Operators
* check main.cpp for Operator initialization/update/save/load

# How to test
* `save_state.yml` runs 20250106 and saves the end-of-day checkpoint
* `load_state.yml` loads the checkpoint from 20250106 (`auto_load: yesterday`) and runs 20250107; output goes to `output/resume/`
* `full.yml` runs 20250106 – 20250107 continuously (no checkpoint); output goes to `output/full/`
* the 20250107 output from `load_state.yml` matches that from `full.yml` (run both, then `diff output/resume/operators/20250107/operators-20250107.csv output/full/operators/20250107/operators-20250107.csv`)
