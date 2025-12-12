#!/bin/bash
# builds the benchmark config, see /benchmark.ipynb for actually running and analyzing data

echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true
mkdir -p ../build
cmake -S .. -G "Ninja Multi-Config" -B ../build
cmake --build ../build --config Release

echo ""
