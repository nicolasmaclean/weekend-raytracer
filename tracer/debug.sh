echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true

mkdir -p ../build
cmake -S .. -G "Ninja Multi-Config" -B ../build
cmake --build ../build --config Debug 

echo ""
../build/tracer/Debug/tracer $1 >image.ppm
xdg-open image.ppm
