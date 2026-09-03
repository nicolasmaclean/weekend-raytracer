#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

PXR_NAMESPACE_OPEN_SCOPE


class HdWeekendInstancer final : public HdInstancer
{
public:
  HdWeekendInstancer(HdSceneDelegate *delegate, SdfPath const &id);
  ~HdWeekendInstancer() override = default;

  void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits) override;

  VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId);

private:
  void _SyncPrimvars(HdSceneDelegate *delegate, HdDirtyBits dirtyBits);

  TfHashMap<TfToken, VtValue, TfToken::HashFunctor> _primvars;
  bool _visible = true;
};


PXR_NAMESPACE_CLOSE_SCOPE

