# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

# weekend-raytracer/env.sh — source before any usd tool
export USD_SRC=$HOME/opt/OpenUSD
export USD_ROOT=$HOME/opt/usd_src_build
export USD_PY=$HOME/opt/usd-build-venv/bin/python

export PYTHONPATH=$USD_ROOT/lib/python${PYTHONPATH:+:$PYTHONPATH}
export LD_LIBRARY_PATH=$USD_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PATH=$USD_ROOT/bin:$PATH

# where step 6 installs to, and where Plug is told to look
export HDW_INSTALL=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-hydra/install
export PXR_PLUGINPATH_NAME=$HDW_INSTALL/plugin/usd/hdWeekend/resources

# --- Blender 4.5 flavour (docs/plans/blender-addon.md) -----------------
export BLENDER_LIB_DIR=$HOME/opt/lib-linux_x64          # sparse checkout, Step 0.2
export BLENDER_BIN=$HOME/opt/blender-4.5.13-linux-x64/blender
# $BLENDER is the wrapper, not the binary: sourcing this file puts the vanilla USD on
# LD_LIBRARY_PATH, which out-ranks Blender's own RUNPATH and breaks it. See blender/blender.sh.
export BLENDER=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/blender/blender.sh
export BLENDER_USD_NAMESPACE=pxrBlender_v25_02          # asserted by cmake, docs/notes/ci.md §2
export BLENDER_PYTHON=3.11                               # python/include/python3.11, Step A1
export HDW_BLENDER_INSTALL=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-hydra-blender/install

# $BPY_PYTHON is a wrapper for the same reason $BLENDER is: the bpy wheel is the same Blender
# build, so a sourced shell breaks `import bpy` identically. See blender/bpy.sh.
export BPY_PYTHON=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/blender/bpy.sh

# Do NOT put the Blender USD on LD_LIBRARY_PATH or PYTHONPATH — two USD builds with
# different internal namespaces on one path is exactly what the namespace rename prevents.
