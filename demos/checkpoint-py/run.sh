#!/bin/bash
#
# Mirrors demos/checkpoint/run.sh (the C++ version):
#   1. save.yml  — run 20230103-20230104, save the 20230103 end-of-day checkpoint
#   2. copy the saved checkpoint into the name expected by the load run
#   3. load.yml  — load the 20230103 checkpoint and run only 20230104
#   4. compare   — the load run's 20230104 output must match the save run's

set -euo pipefail

rm -rf checkpoint output

# run first job (20230103-20230104) and save checkpoint for 20230103
wl-sim save.yml

# the checkpoint is stored under checkpoint/<date>/<signal_name>/; the load run
# uses a different signal name, so copy the saved dir into the load name.
cp -r checkpoint/20230103/checkpoint_save/ checkpoint/20230103/checkpoint_load/

# run the second job which loads from 20230103, and runs only 20230104
wl-sim load.yml

echo "################################################################"
echo "comparing the output..."
if diff output/*/20230104/*.csv; then
    echo "results are the same"
else
    echo "results are different"
    exit 1
fi
