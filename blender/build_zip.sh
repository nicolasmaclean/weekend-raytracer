#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Package the add-on and hdWeekend.so as an extension zip. Defaults reproduce the local Blender
# 4.5 build; CI sets every variable from ci/targets.toml (docs/plans/blender-ci.md Step A3).
set -euo pipefail

: "${HDW_INSTALL_DIR:=build-hydra-blender/install}"
: "${DIST_DIR:=$PWD/dist}"

STAGE=$(mktemp -d)/weekend_raytracer
mkdir -p "$STAGE/plugin/usd" "$DIST_DIR"
cp blender/weekend_raytracer/*.py "$STAGE/"
blender/render_manifest.sh "$STAGE/blender_manifest.toml"

# Mirror cmake's install layout exactly: the .so is a SIBLING of hdWeekend/, because
# plugInfo.json says LibraryPath "../hdWeekend.so" with Root "..".
cp -r "$HDW_INSTALL_DIR/plugin/usd/." "$STAGE/plugin/usd/"

# BLENDER_EXT: path to blender_ext.py - stdlib-only, ships in every bpy wheel, so CI needs no
# Blender app. Falls back to the app's own `--command extension` for the local dev loop.
if [[ -n "${BLENDER_EXT:-}" ]]; then
    ext=(python3 "$BLENDER_EXT")
else
    ext=("${BLENDER:?set BLENDER (the app) or BLENDER_EXT (blender_ext.py)}" --command extension)
fi

# CI names each zip per row; the manifest's default {id}-{version}.zip would collide in a release.
if [[ -n "${ZIP_NAME:-}" ]]; then
    out=(--output-filepath "$DIST_DIR/$ZIP_NAME")
else
    out=(--output-dir "$DIST_DIR")
fi
"${ext[@]}" build --source-dir "$STAGE" "${out[@]}"
