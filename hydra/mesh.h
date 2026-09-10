// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// SPDX-FileCopyrightText: 2025-2026 Nick Maclean
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Modifications to this file are licensed GPL-3.0-or-later. The original Pixar
// material remains under the Tomorrow Open Source Technology License 1.0.

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/base/gf/matrix4d.h>
#include <vector>

#include "tracer/instance.h"
#include "tracer/mesh.h"
#include "tracer/scene.h"

PXR_NAMESPACE_OPEN_SCOPE


class HdWeekendMesh final : public HdMesh
{
public:
  HF_MALLOC_TAG_NEW("new HdWeekendMesh");

  HdWeekendMesh(SdfPath const &id);

  ~HdWeekendMesh() override = default;

  // tell scene graph what data is needed in the first sync
  HdDirtyBits GetInitialDirtyBitsMask() const override;

  // setup or update the renderable representation.
  //
  // dirtyBits tells this function what data to update. Only use the buffers
  // marked dirty. Buffers use just-in-time data schemes, which means crashes
  // can happen if we touch don't respect dirtyBits.
  //
  // make sure this is threadsafe. HdSceneDelegate calls are ok.
  void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits,
            TfToken const &reprToken) override;

  void Finalize(HdRenderParam *renderParam) override;

protected:
  // Init representation of this Rprim. Called before sync, the first time
  // the repr is used,
  //
  // reprToken is the name of the repr to initalize.
  // dirtyBits is an in/out value. we can inject dirty bits here is desired
  //
  // InitRepr occurs before dirty bit propagation.
  // See HdRprim::InitRepr()
  void _InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) override;

  // This callback lets us inject dirty bits before they are passed to scene delegate
  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

  // This class does not support copying.
  HdWeekendMesh(const HdWeekendMesh &) = delete;
  HdWeekendMesh &operator=(const HdWeekendMesh &) = delete;

private:
  VtVec3fArray _points;
  HdMeshTopology _topology;
  GfMatrix4d _transform{1.0};
  // The albedo a mesh falls back to: nothing authored a displayColor, or scene
  // colours are switched off. Named because Sync now caches the authored value
  // unconditionally, so _displayColor no longer doubles as "the default".
  static constexpr GfVec3f kDefaultDisplayColor{0.8F, 0.8F, 0.8F};
  GfVec3f _displayColor{kDefaultDisplayColor};

  shared_ptr<mesh> _mesh;
  std::vector<shared_ptr<instance>> _instances;
  std::vector<prim_handle> _handles;
};

PXR_NAMESPACE_CLOSE_SCOPE

