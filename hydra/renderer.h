#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rect2i.h"

#include "tracer/scene.h"
#include "tracer/camera.h"
#include "tracer/renderer.h"
#include "tracer/render_buffer.h"

using namespace pxr;


class HdWeekendRenderer final
{
public:
    void SetCamera(const GfMatrix4d &view, const GfMatrix4d &proj);
    void SetDataWindow(const GfRect2i &window);
    void SetAovBindings(HdRenderPassAovBindingVector const &bindings);
    HdRenderPassAovBindingVector const& GetAovBindings() const { return _aovBindings; }

    void SetSamplesToConvergence(int n)      { _renderer.samples_to_converge = n; }
    void SetRandomNumberSeed(uint64_t s)     { _renderer.frame_seed = s; }

    void Clear();                                     // clear every bound AOV to its clear value
    void MarkAovBuffersUnconverged() { mark_unconverged(_aovs); }
    void Render(HdRenderThread *thread);              // stage C; stage A passes nullptr
    int  CompletedSamples() const { return _renderer.completed_samples(); }

    scene& Scene() { return _scene; }                 // reach it only via HdWeekendRenderParam

private:
    scene    _scene;
    camera   _cam;
    renderer _renderer;      // tracer's
    aov_bindings _aovs;      // tracer's, parallel to _aovBindings
    HdRenderPassAovBindingVector _aovBindings;
    GfRect2i _dataWindow;
};

