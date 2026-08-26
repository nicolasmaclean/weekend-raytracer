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
