#!/usr/bin/env bash
set -e

cd "$(dirname "${BASH_SOURCE[0]}")/.."
source env.sh

cat > /tmp/empty.usda <<'EOF'
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World" {}
EOF

cmake --build build-hydra --target install -j
$USD_PY $USD_ROOT/bin/usdrecord --renderer Weekend --disableGpu --imageWidth 400 /tmp/empty.usda /tmp/out.png
xdg-open /tmp/out.png
