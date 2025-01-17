# Operators
* check main.cpp for Operator initialization/update/save/load
 
# How to test 
* save_state.yml runs 20250105 and saves the end of day checkpoint
* load_state.yml loads the checkpoint from 20250105 and runs 20250106
* full.yml runs 20250105 - 20250106
* output from load_state.yml matches that from full.yml