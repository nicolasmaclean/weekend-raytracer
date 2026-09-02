#pragma once

#include <pxr/imaging/hd/renderThread.h>
#include <pxr/imaging/hd/renderDelegate.h>

#include "tracer/scene.h"

using namespace pxr;


class HdWeekendRenderParam final : public HdRenderParam
{
public:
  HdWeekendRenderParam(scene *s, HdRenderThread *thread, std::atomic<int> *sceneVersion)
      : _scene(s),
        _renderThread(thread),
        _sceneVersion(sceneVersion)
  {
  }

  // Stop the render thread without touching the scene. Render buffers are not
  // scene data, so they must not bump _sceneVersion - but they DO need the
  // render stopped before they are resized or destroyed, because the tracer
  // writes into their memory directly from the render thread.
  //
  // StopRender() blocks until the in-flight callback returns: _RenderLoop holds
  // _requestedStateMutex across _renderCallback(), and StopRender() has to take
  // that same mutex. So on return the render thread is provably not writing.
  void StopRender() { _renderThread->StopRender(); }

  scene_edit AcquireSceneForEdit()
  {
    (*_sceneVersion)++;    // the int the render pass diffs against
    return _scene->edit(); // stops the render, locks, bumps scene::version()
  }

private:
  scene *_scene;
  HdRenderThread *_renderThread;
  std::atomic<int> *_sceneVersion;
};

