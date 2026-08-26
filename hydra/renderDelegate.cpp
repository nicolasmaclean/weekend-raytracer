//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "renderDelegate.h"
#include "mesh.h"
#include "renderPass.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
};

const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
};

HdWeekendRenderDelegate::HdWeekendRenderDelegate()
    : HdRenderDelegate()
{
    _Initialize();
}

HdWeekendRenderDelegate::HdWeekendRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void
HdWeekendRenderDelegate::_Initialize()
{
    std::cout << "Creating Weekend RenderDelegate" << std::endl;
    _resourceRegistry = std::make_shared<HdResourceRegistry>();
}

HdWeekendRenderDelegate::~HdWeekendRenderDelegate()
{
    _resourceRegistry.reset();
    std::cout << "Destroying Weekend RenderDelegate" << std::endl;
}

TfTokenVector const&
HdWeekendRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdWeekendRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdWeekendRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdWeekendRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

void 
HdWeekendRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
    std::cout << "=> CommitResources RenderDelegate" << std::endl;
}

HdRenderPassSharedPtr 
HdWeekendRenderDelegate::CreateRenderPass(
    HdRenderIndex *index,
    HdRprimCollection const& collection)
{
    std::cout << "Create RenderPass with Collection=" 
        << collection.GetName() << std::endl; 

    return HdRenderPassSharedPtr(new HdWeekendRenderPass(index, collection));  
}

HdRprim *
HdWeekendRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId)
{
    std::cout << "Create Weekend Rprim type=" << typeId.GetText() 
        << " id=" << rprimId 
        << std::endl;

    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdWeekendMesh(rprimId);
    } else {
        TF_CODING_ERROR("Unknown Rprim type=%s id=%s", 
            typeId.GetText(), 
            rprimId.GetText());
    }
    return nullptr;
}

void
HdWeekendRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    std::cout << "Destroy Weekend Rprim id=" << rPrim->GetId() << std::endl;
    delete rPrim;
}

HdSprim *
HdWeekendRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    TF_CODING_ERROR("Unknown Sprim type=%s id=%s", 
        typeId.GetText(), 
        sprimId.GetText());
    return nullptr;
}

HdSprim *
HdWeekendRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    TF_CODING_ERROR("Creating unknown fallback sprim type=%s", 
        typeId.GetText()); 
    return nullptr;
}

void
HdWeekendRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    TF_CODING_ERROR("Destroy Sprim not supported");
}

HdBprim *
HdWeekendRenderDelegate::CreateBprim(TfToken const& typeId, SdfPath const& bprimId)
{
    TF_CODING_ERROR("Unknown Bprim type=%s id=%s", 
        typeId.GetText(), 
        bprimId.GetText());
    return nullptr;
}

HdBprim *
HdWeekendRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    TF_CODING_ERROR("Creating unknown fallback bprim type=%s", 
        typeId.GetText()); 
    return nullptr;
}

void
HdWeekendRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    TF_CODING_ERROR("Destroy Bprim not supported");
}

HdInstancer *
HdWeekendRenderDelegate::CreateInstancer(
    HdSceneDelegate *delegate,
    SdfPath const& id)
{
    TF_CODING_ERROR("Creating Instancer not supported id=%s", 
        id.GetText());
    return nullptr;
}

void 
HdWeekendRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    TF_CODING_ERROR("Destroy instancer not supported");
}

HdRenderParam *
HdWeekendRenderDelegate::GetRenderParam() const
{
    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
