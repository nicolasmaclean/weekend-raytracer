#include "renderer.h"

#include <cstring>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/tokens.h>

#include "convert.h"
#include "renderBuffer.h"

void HdWeekendRenderer::SetCamera(const GfMatrix4d &view, const GfMatrix4d &proj)
{
  _cam.set_camera(ToMat4(view), ToMat4(proj));   // computes inverses, detects ortho
}

void HdWeekendRenderer::SetDataWindow(const GfRect2i &w)
{
  _dataWindow = w;
  _cam.data_window = { w.GetMinX(), w.GetMinY(), w.GetMaxX(), w.GetMaxY() };
}

// The one place a HdRenderBuffer becomes a tracer render_buffer. Every buffer in
// this render index came from our CreateBprim, and the render pass's fallbacks
// are ours too, so the static_cast is sound - but a host that ever hands us a
// foreign buffer would corrupt memory silently, hence the debug check.
static render_buffer *_TracerBuffer(HdRenderBuffer *rb)
{
  if (rb == nullptr)
  {
    return nullptr;
  }

  if (!TF_VERIFY(dynamic_cast<HdWeekendRenderBuffer *>(rb),
                 "AOV bound to a render buffer this delegate did not create"))
  {
    return nullptr;
  }

  return &static_cast<HdWeekendRenderBuffer *>(rb)->Buffer();
}

void HdWeekendRenderer::SetAovBindings(HdRenderPassAovBindingVector const &bindings)
{
  _aovBindings = bindings;

  _aovs.clear();
  _aovs.reserve(bindings.size());
  for (const HdRenderPassAovBinding &b : bindings)
  {
    // Hosts request AOVs we never declared. Dropping them is correct;
    // TF_CODING_ERROR-ing on them is not.
    aov name;
    if (!ToAov(b.aovName, &name))
    {
      continue;
    }

    if (render_buffer *buffer = _TracerBuffer(b.renderBuffer))
    {
      _aovs.push_back({name, buffer});
    }
  }
}

// The binding's clearValue if it holds one, else the AOV's default. The value's
// type follows the buffer's format, not the VtValue's: `int32` buffers must be
// cleared with int32_t or render_buffer::clear writes floats into them.
static void _ClearAov(render_buffer &buffer, aov name, const VtValue &clearValue)
{
  const aov_descriptor d = default_aov_descriptor(name);

  int n = d.clear_components;
  float v[4] = { d.clear_value[0], d.clear_value[1], d.clear_value[2], d.clear_value[3] };

  if (clearValue.IsHolding<GfVec4f>())
  {
    const GfVec4f c = clearValue.UncheckedGet<GfVec4f>();
    n = 4;
    std::memcpy(v, c.data(), sizeof(v));
  }
  else if (clearValue.IsHolding<GfVec3f>())
  {
    const GfVec3f c = clearValue.UncheckedGet<GfVec3f>();
    n = 3;
    std::memcpy(v, c.data(), 3 * sizeof(float));
  }
  else if (clearValue.IsHolding<float>())
  {
    n = 1;
    v[0] = clearValue.UncheckedGet<float>();
  }
  else if (clearValue.IsHolding<double>())
  {
    n = 1;
    v[0] = float(clearValue.UncheckedGet<double>());
  }
  else if (clearValue.IsHolding<int>())
  {
    n = 1;
    v[0] = float(clearValue.UncheckedGet<int>());
  }

  if (component_of(buffer.format()) == component_type::int32)
  {
    const int32_t iv[4] = { int32_t(v[0]), int32_t(v[1]), int32_t(v[2]), int32_t(v[3]) };
    buffer.clear(n, iv);
  }
  else
  {
    buffer.clear(n, v);
  }
}

void HdWeekendRenderer::Clear()
{
  // Walk the Hydra bindings rather than _aovs: the clear value lives on the
  // binding. Unrecognised names were already dropped from _aovs, so re-derive
  // the name here and skip them the same way.
  for (const HdRenderPassAovBinding &b : _aovBindings)
  {
    aov name;
    if (!ToAov(b.aovName, &name))
    {
      continue;
    }

    if (render_buffer *buffer = _TracerBuffer(b.renderBuffer))
    {
      _ClearAov(*buffer, name, b.clearValue);
      buffer->set_converged(false);
    }
  }
}

void HdWeekendRenderer::Render(HdRenderThread *thread)
{
  // Stage C wraps `thread` in a render_control adapter. Until then the tracer
  // runs uninterruptible, which is why stage A is verified with usdrecord.
  (void)thread;

  const render_stats stats = _renderer.render(_cam, _scene, _aovs, nullptr);

  // renderer::render() returns an empty render_stats when validate() rejects the
  // state - empty bindings, mismatched buffer sizes, or a data window outside the
  // buffer. Silent otherwise, and the symptom is an all-black frame.
  if (stats.completed_samples == 0)
  {
    TF_WARN("Weekend rendered 0 samples: %zu aov binding(s), data window (%d, %d)-(%d, %d)",
            _aovs.size(),
            _cam.data_window.min_x, _cam.data_window.min_y,
            _cam.data_window.max_x, _cam.data_window.max_y);
  }
}
