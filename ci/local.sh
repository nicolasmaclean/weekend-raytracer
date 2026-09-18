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

venv=build/venv-$TARGET_ID
[[ -x "$venv/bin/python" ]] || {
    uv venv --python "$PYTHON_MM" "$venv"
    uv pip install --python "$venv/bin/python" "bpy==$BPY_VERSION"
}
HDW_PLUGIN_DIR=$PWD/$build/install/plugin/usd HDW_SMOKE_TIMEOUT=60 \
    "$venv/bin/python" blender/smoke_test.py      # HDW_SMOKE_DIGEST comes from targets.py env

export HDW_INSTALL_DIR=$build/install PLATFORMS=linux-x64 \
    ZIP_NAME=weekend_raytracer-$ADDON_VERSION-blender-$TARGET_ID-linux-x64.zip
export BLENDER_EXT=$(find "$venv" -path '*/bl_pkg/cli/blender_ext.py' | head -1)
[[ -n "$BLENDER_EXT" ]] || { echo "blender_ext.py not found in bpy wheel" >&2; exit 1; }
blender/build_zip.sh
"$venv/bin/python" "$BLENDER_EXT" validate "dist/$ZIP_NAME"
