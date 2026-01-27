#!/bin/bash
#
#

# run first job (20230103-20230104) and save checkpoint for 20230103
wl-sim save.yml

# manually make a copy of the checkpoint for the second run
cp -r checkpoint/20230103/checkpoint_save/ checkpoint/20230103/checkpoint_load/

# run the second job which loads from 20230103, and runs only 20230104
wl-sim load.yml

# the final results are the same
echo "################################################################"
echo "################################################################"
echo "################################################################"
echo "################################################################"
echo "comparing the output..."
if diff output/*/20230104/*.csv; then
    echo "results are the same"
else
    echo "results are different"
fi
