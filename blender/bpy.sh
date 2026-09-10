#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run the .venv-bpy45 interpreter with the vanilla-USD environment stripped back out.
#
# The `bpy` wheel is the same Blender build as the tarball, so it fails the same way and for
# the same reason as blender/blender.sh — see the collision table there. In short: env.sh puts
# our own USD build on LD_LIBRARY_PATH, the loader consults that *before* the wheel's RUNPATH
# ($ORIGIN = site-packages/bpy/lib), and `import bpy` dies with
#   ImportError: .../bpy/lib/libusd_ms.so: undefined symbol: _ZTIN17MaterialX_v1_39_27ElementE
# because vanilla MaterialX 1.39.4 answers the soname that bpy's 1.39.2 was linked against.
#
# See docs/plans/blender-addon.md, Step F1.

exec env -u LD_LIBRARY_PATH -u PYTHONPATH -u PXR_PLUGINPATH_NAME \
  "${BPY_PYTHON_BIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.venv-bpy45/bin/python}" "$@"
