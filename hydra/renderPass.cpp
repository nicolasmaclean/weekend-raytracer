//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "renderPass.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

HdWeekendRenderPass::HdWeekendRenderPass(
    HdRenderIndex *index,
    HdRprimCollection const &collection)
    : HdRenderPass(index, collection)
{
}

HdWeekendRenderPass::~HdWeekendRenderPass()
{
    std::cout << "Destroying renderPass" << std::endl;
}

void
HdWeekendRenderPass::_Execute(
    HdRenderPassStateSharedPtr const& renderPassState,
    TfTokenVector const &renderTags)
{
    std::cout << "=> Execute RenderPass" << std::endl;
}

PXR_NAMESPACE_CLOSE_SCOPE
