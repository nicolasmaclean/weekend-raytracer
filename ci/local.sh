#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run one ci/targets.toml row here, as .github/workflows/blender.yml runs it: the same ci/ scripts,
# in the same order. Sequencing only - build logic belongs in the scripts, so CI runs it too.
# See docs/plans/blender-ci.md Step B6.
set -euo pipefail
cd "$(dirname "$0")/.."
# CI never sources env.sh. A sourced shell's vanilla USD on LD_LIBRARY_PATH breaks bpy (blender/bpy.sh).
unset LD_LIBRARY_PATH PXR_PLUGINPATH_NAME
eval "$(ci/targets.py env "$1")"
sdk=build/sdk/$TARGET_ID build=build/$TARGET_ID

[[ -d "$sdk" ]] || ci/provision_sdk.sh "$sdk"      # the local stand-in for actions/cache
ci/verify_sdk.sh "$sdk"
ci/docker_build.sh "$sdk" "$build"
ci/check_so.sh "$build/install/plugin/usd/hdWeekend.so"
# Step C1 appends the bpy install + smoke test, Step D1 the package step.
