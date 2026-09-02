#pragma once

#include <algorithm>
#include <chrono>

#include "pxr/pxr.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rect2i.h"

#include "tracer/render_control.h"
#include "tracer/scene.h"
#include "tracer/camera.h"
#include "tracer/renderer.h"
#include "tracer/render_buffer.h"

using namespace pxr;


struct hd_render_control final : render_control
{
  HdRenderThread *thread = nullptr;

  [[nodiscard]] bool is_stop_requested() const override { return thread && thread->IsStopRequested(); }
  [[nodiscard]] bool is_pause_requested() const override { return thread && thread->IsPauseRequested(); }
};

class HdWeekendRenderer final
{
public:
  explicit HdWeekendRenderer();
  void SetCamera(const GfMatrix4d &view, const GfMatrix4d &proj);
  void SetDataWindow(const GfRect2i &window);
  void SetAovBindings(HdRenderPassAovBindingVector const &bindings);
  [[nodiscard]] HdRenderPassAovBindingVector const &GetAovBindings() const { return _aovBindings; }

  // The render settings, applied by the render pass whenever the delegate's
  // settings version changes. Each clamps the same way config.cpp clamps its
  // env var, because a value from the settings panel never passed through it.
  void SetSamplesToConvergence(int n) { _renderer.samples_to_converge = std::max(1, n); }
  void SetMaxBounces(int n) { _renderer.max_bounces = std::max(0, n); }
  void SetTileSize(int n) { _renderer.tile_size = std::max(1, n); }
  void SetJitterCamera(bool j) { _cam.jitter = j; }

  // -1 means "vary per invocation". The tracer has no such convention -
  // sample_seed() just mixes whatever it is given - so -1 is resolved here,
  // once, rather than leaving frame_seed at a sentinel the renderer would
  // happily use as a literal seed.
  void SetRandomNumberSeed(int s)
  {
    _renderer.frame_seed = (s < 0) ? uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) : uint64_t(s);
  }

  void Clear(); // clear every bound AOV to its clear value
  void MarkAovBuffersUnconverged() { mark_unconverged(_aovs); }
  void Render(HdRenderThread *thread); // stage C; stage A passes nullptr
  [[nodiscard]] int CompletedSamples() const { return _renderer.completed_samples(); }

  scene &Scene() { return _scene; } // reach it only via HdWeekendRenderParam

private:
  scene _scene;
  camera _cam;
  renderer _renderer; // tracer's
  aov_bindings _aovs; // tracer's, parallel to _aovBindings
  HdRenderPassAovBindingVector _aovBindings;
  GfRect2i _dataWindow;
};

