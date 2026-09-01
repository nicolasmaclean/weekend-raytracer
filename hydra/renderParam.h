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

