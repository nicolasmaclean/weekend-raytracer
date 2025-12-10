cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(tracer)

set(CMAKE_CONFIGURATION_TYPES
    Debug
    Release
    Benchmark
    CACHE STRING "Available build configurations" FORCE
)

set(CMAKE_CXX_FLAGS_BENCHMARK "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -DBENCHMARK" CACHE STRING "Flags for Benchmark config" FORCE)
set(CMAKE_C_FLAGS_BENCHMARK   "${CMAKE_C_FLAGS_RELEASE}   -O3 -DNDEBUG -DBENCHMARK" CACHE STRING "Flags for Benchmark config" FORCE)

add_subdirectory(vendor)
add_subdirectory(tracer)
