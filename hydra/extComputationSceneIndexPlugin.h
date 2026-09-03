#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE


/// \class HdWeekend_ExtComputationSceneIndexPlugin
///
/// Resolves ext-computation-driven primvars into plain arrays before they reach
/// HdWeekendMesh::Sync, which only knows how to read `points` as a
/// VtVec3fArray. Skinned meshes are the case that matters: without this their
/// `points` stay computation-backed and the mesh renders in its bind pose.
///
class HdWeekend_ExtComputationSceneIndexPlugin final : public HdSceneIndexPlugin
{
public:
  HdWeekend_ExtComputationSceneIndexPlugin();

protected:
  HdSceneIndexBaseRefPtr _AppendSceneIndex(const HdSceneIndexBaseRefPtr &inputScene,
                                           const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
