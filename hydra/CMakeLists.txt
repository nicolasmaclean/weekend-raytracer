# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)
project(hdWeekend CXX)

# "vanilla" -> our USD 26.05 at $USD_ROOT, installs to build-hydra/    (usdview, usdrecord)
# "blender" -> Blender 4.5's harvested USD 25.02, installs to build-hydra-blender/
set(HDW_USD_FLAVOR "vanilla" CACHE STRING "Which USD to build against: vanilla | blender")
set_property(CACHE HDW_USD_FLAVOR PROPERTY STRINGS vanilla blender)

# C++17 is correct for BOTH flavours: USD 26.05 sets it in cmake/defaults/CXXDefaults.cmake
# (§18.1, GCC 13.3), and Blender 4.5 builds every dependency with -DCMAKE_CXX_STANDARD=17
# (build_environment options.cmake).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# So clangd can resolve pxr/ headers in this directory (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(hdWeekend SHARED
    rendererPlugin.cpp
    renderDelegate.cpp
    renderPass.cpp
    renderer.cpp
    config.cpp
    mesh.cpp
    implicitSurfaceSceneIndexPlugin.cpp
    extComputationSceneIndexPlugin.cpp
    instancer.cpp)

if(HDW_USD_FLAVOR STREQUAL "vanilla")
    # hdTiny links only hd + tf; hd transitively brings plug, tf, vt, work, hf, TBB::tbb.
    # hdsi is the scene-index filter library the step D1 plugins wrap.
    find_package(pxr REQUIRED
                 PATHS $ENV{USD_ROOT}
                 NO_DEFAULT_PATH)
    target_link_libraries(hdWeekend PRIVATE hd tf hdsi)
else()
    # No pxrConfig.cmake and no cmake/ directory is harvested from lib-linux_x64, so
    # find_package(pxr) cannot work here: the monolithic libusd_ms.so is the one link target.
    set(BLENDER_LIB_DIR "$ENV{BLENDER_LIB_DIR}" CACHE PATH "lib-linux_x64 sparse checkout")
    if(NOT EXISTS "${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h")
        message(FATAL_ERROR "BLENDER_LIB_DIR is not a lib-<platform> checkout: ${BLENDER_LIB_DIR}")
    endif()
    if(NOT EXISTS "${BLENDER_LIB_DIR}/python/include/python3.11/Python.h")
        message(FATAL_ERROR
            "no Python headers in ${BLENDER_LIB_DIR} - widen the sparse checkout:\n"
            "    git -C ${BLENDER_LIB_DIR} sparse-checkout add python/include")
    endif()

    # Fail here, loudly, rather than as a link error or a runtime TfType miss (docs/notes/ci.md
    # §2). The expected value is a variable so CI can drive it from a targets.toml row.
    set(HDW_BLENDER_USD_NAMESPACE "$ENV{BLENDER_USD_NAMESPACE}" CACHE STRING "asserted against pxr.h")
    file(READ "${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h" _pxr_h)
    if(NOT _pxr_h MATCHES "PXR_INTERNAL_NS[ \t]+${HDW_BLENDER_USD_NAMESPACE}")
        message(FATAL_ERROR
            "namespace mismatch: expected ${HDW_BLENDER_USD_NAMESPACE} in ${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h")
    endif()

    # Blender's USD is built with PXR_PYTHON_SUPPORT_ENABLED, so pxr/base/tf/pySafePython.h -
    # reached from hd/dataSource.h, hd/perfLog.h and trace/collector.h, i.e. effectively every
    # TU - includes <Python.h>. Use the SDK's own 3.11.15 headers (sparse-checkout path
    # python/include, Step 0.2), the ones libusd_ms.so was compiled against. We never link
    # libpython: nothing here calls the C API, and inside Blender the interpreter is already
    # in the process.
    #
    # USD's public headers also reach into TBB - pxr/base/work/loops.h, which renderer.cpp
    # includes, includes <tbb/parallel_for.h> directly. Use the SDK's TBB headers and lib, never
    # the system oneTBB: at runtime this resolves to the libtbb.so.12 Blender already has loaded.
    target_include_directories(hdWeekend PRIVATE
        "${BLENDER_LIB_DIR}/usd/include"
        "${BLENDER_LIB_DIR}/tbb/include"
        "${BLENDER_LIB_DIR}/python/include/python3.11")
    target_link_libraries(hdWeekend PRIVATE
        "${BLENDER_LIB_DIR}/usd/lib/libusd_ms.so"
        "${BLENDER_LIB_DIR}/tbb/lib/libtbb.so")
endif()

# Header-only tracer, for T1. Never link the `tracer` target - it would drag in oneTBB.
target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# hdWeekend.so, not libhdWeekend.so - LibraryPath in plugInfo.json says so.
set_target_properties(hdWeekend PROPERTIES PREFIX "")

# Install layout per §3.3 - note the .so is a SIBLING of the plugin directory.
install(TARGETS hdWeekend        DESTINATION plugin/usd)
install(FILES   plugInfo.json    DESTINATION plugin/usd/hdWeekend/resources)
