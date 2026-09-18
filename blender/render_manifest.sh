#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Render blender_manifest.toml.in. Used by the dev install (docs/plans/blender-addon.md Step B3)
# and by build_zip.sh, so the two can never disagree. Defaults are the Blender 4.5 row;
# CI sets every variable from ci/targets.toml.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sed -e "s|@ADDON_VERSION@|${ADDON_VERSION:-0.4.0}|" \
    -e "s|@BLENDER_VERSION_MIN@|${BLENDER_VERSION_MIN:-4.5.0}|" \
    -e "s|@BLENDER_VERSION_MAX@|${BLENDER_VERSION_MAX:-5.0.0}|" \
    -e "s|@PLATFORMS@|${PLATFORMS:-linux-x64}|" \
    "$here/weekend_raytracer/blender_manifest.toml.in" > "$1"
# Remove the bad render too: a manifest left on disk is one Blender would skip in silence.
if grep -q "@[A-Z_]*@" "$1"; then echo "unrendered placeholder in $1" >&2; rm -f "$1"; exit 1; fi
