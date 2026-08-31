// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include <iostream>

#include "mesh.h"

PXR_NAMESPACE_OPEN_SCOPE


HdWeekendMesh::HdWeekendMesh(SdfPath const& id) : HdMesh(id) {} 

HdDirtyBits HdWeekendMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean | HdChangeTracker::DirtyTransform;
}

HdDirtyBits HdWeekendMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void HdWeekendMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) { } 

void HdWeekendMesh::Sync(
    HdSceneDelegate *sceneDelegate,
    HdRenderParam   *renderParam,
    HdDirtyBits     *dirtyBits,
    TfToken const   &reprToken
)
{
    std::cout << "* (multithreaded) Sync Weekend Mesh id=" << GetId() << std::endl;
}

PXR_NAMESPACE_CLOSE_SCOPE

