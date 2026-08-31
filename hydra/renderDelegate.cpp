// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include <iostream>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/tokens.h>

#include "renderDelegate.h"
#include "convert.h"
#include "mesh.h"
#include "renderBuffer.h"
#include "renderPass.h"

PXR_NAMESPACE_OPEN_SCOPE


const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_RPRIM_TYPES = {
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_SPRIM_TYPES = {
    HdPrimTypeTokens->camera,
};

const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_BPRIM_TYPES = {
    HdPrimTypeTokens->renderBuffer,
};

HdWeekendRenderDelegate::HdWeekendRenderDelegate() : HdRenderDelegate()
{
  _Initialize();
}

HdWeekendRenderDelegate::HdWeekendRenderDelegate(HdRenderSettingsMap const &settingsMap)
    : HdRenderDelegate(settingsMap)
{
  _Initialize();
}

void HdWeekendRenderDelegate::_Initialize()
{
  std::cout << "Creating Weekend RenderDelegate" << std::endl;
  _resourceRegistry = std::make_shared<HdResourceRegistry>();
}

HdWeekendRenderDelegate::~HdWeekendRenderDelegate()
{
  _resourceRegistry.reset();
  std::cout << "Destroying Weekend RenderDelegate" << std::endl;
}

TfTokenVector const &HdWeekendRenderDelegate::GetSupportedRprimTypes() const
{
  return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const &HdWeekendRenderDelegate::GetSupportedSprimTypes() const
{
  return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const &HdWeekendRenderDelegate::GetSupportedBprimTypes() const
{
  return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr HdWeekendRenderDelegate::GetResourceRegistry() const
{
  return _resourceRegistry;
}

void HdWeekendRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
  std::cout << "=> CommitResources RenderDelegate" << std::endl;
}

HdRenderPassSharedPtr HdWeekendRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                                                                HdRprimCollection const &collection)
{
  std::cout << "Create RenderPass with Collection=" << collection.GetName() << std::endl;

  return HdRenderPassSharedPtr(
      new HdWeekendRenderPass(index, collection, &_renderThread, &_renderer, &_sceneVersion));
}

HdRprim *HdWeekendRenderDelegate::CreateRprim(TfToken const &typeId, SdfPath const &rprimId)
{
  std::cout << "Create Weekend Rprim type=" << typeId.GetText() << " id=" << rprimId << std::endl;

  if (typeId == HdPrimTypeTokens->mesh)
  {
    return new HdWeekendMesh(rprimId);
  }

  TF_CODING_ERROR("Unknown Rprim type=%s id=%s", typeId.GetText(), rprimId.GetText());

  return nullptr;
}

void HdWeekendRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
  std::cout << "Destroy Weekend Rprim id=" << rPrim->GetId() << std::endl;
  delete rPrim;
}

// HdCamera is concrete and needs no subclass: the render pass takes the view and
// projection matrices from HdRenderPassState, never from the Sprim.
HdSprim *HdWeekendRenderDelegate::CreateSprim(TfToken const &typeId, SdfPath const &sprimId)
{
  if (typeId == HdPrimTypeTokens->camera)
  {
    return new HdCamera(sprimId);
  }

  TF_CODING_ERROR("Unknown Sprim type=%s id=%s", typeId.GetText(), sprimId.GetText());
  return nullptr;
}

HdSprim *HdWeekendRenderDelegate::CreateFallbackSprim(TfToken const &typeId)
{
  // A fallback prim is bound to the empty path and never syncs.
  if (typeId == HdPrimTypeTokens->camera)
  {
    return new HdCamera(SdfPath::EmptyPath());
  }

  TF_CODING_ERROR("Creating unknown fallback sprim type=%s", typeId.GetText());
  return nullptr;
}

void HdWeekendRenderDelegate::DestroySprim(HdSprim *sPrim)
{
  delete sPrim;
}

HdBprim *HdWeekendRenderDelegate::CreateBprim(TfToken const &typeId, SdfPath const &bprimId)
{
  if (typeId == HdPrimTypeTokens->renderBuffer)
  {
    return new HdWeekendRenderBuffer(bprimId);
  }

  TF_CODING_ERROR("Unknown Bprim type=%s id=%s", typeId.GetText(), bprimId.GetText());
  return nullptr;
}

HdBprim *HdWeekendRenderDelegate::CreateFallbackBprim(TfToken const &typeId)
{
  if (typeId == HdPrimTypeTokens->renderBuffer)
  {
    return new HdWeekendRenderBuffer(SdfPath::EmptyPath());
  }

  TF_CODING_ERROR("Creating unknown fallback bprim type=%s", typeId.GetText());
  return nullptr;
}

void HdWeekendRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
  delete bPrim;
}

HdInstancer *HdWeekendRenderDelegate::CreateInstancer(HdSceneDelegate *delegate, SdfPath const &id)
{
  TF_CODING_ERROR("Creating Instancer not supported id=%s", id.GetText());
  return nullptr;
}

void HdWeekendRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
  TF_CODING_ERROR("Destroy instancer not supported");
}

HdRenderParam *HdWeekendRenderDelegate::GetRenderParam() const
{
  return nullptr;
}

// The clear value's type has to match the format, or the host's clear is a no-op.
static VtValue _ToClearValue(aov_descriptor const &d)
{
  if (component_of(d.format) == component_type::int32)
  {
    return VtValue(int(d.clear_value[0]));
  }

  switch (component_count(d.format))
  {
  case 1: return VtValue(d.clear_value[0]);
  case 2: return VtValue(GfVec2f(d.clear_value[0], d.clear_value[1]));
  case 3: return VtValue(GfVec3f(d.clear_value[0], d.clear_value[1], d.clear_value[2]));
  default: return VtValue(GfVec4f(d.clear_value[0], d.clear_value[1], d.clear_value[2], d.clear_value[3]));
  }
}

// One AOV table, shared with the cli and the viewer. An HdFormatInvalid answer is
// how a host learns we do not support an AOV, so unrecognised names return the
// default-constructed descriptor rather than erroring.
HdAovDescriptor HdWeekendRenderDelegate::GetDefaultAovDescriptor(TfToken const &name) const
{
  aov which;
  if (!ToAov(name, &which))
  {
    return {};
  }

  const aov_descriptor d = default_aov_descriptor(which);
  return HdAovDescriptor(static_cast<HdFormat>(int(d.format)), d.multisampled, _ToClearValue(d));
}

PXR_NAMESPACE_CLOSE_SCOPE

