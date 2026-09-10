#!/usr/bin/env bash
set -euo pipefail

STAGE=$(mktemp -d)/weekend_raytracer
mkdir -p "$STAGE"
cp blender/weekend_raytracer/*.py "$STAGE/"

sed -e "s|@BLENDER_VERSION_MIN@|4.5.0|" \
    -e "s|@BLENDER_VERSION_MAX@|5.0.0|" \
    -e "s|@PLATFORMS@|linux-x64|" \
    blender/weekend_raytracer/blender_manifest.toml.in > "$STAGE/blender_manifest.toml"

# Mirror cmake's install layout exactly: the .so is a SIBLING of hdWeekend/, because
# plugInfo.json says LibraryPath "../hdWeekend.so" with Root "..".
mkdir -p "$STAGE/plugin/usd"
cp -r build-hydra-blender/install/plugin/usd/. "$STAGE/plugin/usd/"

mkdir -p "$PWD/dist"
$BLENDER --command extension build --source-dir "$STAGE" --output-dir "$PWD/dist"

