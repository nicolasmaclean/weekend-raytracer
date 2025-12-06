echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true
cmake -S . -G "Ninja Multi-Config" -B build
cmake --build build --config Release

echo ""
build/Release/tracer $1 >image.ppm
xdg-open image.ppm
