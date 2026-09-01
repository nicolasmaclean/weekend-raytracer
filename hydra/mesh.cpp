// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/meshUtil.h>

#include "tracer/material.h"

#include "convert.h"
#include "mesh.h"
#include "renderParam.h"

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

  if (pointsDirty)
  {
    _points = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
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

  // case: on first sync
  if (!_mesh)
  {
    _mesh = make_shared<mesh>();
    _mesh->mat = make_shared<lambert>(color(0.8, 0.8, 0.8));
    _instance = make_shared<instance>(_mesh, ToMat4(_transform));
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
    if (_handle == null_prim)
    {
      _handle = edit.insert(_instance);
    }

    // The primId AOV wants the id hydra picks with, not our slot handle.
    // HdRenderIndex has already pushed DirtyPrimID into _primId by now, and
    // re-stamping an int is cheaper than testing the bit for it.
    edit.set_prim_id(_handle, GetPrimId());

    if (pointsDirty)
    {
      _mesh->verts = std::move(verts);
    }

    if (topologyDirty)
    {
      _mesh->tris = std::move(tris);
      _mesh->face = std::move(face);
    }

    if (transformDirty)
    {
      _instance->set_transform(ToMat4(_transform));
    }

    edit.set_visible(_handle, _sharedData.visible);
  }

  *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdWeekendMesh::Finalize(HdRenderParam *renderParam)
{
  if (_handle == null_prim) return;

  auto *param = static_cast<HdWeekendRenderParam *>(renderParam);
  scene_edit edit = param->AcquireSceneForEdit();
  edit.remove(_handle);
  _handle = null_prim;
}

PXR_NAMESPACE_CLOSE_SCOPE

