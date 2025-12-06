echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true
cmake -S . -G "Ninja Multi-Config" -B build
cmake --build build --config Debug

$perf stat build/Debug/tracer >image.ppm
