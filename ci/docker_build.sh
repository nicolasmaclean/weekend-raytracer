#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run ci/build.sh <sdk> <build-dir> inside manylinux_2_28. The one place the docker line and the image
# tag live: .github/workflows/blender.yml and ci/local.sh both call this, so they cannot drift.
# Reads USD_NAMESPACE, PYTHON_MM, CXX_STANDARD, and MANYLINUX_IMAGE to override the image.
# See docs/plans/blender-ci.md Step B5.
set -euo pipefail
sdk=$(realpath "$1") build=$2
[[ "$build" != /* ]] || { echo "docker_build: <build-dir> must be relative - only \$PWD is mounted" >&2; exit 1; }
image=${MANYLINUX_IMAGE:-quay.io/pypa/manylinux_2_28_x86_64:2026.09.14-1}   # bump deliberately
# The container runs as root; hand build/ back to the caller, pass or fail, or a local run leaves a
# tree only sudo can delete. A no-op on a runner.
docker run --rm -v "$PWD:/src" -v "$sdk:/sdk:ro" -w /src \
    -e USD_NAMESPACE -e PYTHON_MM -e CXX_STANDARD -e HOST_IDS="$(id -u):$(id -g)" \
    "$image" bash -c 'ci/build.sh /sdk "$1"; s=$?; [[ -e "$1" ]] && chown -R "$HOST_IDS" "$1"; exit $s' _ "$build"
