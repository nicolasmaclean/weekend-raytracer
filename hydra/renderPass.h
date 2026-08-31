// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
#pragma once

#include <atomic>

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rect2i.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderThread.h"

#include "renderBuffer.h"
#include "renderer.h"

PXR_NAMESPACE_OPEN_SCOPE


/// \class HdWeekendRenderPass
///
/// HdRenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.
///
class HdWeekendRenderPass final : public HdRenderPass
{
public:
  /// Renderpass constructor.
  ///   \param index The render index containing scene data to render.
  ///   \param collection The initial rprim collection for this renderpass.
  ///   \param renderThread The delegate's render thread (unused until stage C).
  ///   \param renderer The delegate's renderer, shared by every pass.
  ///   \param sceneVersion The delegate's scene edit counter.
  HdWeekendRenderPass(HdRenderIndex *index, HdRprimCollection const &collection, HdRenderThread *renderThread,
                      HdWeekendRenderer *renderer, std::atomic<int> *sceneVersion);

  /// Renderpass destructor.
  ~HdWeekendRenderPass() override;

  /// Has the image stopped changing?
  bool IsConverged() const override;

protected:
  /// Draw the scene with the bound renderpass state.
  ///   \param renderPassState Input parameters (including viewer parameters)
  ///                          for this renderpass.
  ///   \param renderTags Which rendertags should be drawn this pass.
  void _Execute(HdRenderPassStateSharedPtr const &renderPassState, TfTokenVector const &renderTags) override;

private:
  // Owned by the render delegate, which outlives every pass it creates.
  HdRenderThread *_renderThread;
  HdWeekendRenderer *_renderer;
  std::atomic<int> *_sceneVersion;

  // The state this pass last reconciled against. _Execute is a diff, not a
  // draw call: anything that changed here restarts the render.
  int _lastSceneVersion;
  int _lastSettingsVersion;
  GfMatrix4d _viewMatrix;
  GfMatrix4d _projMatrix;
  GfRect2i _dataWindow;
  HdRenderPassAovBindingVector _aovBindings;

  // Synthesized when the host binds no AOVs at all - legal input, but never a
  // legal render state. Owned by the pass, which is why the destructor has to
  // stop the render thread before they go away (stage C).
  HdWeekendRenderBuffer _colorBuffer;
  HdWeekendRenderBuffer _depthBuffer;

  // IsConverged()'s answer while rendering into the fallbacks above, which the
  // host cannot ask about itself.
  bool _converged;
};

PXR_NAMESPACE_CLOSE_SCOPE

