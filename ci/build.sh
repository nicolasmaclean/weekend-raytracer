#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Configure, build and install the blender flavour against <sdk>. Runs inside manylinux_2_28 (via
# ci/docker_build.sh). Reads USD_NAMESPACE, PYTHON_MM, CXX_STANDARD. Every value goes in as -D, never
# through $ENV{}, so the configure line in the log is a complete record of what was built.
# See docs/plans/blender-ci.md Step B3.
set -euo pipefail
sdk=$1 build=$2
gcc --version | head -1            # record the toolchain in the log - GCC 14 expected
cmake -S hydra -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build/install" \
      -DHDW_USD_FLAVOR=blender -DBLENDER_LIB_DIR="$sdk" \
      -DHDW_BLENDER_USD_NAMESPACE="$USD_NAMESPACE" \
      -DHDW_BLENDER_PYTHON="$PYTHON_MM" \
      -DHDW_CXX_STANDARD="$CXX_STANDARD"
cmake --build "$build" -j"$(nproc)"
cmake --install "$build"
