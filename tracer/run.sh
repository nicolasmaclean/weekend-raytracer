cmake -S . -G "Ninja Multi-Config" -B build
cmake --build build --config Debug
build/Debug/tracer >image.ppm
