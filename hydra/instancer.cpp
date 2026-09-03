#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/usd/sdf/path.h>

#include "instancer.h"

PXR_NAMESPACE_OPEN_SCOPE


namespace
{
template <typename Array, typename Out> bool _SampleAs(const VtValue &v, size_t i, Out *out)
{
  if (!v.IsHolding<Array>()) return false;

  const auto &a = v.UncheckedGet<Array>();
  if (i >= a.size()) return false;

  *out = Out(a[i]);
  return true;
}

bool _SampleVec3(const VtValue &v, size_t i, GfVec3d *out)
{
  return _SampleAs<VtVec3fArray>(v, i, out) || _SampleAs<VtVec3dArray>(v, i, out) ||
         _SampleAs<VtVec3hArray>(v, i, out);
}

bool _SampleQuat(const VtValue &v, size_t i, GfQuatd *out)
{
  if (_SampleAs<VtQuathArray>(v, i, out) || _SampleAs<VtQuatfArray>(v, i, out) ||
      _SampleAs<VtQuatdArray>(v, i, out))
  {
    return true;
  }

  // The raw <real, i, j, k> vec4 spelling hd/instancer.h documents.
  GfVec4f q;
  if (!_SampleAs<VtVec4fArray>(v, i, &q)) return false;
  *out = GfQuatd(q[0], GfVec3d(q[1], q[2], q[3]));
  return true;
}

bool _SampleMat4(const VtValue &v, size_t i, GfMatrix4d *out)
{
  return _SampleAs<VtMatrix4dArray>(v, i, out) || _SampleAs<VtMatrix4fArray>(v, i, out);
}
} // namespace


HdWeekendInstancer::HdWeekendInstancer(HdSceneDelegate *delegate, SdfPath const &id)
    : HdInstancer(delegate, id)
{
}

void HdWeekendInstancer::Sync(HdSceneDelegate *delegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits)
{
  if (*dirtyBits & HdChangeTracker::DirtyVisibility)
  {
    _visible = delegate->GetVisible(GetId());
  }

  _UpdateInstancer(delegate, dirtyBits);

  if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId()))
  {
    _SyncPrimvars(delegate, *dirtyBits);
  }
}

void HdWeekendInstancer::_SyncPrimvars(HdSceneDelegate *delegate, HdDirtyBits dirtyBits)
{
  SdfPath const &id = GetId();
  for (auto const &pv : delegate->GetPrimvarDescriptors(id, HdInterpolationInstance))
  {
    if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name))
    {
      continue;
    }

    VtValue value = delegate->Get(id, pv.name);
    if (!value.IsEmpty())
    {
      _primvars[pv.name] = value;
    }
  }
}

VtMatrix4dArray HdWeekendInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId)
{
  // Per instance, in row-vector order (v' = v * M), so the leftmost factor is
  // applied first:
  //   instanceTransforms * scales * rotations * translations * instancerTransform
  // Anything the delegate does not provide is the identity.
  if (!_visible) return {};

  const GfMatrix4d instancerTransform = GetDelegate()->GetInstancerTransform(GetId());
  const VtIntArray instanceIndices = GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

  VtMatrix4dArray transforms(instanceIndices.size(), instancerTransform);

  const VtValue &translations = _primvars[HdInstancerTokens->instanceTranslations];
  const VtValue &rotations = _primvars[HdInstancerTokens->instanceRotations];
  const VtValue &scales = _primvars[HdInstancerTokens->instanceScales];
  const VtValue &matrices = _primvars[HdInstancerTokens->instanceTransforms];

  for (size_t i = 0; i < instanceIndices.size(); i++)
  {
    const auto index = size_t(instanceIndices[i]);

    GfVec3d translate;
    if (_SampleVec3(translations, index, &translate))
    {
      GfMatrix4d m(1.0);
      m.SetTranslate(translate);
      transforms[i] = m * transforms[i];
    }

    GfQuatd rotate;
    if (_SampleQuat(rotations, index, &rotate))
    {
      GfMatrix4d m(1.0);
      m.SetRotate(rotate);
      transforms[i] = m * transforms[i];
    }

    GfVec3d scale;
    if (_SampleVec3(scales, index, &scale))
    {
      GfMatrix4d m(1.0);
      m.SetScale(scale);
      transforms[i] = m * transforms[i];
    }

    GfMatrix4d instanceTransform;
    if (_SampleMat4(matrices, index, &instanceTransform))
    {
      transforms[i] = instanceTransform * transforms[i];
    }
  }

  if (GetParentId().IsEmpty()) return transforms;

  auto *parent =
      static_cast<HdWeekendInstancer *>(GetDelegate()->GetRenderIndex().GetInstancer(GetParentId()));
  if (!TF_VERIFY(parent)) return transforms;

  // Nested instancing flattens to the cartesian product: child transform first,
  // then the parent's.
  const VtMatrix4dArray parentTransforms = parent->ComputeInstanceTransforms(GetId());

  VtMatrix4dArray flat(parentTransforms.size() * transforms.size());
  for (size_t i = 0; i < parentTransforms.size(); i++)
  {
    for (size_t j = 0; j < transforms.size(); j++)
    {
      flat[i * transforms.size() + j] = transforms[j] * parentTransforms[i];
    }
  }
  return flat;
}


PXR_NAMESPACE_CLOSE_SCOPE
