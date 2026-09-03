// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderDelegate.h>

#include "hydra/instancer.h"
#include "tracer/material.h"

#include "convert.h"
#include "mesh.h"
#include "renderParam.h"
#include "config.h"

PXR_NAMESPACE_OPEN_SCOPE


HdWeekendMesh::HdWeekendMesh(SdfPath const &id) : HdMesh(id) {}

HdDirtyBits HdWeekendMesh::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyPoints |
         HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
         HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals |
         HdChangeTracker::DirtyInstancer;
}

HdDirtyBits HdWeekendMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
  return bits;
}

void HdWeekendMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) {}

void HdWeekendMesh::Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits,
                         TfToken const &reprToken)
{
  SdfPath const &id = GetId();
  auto *param = static_cast<HdWeekendRenderParam *>(renderParam);

  // _UpdateVisibility (and maybe in future other calls) mutate dirty bits.
  // Save them now so we can query them again later safely
  const bool pointsDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
  const bool topologyDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
  const bool normalsDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals);
  const bool transformDirty = HdChangeTracker::IsTransformDirty(*dirtyBits, id);
  const bool visibilityDirty = HdChangeTracker::IsVisibilityDirty(*dirtyBits, id);
  const bool colorDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor);

  if (pointsDirty)
  {
    // A computation-backed `points` that no scene index resolved into a plain
    // array arrives as an empty VtValue, and Get<VtVec3fArray>() on that is
    // undefined behaviour rather than an error - which is how a skinned mesh
    // segfaults the delegate when the ext-computation scene index is missing.
    const VtValue value = sceneDelegate->Get(id, HdTokens->points);
    if (value.IsHolding<VtVec3fArray>())
    {
      _points = value.UncheckedGet<VtVec3fArray>();
    }
    else
    {
      TF_WARN("%s: `points` holds %s, not a Vec3fArray - dropping the geometry", id.GetText(),
              value.GetTypeName().c_str());
      _points = VtVec3fArray();
    }
  }
  if (topologyDirty)
  {
    _topology = GetMeshTopology(sceneDelegate);
  }
  if (normalsDirty)
  {
    /* authored normals, if any -> _normals */
  }
  if (transformDirty)
  {
    _transform = sceneDelegate->GetTransform(id); // GfMatrix4d
  }
  if (visibilityDirty)
  {
    _UpdateVisibility(sceneDelegate, dirtyBits); // sets _sharedData.visible
  }

  _UpdateInstancer(sceneDelegate, dirtyBits);
  HdInstancer::_SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), GetInstancerId());
  const bool instancerDirty = HdChangeTracker::IsInstancerDirty(*dirtyBits, id);

  const HdWeekendConfig &config = HdWeekendConfig::GetInstance();
  const bool enableSceneColors = config.enableSceneColors;
  if (colorDirty && enableSceneColors)
  {
    const VtValue v = sceneDelegate->Get(id, HdTokens->displayColor);
    if (v.IsHolding<VtVec3fArray>())
    {
      // Constant and vertex-interpolated collapse to the same thing here: element
      // 0. Per-vertex interpolation is 0.4.0 and needs hdEmbree's sampler.h.
      const auto &a = v.UncheckedGet<VtVec3fArray>();
      if (!a.empty()) _displayColor = a[0];
    }
    else if (v.IsHolding<GfVec3f>())
    {
      _displayColor = v.UncheckedGet<GfVec3f>();
    }
  }

  // case: on first sync
  const bool createdMesh = !_mesh;
  if (createdMesh)
  {
    _mesh = make_shared<mesh>();
    _mesh->mat = make_shared<lambert>(ToColor(_displayColor));
  }

  // An un-instanced prim is the N == 1 case with instance transform I, so the
  // insert loop below has exactly one shape.
  const bool xformsDirty = createdMesh || instancerDirty || transformDirty;
  VtMatrix4dArray transforms;
  if (xformsDirty)
  {
    if (GetInstancerId().IsEmpty())
    {
      transforms.push_back(GfMatrix4d(1.0));
    }
    else
    {
      auto *instancer =
          static_cast<HdWeekendInstancer *>(sceneDelegate->GetRenderIndex().GetInstancer(GetInstancerId()));
      if (TF_VERIFY(instancer))
      {
        transforms = instancer->ComputeInstanceTransforms(id);
      }
    }
  }

  std::vector<vec3> verts;
  if (pointsDirty)
  {
    verts.reserve(_points.size());
    for (const GfVec3f &p : _points)
    {
      verts.emplace_back(p[0], p[1], p[2]);
    }
  }

  std::vector<int32_t> tris, face;
  if (topologyDirty)
  {
    // Triangulate the coarse hull and skip subdivision entirely, as hdEmbree
    // does at refineLevel 0.
    // primitiveParams maps each generated triangle back to the face it was
    // authored as, which is what mesh::face exists for.
    HdMeshUtil util(&_topology, id);
    VtVec3iArray indices;
    VtIntArray params;
    util.ComputeTriangleIndices(&indices, &params);

    tris.reserve(indices.size() * 3);
    face.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); i++)
    {
      tris.push_back(indices[i][0]);
      tris.push_back(indices[i][1]);
      tris.push_back(indices[i][2]);

      // Push in emission order, never sort. mesh::commit reorders `geom` into
      // BVH order so `face` bridges the bvh ordered `geom` to tri_index.
      face.push_back(HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(params[i]));
    }
  }

  // publish changes to the sceneQ
  {
    scene_edit edit = param->AcquireSceneForEdit();

    if (pointsDirty)
    {
      _mesh->verts = std::move(verts);
    }

    if (topologyDirty)
    {
      _mesh->tris = std::move(tris);
      _mesh->face = std::move(face);
    }

    // tris index into verts, so a topology published without the points it was
    // authored against is a read past the end of `verts` in mesh::commit.
    if (_mesh->verts.empty())
    {
      _mesh->tris.clear();
      _mesh->face.clear();
    }

    if (colorDirty && !createdMesh)
    {
      _mesh->mat = make_shared<lambert>(ToColor(_displayColor));
    }

    if (xformsDirty)
    {
      // An instanced prim owns N slots, not one. Shrink first, then grow.
      for (size_t i = transforms.size(); i < _handles.size(); i++)
      {
        edit.remove(_handles[i]);
      }
      _handles.resize(transforms.size(), null_prim);
      _instances.resize(transforms.size());

      for (size_t i = 0; i < transforms.size(); i++)
      {
        // Row-vector convention, so the rprim's own transform applies first and
        // the instance transform after it. mat4 is element-identical to
        // GfMatrix4d, so do the multiply in Gf and convert once.
        const mat4 xf = ToMat4(_transform * transforms[i]);

        if (_instances[i])
        {
          // In place, for B2.3's reason squared: re-inserting would change N
          // prim pointers at once, which turns a TLAS refit into a guaranteed
          // rebuild rather than a merely likely one.
          _instances[i]->set_transform(xf);
          continue;
        }

        // Every instance shares one prototype mesh, and therefore one BLAS.
        _instances[i] = make_shared<instance>(_mesh, xf);
        _instances[i]->instance_id = int32_t(i);
        _handles[i] = edit.insert(_instances[i]);
      }
    }

    for (prim_handle h : _handles)
    {
      edit.set_prim_id(h, GetPrimId());
      edit.set_visible(h, _sharedData.visible);
    }
  }

  *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdWeekendMesh::Finalize(HdRenderParam *renderParam)
{
  if (_handles.empty()) return;

  auto *param = static_cast<HdWeekendRenderParam *>(renderParam);
  scene_edit edit = param->AcquireSceneForEdit();
  for (auto h : _handles)
  {
    edit.remove(h);
  }
  _handles.clear();
  _instances.clear();
}

PXR_NAMESPACE_CLOSE_SCOPE

