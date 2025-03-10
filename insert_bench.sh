#!/bin/bash

BINARY=./build/bin/bench_insertion

# Default parameters
NUM_KEYS=1000001
KEY_SIZE=101
VAL_MIN_SIZE=51
VAL_MAX_SIZE=101

# Threads to test
THREADS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)

# Number of runs per thread count
RUNS=4

# Run test for different thread counts and write output to a file
for t in "${THREADS[@]}"; do
    output_file="masstree_insert_${t}"
    # Remove the previous file if it exists
    rm -f "$output_file"
    
    echo "Running with $t thread(s)..." > "$output_file"
    for i in $(seq 1 $RUNS); do
        echo "Run $i:" >> "$output_file"
        $BINARY "$t" "$NUM_KEYS" "$KEY_SIZE" "$VAL_MIN_SIZE" "$VAL_MAX_SIZE" >> "$output_file"
    done
    echo "-----------------------------" >> "$output_file"
done
