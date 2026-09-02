cmake_minimum_required(VERSION 3.15)
project(hdWeekend CXX)

# Must match the USD build exactly (§18.1): USD 26.05 is C++17, GCC 13.3.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# So clangd can resolve pxr/ headers in this directory (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(pxr REQUIRED
             PATHS $ENV{USD_ROOT}
             NO_DEFAULT_PATH)

add_library(hdWeekend SHARED
    rendererPlugin.cpp
    renderDelegate.cpp
    renderPass.cpp
    renderer.cpp
    config.cpp
    mesh.cpp)

# hdTiny links only hd + tf; hd transitively brings plug, tf, vt, work, hf, TBB::tbb.
target_link_libraries(hdWeekend PRIVATE hd tf)

# Header-only tracer, for T1. Never link the `tracer` target - it would drag in oneTBB.
target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# hdWeekend.so, not libhdWeekend.so - LibraryPath in plugInfo.json says so.
set_target_properties(hdWeekend PROPERTIES PREFIX "")

# Install layout per §3.3 - note the .so is a SIBLING of the plugin directory.
install(TARGETS hdWeekend        DESTINATION plugin/usd)
install(FILES   plugInfo.json    DESTINATION plugin/usd/hdWeekend/resources)
