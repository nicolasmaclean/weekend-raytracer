echo "Build"
echo "==============="

OMP_NUM_THREADS=8
OMP_VERBOSE=true

mkdir -p ../build
cmake -S .. -G "Ninja Multi-Config" -B ../build
cmake --build ../build --config Debug 

echo ""
echo "Running!"
echo "==============="
../build/viewer/Debug/viewer $1

