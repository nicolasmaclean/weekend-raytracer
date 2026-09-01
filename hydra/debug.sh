#!/usr/bin/env bash
set -e

cd "$(dirname "${BASH_SOURCE[0]}")/.."
source env.sh

# A cube as a UsdGeomMesh, not a UsdGeomCube: an implicit surface arrives as a
# native `cube` prim type until a scene index converts it (step D1).
cat > /tmp/cube.usda <<'EOF'
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Camera "Cam"
    {
        float focalLength = 24
        float horizontalAperture = 20.955
        float verticalAperture = 15.2908
        float2 clippingRange = (0.1, 1000)
        double3 xformOp:translate = (6, 5, 9)
        double3 xformOp:rotateXYZ = (-22, 32, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def Mesh "Cube"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [
            0, 1, 3, 2,
            4, 6, 7, 5,
            0, 4, 5, 1,
            2, 3, 7, 6,
            0, 2, 6, 4,
            1, 5, 7, 3
        ]
        point3f[] points = [
            (-1, -1, -1), (-1, -1, 1), (-1, 1, -1), (-1, 1, 1),
            ( 1, -1, -1), ( 1, -1, 1), ( 1, 1, -1), ( 1, 1, 1)
        ]
        uniform token subdivisionScheme = "none"
    }
}
EOF

cmake --build build-hydra --target install -j
$USD_PY $USD_ROOT/bin/usdrecord --renderer Weekend --disableGpu --imageWidth 400 --camera /World/Cam /tmp/cube.usda /tmp/out.png
xdg-open /tmp/out.png
