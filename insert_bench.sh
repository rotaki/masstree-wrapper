#!/bin/bash

BINARY=./build/bin/bench_insertion

# Default parameters
NUM_KEYS=1000000
KEY_SIZE=100
VAL_MIN_SIZE=50
VAL_MAX_SIZE=100

# Threads to test
THREADS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)

# Run test for different thread counts and write output to a file
for t in "${THREADS[@]}"; do
    output_file="masstree_insert_${t}"
    echo "Running with $t thread(s)..." > "$output_file"
    $BINARY "$t" "$NUM_KEYS" "$KEY_SIZE" "$VAL_MIN_SIZE" "$VAL_MAX_SIZE" >> "$output_file"
    echo "-----------------------------" >> "$output_file"
done
