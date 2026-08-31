#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// pixel buffer in the same format as pxr HdRenderBuffer
// tracer can use this without a usd dependency then the hydra delegate can
// cast this pixel buffer to HdRenderBuffer at pretty much no cost
//
// supports generic data types allowing different precisions or color, depth, etc.
enum class buffer_format : int {
  invalid = -1,

  unorm8 = 0, // a byte holding a float in [0,1]
  unorm8_vec2,
  unorm8_vec3,
  unorm8_vec4,

  snorm8, // a byte holding a float in [-1,1]
  snorm8_vec2,
  snorm8_vec3,
  snorm8_vec4,

  float16, // NOT SUPPORTED - allocate() rejects it.
  float16_vec2,
  float16_vec3,
  float16_vec4,

  float32,
  float32_vec2,
  float32_vec3,
  float32_vec4,

  int16, // NOT SUPPORTED
  int16_vec2,
  int16_vec3,
  int16_vec4,

  uint16, // NOT SUPPORTED
  uint16_vec2,
  uint16_vec3,
  uint16_vec4,

  int32,
  int32_vec2,
  int32_vec3,
  int32_vec4,
};

// The four component types we handle. float16/int16/uint16 map to `unsupported`
// and are rejected at allocate() rather than silently mis-written.
enum class component_type { unsupported, unorm8, snorm8, float32, int32 };

inline int component_count(buffer_format f)
{
  int i = int(f);
  return i < 0 ? 0 : (i % 4) + 1;
}

inline component_type component_of(buffer_format f)
{
  switch (int(f) / 4)
  {
  case 0: return component_type::unorm8;
  case 1: return component_type::snorm8;
  case 3: return component_type::float32;
  case 6: return component_type::int32;
  default: return component_type::unsupported; // float16, int16, uint16
  }
}

inline int component_size(component_type c)
{
  switch (c)
  {
  case component_type::unorm8:
  case component_type::snorm8: return 1;
  case component_type::float32:
  case component_type::int32: return 4;
  default: return 0;
  }
}

// == HdDataSizeOfFormat for every format this file supports.
inline int format_size(buffer_format f)
{
  return component_size(component_of(f)) * component_count(f);
}

// Sample buffers are always float32 or int32, with the SAME component count as
// the resolved format (HdEmbreeRenderBuffer::_GetSampleFormat).
inline buffer_format sample_format_of(buffer_format f)
{
  int n = component_count(f);
  switch (component_of(f))
  {
  case component_type::unorm8:
  case component_type::snorm8:
  case component_type::float32: return buffer_format(int(buffer_format::float32) + n - 1);
  case component_type::int32: return buffer_format(int(buffer_format::int32) + n - 1);
  default: return buffer_format::invalid;
  }
}

class render_buffer
{
public:
  bool allocate(int w, int h, buffer_format f, bool multisampled)
  {
    deallocate();

    if (w < 0 || h < 0 || component_of(f) == component_type::unsupported)
    {
      return false;
    }

    _width = w;
    _height = h;
    _format = f;
    _resolved.resize(size_t(w) * h * format_size(f));

    _multisampled = multisampled;
    if (_multisampled)
    {
      _samples.resize(size_t(w) * h * format_size(sample_format_of(f)));
      _sample_count.resize(size_t(w) * h);
    }

    return true;
  }

  [[nodiscard]] int width() const { return _width; }
  [[nodiscard]] int height() const { return _height; }
  [[nodiscard]] static int depth() { return 1; }
  [[nodiscard]] buffer_format format() const { return _format; }
  [[nodiscard]] bool is_multisampled() const { return _multisampled; }

  void *map()
  {
    _mappers++;
    return _resolved.data();
  }
  void unmap() { _mappers--; }
  [[nodiscard]] bool is_mapped() const { return _mappers.load() != 0; }

  [[nodiscard]] bool is_converged() const { return _converged.load(); }
  void set_converged(bool c) { _converged.store(c); }

  // Divide accumulation by count and convert into the resolved buffer. Cheap
  // enough to call every presented frame; that is what makes a partial render
  // legible to the host.
  void resolve()
  {
    if (!_multisampled)
    {
      return;
    }

    const component_type comp = component_of(_format);
    const int n = component_count(_format);
    const int out_stride = format_size(_format);
    const int in_stride = format_size(sample_format_of(_format));

    for (size_t i = 0; i < size_t(_width) * _height; i++)
    {
      const uint32_t count = _sample_count[i];
      if (count == 0)
      {
        continue;
      }

      uint8_t *dst = &_resolved[i * out_stride];
      const uint8_t *src = &_samples[i * in_stride];
      for (int c = 0; c < n; c++)
      {
        if (comp == component_type::int32)
        {
          ((int32_t *)dst)[c] = ((const int32_t *)src)[c] / int32_t(count);
        }
        else
        {
          _store(comp, dst, c, ((const float *)src)[c] / float(count));
        }
      }
    }
  }

  void deallocate()
  {
    _width = _height = 0;
    _format = buffer_format::invalid;
    _multisampled = false;
    _resolved.clear();
    _samples.clear();
    _sample_count.clear();
    _mappers.store(0);
    _converged.store(false);
  }

  // --- renderer-facing writes (HdEmbreeRenderBuffer's I/O helpers) ---

  // Multisampled: accumulate one sample and bump the pixel's count.
  // Single-sampled: overwrite the resolved value.
  // Missing components are taken as 0, extra ones discarded.
  void write(int x, int y, int n, const float *v)
  {
    const size_t i = size_t(y) * _width + x;
    if (_multisampled)
    {
      _accumulate(&_samples[i * format_size(sample_format_of(_format))], n, v);
      _sample_count[i]++;
    }
    else
    {
      _store_all(&_resolved[i * format_size(_format)], n, v);
    }
  }

  void write(int x, int y, int n, const int32_t *v)
  {
    const size_t i = size_t(y) * _width + x;
    if (_multisampled)
    {
      _accumulate(&_samples[i * format_size(sample_format_of(_format))], n, v);
      _sample_count[i]++;
    }
    else
    {
      _store_all(&_resolved[i * format_size(_format)], n, v);
    }
  }

  // Set every resolved pixel, and zero the accumulation. This is the AOV clear
  // value, not a memset: `color` clears to (0,0,0,0), `depth` to 1.0.
  template <typename T> void clear(int n, const T *v)
  {
    const int stride = format_size(_format);
    for (size_t i = 0; i < size_t(_width) * _height; i++)
    {
      _store_all(&_resolved[i * stride], n, v);
    }

    if (_multisampled)
    {
      std::fill(_sample_count.begin(), _sample_count.end(), 0U);
      std::fill(_samples.begin(), _samples.end(), uint8_t(0));
    }
  }

  // How many samples pixel (x,y) has taken. The renderer needs it to seed its
  // rng deterministically across batches.
  [[nodiscard]] uint32_t samples_at(int x, int y) const
  {
    return _multisampled ? _sample_count[size_t(y) * _width + x] : 0;
  }

  // Read a resolved pixel back out. For the ppm writer and the sdl blit; the
  // hydra path uses map() instead.
  void read(int x, int y, int n, float *out) const
  {
    const component_type comp = component_of(_format);
    const int have = component_count(_format);
    const uint8_t *src = &_resolved[(size_t(y) * _width + x) * format_size(_format)];

    for (int c = 0; c < n; c++)
    {
      if (c >= have)
      {
        out[c] = 0;
        continue;
      }
      switch (comp)
      {
      case component_type::unorm8: out[c] = ((const uint8_t *)src)[c] / 255.0F; break;
      case component_type::snorm8: out[c] = std::max(((const int8_t *)src)[c] / 127.0F, -1.0F); break;
      case component_type::int32: out[c] = float(((const int32_t *)src)[c]); break;
      default: out[c] = ((const float *)src)[c]; break;
      }
    }
  }

private:
  template <typename T> void _accumulate(uint8_t *dst, int n, const T *v)
  {
    const bool ints = component_of(_format) == component_type::int32;
    for (int c = 0; c < component_count(_format); c++)
    {
      const T value = c < n ? v[c] : T(0);
      if (ints)
      {
        ((int32_t *)dst)[c] += int32_t(value);
      }
      else
      {
        ((float *)dst)[c] += float(value);
      }
    }
  }

  template <typename T> void _store_all(uint8_t *dst, int n, const T *v)
  {
    const component_type comp = component_of(_format);
    for (int c = 0; c < component_count(_format); c++)
    {
      _store(comp, dst, c, c < n ? float(v[c]) : 0.0F);
    }
  }

  static void _store(component_type comp, uint8_t *dst, int c, float value)
  {
    switch (comp)
    {
    case component_type::unorm8: ((uint8_t *)dst)[c] = uint8_t(value * 255.0F); break;
    case component_type::snorm8: ((int8_t *)dst)[c] = int8_t(value * 127.0F); break;
    case component_type::int32: ((int32_t *)dst)[c] = int32_t(value); break;
    default: ((float *)dst)[c] = value; break;
    }
  }

  int _width = 0, _height = 0;
  buffer_format _format = buffer_format::invalid;
  bool _multisampled = false;

  std::vector<uint8_t> _resolved;      // output, in _format
  std::vector<uint8_t> _samples;       // accumulation, float32/int32
  std::vector<uint32_t> _sample_count; // per pixel

  std::atomic<int> _mappers{0};
  std::atomic<bool> _converged{false};
};

// The AOVs the renderer knows how to fill. Named to match HdAovTokens
// (pxr/imaging/hd/tokens.h) so the delegate's mapping is one switch.
enum class aov { color, depth, camera_depth, normal, n_eye, prim_id, instance_id, element_id };

struct aov_binding
{
  aov name;
  render_buffer *buffer = nullptr;
};

using aov_bindings = std::vector<aov_binding>;

// hdEmbree's AOV table (spec §8.3), in tracer terms. The delegate's
// GetDefaultAovDescriptor is a translation of this, and the cli/viewer use it to
// allocate and clear - one table, both consumers.
struct aov_descriptor
{
  buffer_format format = buffer_format::float32_vec4;
  bool multisampled = false;
  int clear_components = 0;
  float clear_value[4] = {0, 0, 0, 0}; // ids are small enough to be exact in float
};

inline aov_descriptor default_aov_descriptor(aov name)
{
  switch (name)
  {
  case aov::color: return {buffer_format::float32_vec4, true, 4, {0, 0, 0, 0}};
  case aov::depth: return {buffer_format::float32, false, 1, {1, 0, 0, 0}};
  case aov::camera_depth: return {buffer_format::float32, false, 1, {0, 0, 0, 0}};
  case aov::normal:
  case aov::n_eye: return {buffer_format::float32_vec3, false, 3, {-1, -1, -1, 0}};
  case aov::prim_id:
  case aov::instance_id:
  case aov::element_id: return {buffer_format::int32, false, 1, {-1, 0, 0, 0}};
  }
  return {};
}

// Allocate `buffer` for `name` at the default format and clear it. Returns the
// binding, so a caller can build its bindings vector in one expression.
inline aov_binding allocate_aov(render_buffer &buffer, aov name, int width, int height)
{
  const aov_descriptor d = default_aov_descriptor(name);
  buffer.allocate(width, height, d.format, d.multisampled);
  buffer.clear(d.clear_components, d.clear_value);
  return {name, &buffer};
}

