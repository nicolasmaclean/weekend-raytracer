#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sparse-clone Blender's lib-linux_x64 at the row's pinned commit into <dir>.
# Reads LIB_BRANCH, LIB_SHA (ci/targets.py env <id>). See docs/plans/blender-ci.md Step B1.
set -euo pipefail
dir=$1
git lfs install --skip-repo      # without this, checkout silently leaves 133-byte pointer files
git clone --filter=blob:none --no-checkout --branch "$LIB_BRANCH" \
    https://projects.blender.org/blender/lib-linux_x64.git "$dir"
git -C "$dir" sparse-checkout set usd tbb python/include
git -C "$dir" checkout --detach "$LIB_SHA"
