#!/bin/bash

echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true
mkdir -p ../build
cmake -S .. -G "Ninja Multi-Config" -B ../build
cmake --build ../build --config Benchmark

echo ""
../build/tracer/Benchmark/tracer $1 $2 >image.ppm
