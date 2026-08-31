// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderPassState.h"
#include "pxr/imaging/hd/tokens.h"

#include "renderPass.h"

PXR_NAMESPACE_OPEN_SCOPE

HdWeekendRenderPass::HdWeekendRenderPass(
    HdRenderIndex *index,
    HdRprimCollection const &collection,
    HdRenderThread *renderThread,
    HdWeekendRenderer *renderer,
    std::atomic<int> *sceneVersion)
    : HdRenderPass(index, collection)
    , _renderThread(renderThread)
    , _renderer(renderer)
    , _sceneVersion(sceneVersion)
    , _lastSceneVersion(0)
    , _lastSettingsVersion(0)
    , _viewMatrix(1.0)   // == identity
    , _projMatrix(1.0)   // == identity
    , _aovBindings()
    , _colorBuffer(SdfPath::EmptyPath())
    , _depthBuffer(SdfPath::EmptyPath())
    , _converged(false)
{
}

HdWeekendRenderPass::~HdWeekendRenderPass()
{
    // The render thread may still be writing into _colorBuffer/_depthBuffer,
    // which die with this object. No-op until stage C starts the thread.
    _renderThread->StopRender();
}

bool
HdWeekendRenderPass::IsConverged() const
{
    // With no host bindings the render went into the pass's own buffers, which
    // the host never sees, so answer from the cached flag instead.
    if (_aovBindings.empty()) {
        return _converged;
    }

    for (size_t i = 0; i < _aovBindings.size(); ++i) {
        if (_aovBindings[i].renderBuffer &&
            !_aovBindings[i].renderBuffer->IsConverged()) {
            return false;
        }
    }
    return true;
}

// usdview drives the framing API; older hosts and some test harnesses still use
// the viewport. Both have to work, and which one we got decides whether the
// fallback buffers below get allocated.
static
GfRect2i
_GetDataWindow(HdRenderPassStateSharedPtr const& renderPassState)
{
    const CameraUtilFraming &framing = renderPassState->GetFraming();
    if (framing.IsValid()) {
        return framing.dataWindow;
    } else {
        const GfVec4f vp = renderPassState->GetViewport();
        return GfRect2i(GfVec2i(0), int(vp[2]), int(vp[3]));
    }
}

void
HdWeekendRenderPass::_Execute(
    HdRenderPassStateSharedPtr const& renderPassState,
    TfTokenVector const &renderTags)
{
    // This is not "draw a frame". It is "reconcile the requested state against
    // the current state, and restart the render if anything changed". The order
    // is load-bearing: every check stops the render before it mutates the
    // renderer, and the restart happens once at the end.
    bool needStartRender = false;

    // 1. the scene
    const int currentSceneVersion = _sceneVersion->load();
    if (_lastSceneVersion != currentSceneVersion) {
        _lastSceneVersion = currentSceneVersion;
        needStartRender = true;
    }

    // 2. the render settings. Stage C re-reads the whole descriptor map here;
    //    for now the only one that matters is how many samples count as done.
    HdRenderDelegate *renderDelegate = GetRenderIndex()->GetRenderDelegate();
    const int currentSettingsVersion = renderDelegate->GetRenderSettingsVersion();
    if (_lastSettingsVersion != currentSettingsVersion) {
        _renderThread->StopRender();
        _lastSettingsVersion = currentSettingsVersion;

        // Stage A renders synchronously inside _Execute, so this has to stay at
        // 1: a multi-thousand-sample blocking render would hang the host.
        _renderer->SetSamplesToConvergence(1);

        needStartRender = true;
    }

    // 3. the camera
    const GfMatrix4d view = renderPassState->GetWorldToViewMatrix();
    const GfMatrix4d proj = renderPassState->GetProjectionMatrix();
    if (_viewMatrix != view || _projMatrix != proj) {
        _viewMatrix = view;
        _projMatrix = proj;

        _renderThread->StopRender();
        _renderer->SetCamera(_viewMatrix, _projMatrix);
        needStartRender = true;
    }

    // 4. the data window
    const GfRect2i dataWindow = _GetDataWindow(renderPassState);
    if (_dataWindow != dataWindow) {
        _dataWindow = dataWindow;

        _renderThread->StopRender();
        _renderer->SetDataWindow(dataWindow);

        // Only a host on the old viewport API can end up with no AOV bindings,
        // so that is the only case that needs the fallbacks sized.
        if (!renderPassState->GetFraming().IsValid()) {
            const GfVec3i dimensions(_dataWindow.GetWidth(),
                                     _dataWindow.GetHeight(),
                                     1);

            _colorBuffer.Allocate(dimensions,
                                  HdFormatUNorm8Vec4,
                                  /*multiSampled=*/true);

            _depthBuffer.Allocate(dimensions,
                                  HdFormatFloat32,
                                  /*multiSampled=*/false);
        }

        needStartRender = true;
    }

    // 5. the AOV bindings. Empty is legal input but never a legal render state,
    //    so synthesize color + depth against the pass's own buffers. The second
    //    clause forces the first pass through even when both are empty.
    HdRenderPassAovBindingVector aovBindings =
        renderPassState->GetAovBindings();
    if (_aovBindings != aovBindings || _renderer->GetAovBindings().empty()) {
        _aovBindings = aovBindings;

        _renderThread->StopRender();
        if (aovBindings.empty()) {
            HdRenderPassAovBinding colorAov;
            colorAov.aovName = HdAovTokens->color;
            colorAov.renderBuffer = &_colorBuffer;
            colorAov.clearValue =
                VtValue(GfVec4f(0.0707f, 0.0707f, 0.0707f, 1.0f));
            aovBindings.push_back(colorAov);

            HdRenderPassAovBinding depthAov;
            depthAov.aovName = HdAovTokens->depth;
            depthAov.renderBuffer = &_depthBuffer;
            depthAov.clearValue = VtValue(1.0f);
            aovBindings.push_back(depthAov);
        }
        _renderer->SetAovBindings(aovBindings);

        // Stage C's render thread clears; make sure it happens at least once on
        // this thread first.
        _renderer->Clear();
        needStartRender = true;
    }

    TF_VERIFY(!_renderer->GetAovBindings().empty(),
              "No aov bindings to render into");

    if (needStartRender) {
        _converged = false;
        _renderer->MarkAovBuffersUnconverged();

        // Blocking. Becomes _renderThread->StartRender() in stage C, at which
        // point _converged stops being set here and starts being read from the
        // thread's own progress.
        _renderer->Render(nullptr);
        _converged = true;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

