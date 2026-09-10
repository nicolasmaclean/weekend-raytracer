#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch Blender 4.5 with the vanilla-USD environment stripped back out.
#
# env.sh puts our own USD build on LD_LIBRARY_PATH/PYTHONPATH. Blender's libs are found
# through RUNPATH ($ORIGIN), which the dynamic loader consults *after* LD_LIBRARY_PATH, so
# a sourced shell hands Blender our libraries instead of its own. They collide by soname:
#
#   blender  lib/libMaterialXCore.so.1  ->  1.39.2, namespace MaterialX_v1_39_2
#   vanilla  lib/libMaterialXCore.so.1  ->  1.39.4, namespace MaterialX_v1_39_4
#
# Blender then dies before main() with
#   symbol lookup error: lib/libusd_ms.so: undefined symbol: _ZTIN17MaterialX_v1_39_27ElementE
#
# PXR_PLUGINPATH_NAME is dropped for the same reason one level up: it points at the vanilla
# hdWeekend build, which is linked against the wrong USD namespace. The Blender-flavour
# plugin is located by HDW_PLUGIN_DIR instead (Step B3), which is passed through untouched.
#
# See docs/plans/blender-addon.md, Step 0.4 / GATE 0.

exec env -u LD_LIBRARY_PATH -u PYTHONPATH -u PXR_PLUGINPATH_NAME \
  "${BLENDER_BIN:-$HOME/opt/blender-4.5.13-linux-x64/blender}" "$@"
