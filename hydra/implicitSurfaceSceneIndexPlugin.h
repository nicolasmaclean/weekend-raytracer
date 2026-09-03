#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE


/// \class HdWeekend_ImplicitSurfaceSceneIndexPlugin
///
/// Narrows the input language to what the renderer supports (§16). Hydra hands
/// a delegate `sphere`, `cube`, `cone`, `cylinder`, `capsule` and `plane` as
/// native prim types unless it is asked to convert them, and `mesh` is the only
/// Rprim type in GetSupportedRprimTypes() - so without this every implicit
/// surface in an asset hits CreateRprim's TF_CODING_ERROR.
///
/// Every type is mapped to "toMesh", spheres included: see the plan's
/// "analytic-sphere decision". tracer/sphere.h has a real analytic
/// sphere::hit, but a transformed UsdGeomSphere is an ellipsoid, which that
/// hit cannot represent, so a native sphere Rprim would need the `instance`
/// wrapper anyway and the win shrinks to one ray-quadric test against one
/// ray-triangle test.
///
class HdWeekend_ImplicitSurfaceSceneIndexPlugin final : public HdSceneIndexPlugin
{
public:
  HdWeekend_ImplicitSurfaceSceneIndexPlugin();

protected:
  HdSceneIndexBaseRefPtr _AppendSceneIndex(const HdSceneIndexBaseRefPtr &inputScene,
                                           const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
