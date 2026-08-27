# render target refactor — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `render target refactor (prepare framebuffer to work with hydra)` — see [[Roadmap]]
**Context:** [[hydra-spec]] §8.1–8.3, §9, §10.2, §17.2, §17.7 · [[roadmap-discussion-8-26]] §2 "Render target refactor" · [[camera-refactor]] "Next up"
**Every number in this document was measured on this machine on 2026-08-26. See "Pre-verified facts".**

---

## What this task is

[[hydra-spec]] §8.2 describes hdEmbree's render buffer as three allocations:

> - `_buffer` — the resolved output, in the requested `HdFormat`
> - `_sampleBuffer` — the accumulation buffer, always float32 or int32 with the *same
>   component count* as the requested format
> - `_sampleCount` — per-pixel sample counts
>
> `Resolve()` divides accumulation by count and converts into `_buffer`. This is the
> mechanism that makes progressive refinement legible to the host.

`tracer/framebuffer.h` is `std::vector<color>` (3 **doubles**) + `std::vector<int> samples` +
`get_pixel()` dividing on read. That is the same *idea* — accumulate and divide — with none of the
host-facing contract: no format, no resolved output, no map/unmap, no converged flag, no AOVs, no
clear values, and the wrong element type.

At the end of this task `tracer/render_buffer.h` is a **USD-free implementation of
`HdRenderBuffer`'s semantics**: `allocate` / `map` / `unmap` / `is_mapped` / `resolve` /
`is_converged` / `deallocate` plus hdEmbree's `write` / `clear` / `set_converged` helpers, over
the same three allocations, keyed to a `buffer_format` enum whose values are `HdFormat`'s values.
The hydra delegate's `HdWeekendRenderBuffer` then *forwards* its twelve pure virtuals to one of
these and `Map()` hands the host the exact bytes the tracer wrote — no copy, no second buffer, no
conversion.

The renderer stops taking a `framebuffer&` and starts taking an `aov_bindings` list, so `color`
and `depth` are filled from one traced ray. `camera.h` does not change at all.

## Why it matters more than "framebuffer refactor" sounds

1. **Line order is decided here, and it reaches backwards.** [[camera-refactor]]'s closing note
   flagged this as "the one decision that reaches back into this task". `HdRenderBuffer` row 0 is
   the **bottom** image line; the tracer's framebuffer is top-left origin. Step 3 puts the flip on
   one line in the renderer and step 6 proves it with a sub-window, which is the only place the
   two plausible flips (`height-1-y` vs `max_y-y`) disagree.
2. **Zero-copy or double-buffer, forever.** If the tracer keeps a private buffer shape, the
   delegate must allocate its own `HdRenderBuffer` storage and memcpy-plus-convert every frame at
   400×225×4×4 bytes per presented frame, per AOV. Mirroring the layout now means the delegate is
   a ~60-line forwarding shim (Appendix A) and the render thread writes straight into host memory.
3. **`get_pixel()` divides on read; Hydra wants `Resolve()`.** Same arithmetic, different
   ownership: the host decides *when* the noisy-but-correct image is materialised, and reads it
   through `Map()` whenever it likes. §17.2 is what makes this pay off — `HdxAovInputTask` uploads
   a mapped CPU buffer into an `HgiTexture` on its own, so a CPU delegate gets viewport
   presentation, colour correction and picking for free.
4. **AOVs are the render pass's required input.** §9: "Empty AOV bindings are legal input but not
   a legal render state. If `renderPassState->GetAovBindings()` is empty, synthesize a `color` +
   `depth` binding." A renderer that can only write one image cannot satisfy that, and `depth`
   needs the primary hit, which `ray_color` currently throws away.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| `HdRenderBuffer` subclass in `hydra/`, `CreateBprim`, `GetDefaultAovDescriptor`, `HdAovTokens` mapping | `hydra wrapper` (0.3.0) — Appendix A is written and compiled, just not committed to `hydra/` |
| `HdRenderThread`, `IsStopRequested()`, cancellation points, pause loop | `interruptible tile-driven render loop` |
| `render_tile()` entry point, `WorkParallelForN`, `threadLimit` | `interruptible tile-driven render loop` |
| `samplesToConvergence`, `IsConverged()` policy, `GetRenderStats()` | `interruptible tile-driven render loop` |
| `primId` / `instanceId` / `elementId` AOVs | `scene graph with mutation` — there are no prim ids to write yet |
| `primvars:<name>` AOVs, `HdParsedAovToken` | not scheduled; the switch in step 3 is where they land |
| float16 / int16 / uint16 buffer formats | not scheduled; `allocate()` rejects them, and step 2 asserts the rejection is total |
| Any change to `camera.h` | nothing — the flip lives in the renderer, and step 5 proves the camera is untouched |

The tracer stays **USD-free**: no `pxr/` include appears under `tracer/` or `viewer/`. Steps 2 and
7 are the only places USD appears and both are scratchpad programs — per the standing rule, **no
test code lands in the repo**.

---

## Pre-verified facts

These were measured, not assumed. Every program named below exists and was run; the numbers are
its output. Scratchpad:

```
S=/tmp/claude-1000/-home-nick-git-weekend-raytracer/da9c9438-ac91-47c3-8b1a-1fd327126a2c/scratchpad
```

| Claim | Measured |
|---|---|
| `buffer_format`'s values == `HdFormat`'s values | `static_assert`, 12 anchor points, compiles (`test_render_buffer_hd.cpp`) |
| `format_size` / `component_count` / `component_of` / `sample_format_of` agree with `HdDataSizeOfFormat` / `HdGetComponentCount` / `HdGetComponentFormat` and hdEmbree's `_GetSampleFormat` rule | **16 formats agree**, and every format mapped to `unsupported` is confirmed float16/int16/uint16/float32uint8 |
| A 60-line `HdWeekendRenderBuffer` forwarding to `render_buffer` satisfies all 12 pure virtuals and behaves through an `HdRenderBuffer*` | compiles and passes: dims, format round-trip, `depth != 1` rejected, float16 rejected, nested `Map()` returns the same address, mapper count returns to zero, `SetConverged` observed, clear value readable |
| `resolve()` == the arithmetic mean computed in double | worst \|d\| = **1.34e-06** over 1024 pixels × 0–200 samples; pixels with zero samples left at the clear value |
| **float32 accumulation is cheap enough to be free** | vs double: worst relative error **2e-06 at 1000 spp**, **2e-05 at 100 000 spp**; worst 8-bit channel delta **1** at every count (`precision.cpp`) |
| **`render_buffer` row 0 is the bottom image line** | buffer row 0 mean luminance 0.311 vs the current ppm's *bottom* row 0.311; buffer row 224 is 0.863 vs the ppm's *top* row 0.858 (scene 0, `rowprobe.cpp`) |
| **The new path reproduces the current images** | scenes 0 and 1: `max|d| = 1` on **2 of 270 000 channels**; scene 2: **byte-identical**. Serial path likewise (`render_e2e.cpp` vs `gold_*.ppm`) |
| A data window inset on all four sides fills exactly that sub-rect, right side up | 25 000/25 000 pixels inside sampled, **0** outside; window top line 0.864 brighter than bottom line 0.327 (`subwindow.cpp`) |
| The `depth` AOV is in range and sane | scene 0: 74 995/90 000 pixels written, all in [0.980, 0.998]; scene 1: 67 473 in [0.800, 0.953]; scene 2: 90 000 in [0.958, 0.983] |
| No performance regression | scene 0 **0.0617–0.0633 ms/px** across runs vs **0.0622 ms/px** for the current build — run-to-run noise |
| `build-hydra` builds `hdWeekend` and `testHdWeekend` today | verified before any edit — but see "As executed": `hydra/` includes no tracer header yet, so this gate cannot fail in this task |

The image row is the load-bearing one. Rays are bit-identical to the current renderer because
seeds stay keyed on the y-**down** pixel index; the only difference is float32 vs double
accumulation, which lands on two 8-bit boundaries out of 270 000. **Any larger difference in
step 5 is a bug you introduced, not an expected consequence.**

Build lines that work on this machine:

```bash
# tracer-only scratchpad program
g++ -std=c++17 -O3 -DNDEBUG -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/e2e $S/render_e2e.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# USD-linked scratchpad program (note: -lusd_*, and python for boost::python headers)
source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$S -Itracer \
    -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/test_rb $S/test_render_buffer_hd.cpp \
    -L$USD_ROOT/lib -lusd_hd -lusd_gf -lusd_tf -lusd_sdf -lusd_vt -lpython3.12
```

`-Wno-deprecated` silences `tf/hashset.h`'s `ext/hash_set`; `-I/usr/include/python3.12` and
`-lpython3.12` are needed because `hd/types.h` pulls in `vt/value.h` → `tf/pyObjWrapper.h` →
`pyconfig.h`.

---

## The design in one page

```
tracer/render_buffer.h   NEW      buffer_format (== HdFormat's values), render_buffer
                                  (HdRenderBuffer's semantics, no USD), aov / aov_binding /
                                  aov_bindings, and the §8.3 default-AOV table.

tracer/renderer.h        REWRITE  render(cam, world, aov_bindings, samples). Traces once per
                                  sample and writes every bound AOV. Owns the y-flip.
                                  ray_color becomes trace(), which also reports the primary hit.

tracer/framebuffer.h     DELETE   nothing survives it that render_buffer does not do better.

tracer/main.cpp          EDIT     allocate color + depth, resolve, write ppm bottom-row-first.
viewer/main.cpp          EDIT     allocate color, resolve each frame, blit bottom-row-first.

tracer/camera.h          UNTOUCHED
```

### The line-order decision, and why it costs one line

`HdRenderBuffer`'s row 0 is the bottom image line. Two independent pieces of evidence:

- hdEmbree's own test writes a mapped render buffer to a file with
  `storage.flipped = true` (`testenv/testHdEmbree.cpp:397`).
- hdEmbree's renderer flips the data window against the buffer height before looping:
  "The data window is y-Down but the image line order is from bottom to top, so we need to flip
  it" (`renderer.cpp:626-635`).

`CameraUtilFraming::dataWindow` — what `camera::data_window` holds — is y-**down**. So somewhere a
flip must happen. hdEmbree flips the *loop bounds* (`minY = _height - maxY`, swap) and then
computes NDC from the flipped `minY`, which is why its NDC y has no sign inversion while ours
does. We do the equivalent thing more cheaply:

```cpp
const int by = height - 1 - y;   // y is a y-down data-window row; by is a buffer row
```

The loop keeps iterating y-down window rows, `camera::get_ray(generator, x, y)` keeps receiving
the y-down row and keeps its existing `ndc_y = 1 - 2 * (...)`, and only the *write index* is
flipped. That is why `camera.h` does not change. The two arrangements are algebraically identical:
at the top window row, hdEmbree's flipped loop reaches buffer row `height-1-min_y` with
NDC y ≈ +1, and so does this.

Consumers of a bottom-up buffer flip on read: the ppm writer iterates `y = height-1 → 0` (ppm is
top-to-bottom), and the SDL blit reads buffer row `height-1-y` for texture row `y`.

### Why the tracer mirrors `HdRenderBuffer` rather than being wrapped by it

The alternative — keep a tracer-shaped buffer and convert in the delegate — costs a full-frame
convert-and-copy per presented frame per AOV, and it means the accumulation buffer's element type
is a private tracer decision that the host can never see. Mirroring the layout instead makes the
delegate's buffer a forwarding shim (Appendix A) whose `Map()` returns the tracer's resolved
bytes directly, and makes the format enum a `static_cast` rather than a switch. It costs the
tracer one enum it does not otherwise need, and `render_buffer` stays plain C++ with no `pxr/`
include.

Accumulating in float32 rather than double is forced by §8.2 — the sample buffer must have the
same component count as the resolved format, and hdEmbree's `_GetSampleFormat` makes it float32 —
and it is measured harmless above.

### Alpha

`color` is `HdFormatFloat32Vec4`, multisampled, cleared to `(0,0,0,0)`, and **premultiplied**
(`tokens.h:365`). The tracer writes `(r, g, b, 1)` per sample: with alpha 1 the premultiplication
is the identity, and `resolve()` divides all four components so resolved alpha stays 1.

A camera-ray miss returns sky, not transparency. That matches hdEmbree with a dome light bound —
`_ComputeColor` returns `domeColor` with alpha 1 — and differs from hdEmbree with *no* lights,
where a miss returns the clear value at alpha 0. Our sky gradient is an environment we own, so
opaque is right. When a real dome-light toggle exists, a miss with lighting disabled should write
the clear value instead; that is the one line to revisit.

---

# Step 0 — Capture golden images before you touch anything

**Why:** step 5's gate compares against these, and they cannot be regenerated once
`framebuffer.h` is gone.

```bash
cd /home/nick/git/weekend-raytracer
git status --porcelain          # must be empty

S=/tmp/claude-1000/-home-nick-git-weekend-raytracer/da9c9438-ac91-47c3-8b1a-1fd327126a2c/scratchpad
mkdir -p $S

cmake --build build --config Release
for s in 0 1 2; do
  ./build/tracer/Release/tracer_cli $s > $S/gold_$s.ppm
done
md5sum $S/gold_*.ppm | tee $S/gold.md5
```

Golds already exist in that scratchpad from this document's own measurements:

```
a97f9ba0674d4c1563ef877f3fb1af57  gold_0.ppm
3973858ba527239037f8c683588f8142  gold_1.ppm
418151b864772683d18aef594a1651b7  gold_2.ppm
```

Record the `ms/px` on stderr for scene 0 (it was **0.0622**), and confirm the hydra plugin builds
*before* you start, since you are about to change headers it includes:

```bash
source env.sh && cmake --build build-hydra -j8      # hdWeekend + testHdWeekend
```

---

# Step 1 — Write `tracer/render_buffer.h`

**Why first:** everything else depends on it, and it is the only part where a silent error (a
format size, a sample-format rule, a resolve that divides the wrong component) produces a
plausible image. Step 2 proves it against USD before anything is built on top.

A working copy exists at `$S/render_buffer.h` — the file below verbatim. Copy it rather than
retyping.

```cpp
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// A host-facing render target, shaped exactly like pxr HdRenderBuffer
// (pxr/imaging/hd/renderBuffer.h) but with no USD dependency. The hydra
// delegate's HdWeekendRenderBuffer owns one of these and forwards its 12 pure
// virtuals to it, so the renderer writes straight into the memory the host maps
// - no copy, no second buffer.
//
// Row 0 is the BOTTOM image line. That is HdRenderBuffer's line order: the
// image writer in hdEmbree's own test sets `storage.flipped = true` when
// handing a mapped render buffer to Hio (testHdEmbree.cpp:397), and hdEmbree's
// renderer flips the y-down data window against the buffer height before
// looping (renderer.cpp:626-635).

// Mirrors HdFormat (pxr/imaging/hd/types.h) VALUE FOR VALUE, so the delegate
// can static_cast between the two. The USD-linked gate static_asserts that.
enum class buffer_format : int {
  invalid = -1,

  unorm8 = 0,  // a byte holding a float in [0,1]
  unorm8_vec2,
  unorm8_vec3,
  unorm8_vec4,

  snorm8,  // a byte holding a float in [-1,1]
  snorm8_vec2,
  snorm8_vec3,
  snorm8_vec4,

  float16,  // NOT SUPPORTED - allocate() rejects it.
  float16_vec2,
  float16_vec3,
  float16_vec4,

  float32,
  float32_vec2,
  float32_vec3,
  float32_vec4,

  int16,  // NOT SUPPORTED
  int16_vec2,
  int16_vec3,
  int16_vec4,

  uint16,  // NOT SUPPORTED
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
  switch (int(f) / 4) {
    case 0: return component_type::unorm8;
    case 1: return component_type::snorm8;
    case 3: return component_type::float32;
    case 6: return component_type::int32;
    default: return component_type::unsupported;  // float16, int16, uint16
  }
}

inline int component_size(component_type c)
{
  switch (c) {
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
  switch (component_of(f)) {
    case component_type::unorm8:
    case component_type::snorm8:
    case component_type::float32:
      return buffer_format(int(buffer_format::float32) + n - 1);
    case component_type::int32:
      return buffer_format(int(buffer_format::int32) + n - 1);
    default:
      return buffer_format::invalid;
  }
}

class render_buffer
{
public:
  // --- host-facing contract: one method per HdRenderBuffer pure virtual ---

  bool allocate(int w, int h, buffer_format f, bool multisampled)
  {
    deallocate();

    if (w < 0 || h < 0 || component_of(f) == component_type::unsupported) {
      return false;
    }

    _width = w;
    _height = h;
    _format = f;
    _resolved.resize(size_t(w) * h * format_size(f));

    _multisampled = multisampled;
    if (_multisampled) {
      _samples.resize(size_t(w) * h * format_size(sample_format_of(f)));
      _sample_count.resize(size_t(w) * h);
    }

    return true;
  }

  int width() const { return _width; }
  int height() const { return _height; }
  int depth() const { return 1; }
  buffer_format format() const { return _format; }
  bool is_multisampled() const { return _multisampled; }

  void *map()
  {
    _mappers++;
    return _resolved.data();
  }
  void unmap() { _mappers--; }
  bool is_mapped() const { return _mappers.load() != 0; }

  bool is_converged() const { return _converged.load(); }
  void set_converged(bool c) { _converged.store(c); }

  // Divide accumulation by count and convert into the resolved buffer. Cheap
  // enough to call every presented frame; that is what makes a partial render
  // legible to the host.
  void resolve()
  {
    if (!_multisampled) {
      return;
    }

    const component_type comp = component_of(_format);
    const int n = component_count(_format);
    const int out_stride = format_size(_format);
    const int in_stride = format_size(sample_format_of(_format));

    for (size_t i = 0; i < size_t(_width) * _height; i++) {
      const uint32_t count = _sample_count[i];
      if (count == 0) {
        continue;
      }

      uint8_t *dst = &_resolved[i * out_stride];
      const uint8_t *src = &_samples[i * in_stride];
      for (int c = 0; c < n; c++) {
        if (comp == component_type::int32) {
          ((int32_t *)dst)[c] = ((const int32_t *)src)[c] / int32_t(count);
        } else {
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
    if (_multisampled) {
      _accumulate(&_samples[i * format_size(sample_format_of(_format))], n, v);
      _sample_count[i]++;
    } else {
      _store_all(&_resolved[i * format_size(_format)], n, v);
    }
  }

  void write(int x, int y, int n, const int32_t *v)
  {
    const size_t i = size_t(y) * _width + x;
    if (_multisampled) {
      _accumulate(&_samples[i * format_size(sample_format_of(_format))], n, v);
      _sample_count[i]++;
    } else {
      _store_all(&_resolved[i * format_size(_format)], n, v);
    }
  }

  // Set every resolved pixel, and zero the accumulation. This is the AOV clear
  // value, not a memset: `color` clears to (0,0,0,0), `depth` to 1.0.
  template <typename T>
  void clear(int n, const T *v)
  {
    const int stride = format_size(_format);
    for (size_t i = 0; i < size_t(_width) * _height; i++) {
      _store_all(&_resolved[i * stride], n, v);
    }

    if (_multisampled) {
      std::fill(_sample_count.begin(), _sample_count.end(), 0u);
      std::fill(_samples.begin(), _samples.end(), uint8_t(0));
    }
  }

  // How many samples pixel (x,y) has taken. The renderer needs it to seed its
  // rng deterministically across batches.
  uint32_t samples_at(int x, int y) const
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

    for (int c = 0; c < n; c++) {
      if (c >= have) {
        out[c] = 0;
        continue;
      }
      switch (comp) {
        case component_type::unorm8: out[c] = ((const uint8_t *)src)[c] / 255.0f; break;
        case component_type::snorm8: out[c] = std::max(((const int8_t *)src)[c] / 127.0f, -1.0f); break;
        case component_type::int32: out[c] = float(((const int32_t *)src)[c]); break;
        default: out[c] = ((const float *)src)[c]; break;
      }
    }
  }

private:
  template <typename T>
  void _accumulate(uint8_t *dst, int n, const T *v)
  {
    const bool ints = component_of(_format) == component_type::int32;
    for (int c = 0; c < component_count(_format); c++) {
      const T value = c < n ? v[c] : T(0);
      if (ints) {
        ((int32_t *)dst)[c] += int32_t(value);
      } else {
        ((float *)dst)[c] += float(value);
      }
    }
  }

  template <typename T>
  void _store_all(uint8_t *dst, int n, const T *v)
  {
    const component_type comp = component_of(_format);
    for (int c = 0; c < component_count(_format); c++) {
      _store(comp, dst, c, c < n ? float(v[c]) : 0.0f);
    }
  }

  static void _store(component_type comp, uint8_t *dst, int c, float value)
  {
    switch (comp) {
      case component_type::unorm8: ((uint8_t *)dst)[c] = uint8_t(value * 255.0f); break;
      case component_type::snorm8: ((int8_t *)dst)[c] = int8_t(value * 127.0f); break;
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
enum class aov { color, depth, camera_depth, normal, n_eye };

struct aov_binding {
  aov name;
  render_buffer *buffer = nullptr;
};

using aov_bindings = std::vector<aov_binding>;

// hdEmbree's AOV table (spec §8.3), in tracer terms. The delegate's
// GetDefaultAovDescriptor is a translation of this, and the cli/viewer use it to
// allocate and clear - one table, both consumers.
struct aov_descriptor {
  buffer_format format = buffer_format::float32_vec4;
  bool multisampled = false;
  int clear_components = 0;
  float clear_value[4] = {0, 0, 0, 0};  // ids are small enough to be exact in float
};

inline aov_descriptor default_aov_descriptor(aov name)
{
  switch (name) {
    case aov::color:        return {buffer_format::float32_vec4, true,  4, {0, 0, 0, 0}};
    case aov::depth:        return {buffer_format::float32,      false, 1, {1, 0, 0, 0}};
    case aov::camera_depth: return {buffer_format::float32,      false, 1, {0, 0, 0, 0}};
    case aov::normal:
    case aov::n_eye:        return {buffer_format::float32_vec3, false, 3, {-1, -1, -1, 0}};
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
```

Four things in there are load-bearing and easy to get subtly wrong:

- **`component_of` divides the enum by 4.** That works only because the enum's values are
  `HdFormat`'s values and `HdFormat` groups by component type in fours. If you renumber the enum
  "for tidiness", this function silently starts lying. Step 2's `static_assert` is the guard.
- **`_sample_count` is `uint32_t`, not atomic.** Two tiles never touch the same pixel, so
  unsynchronised writes are safe — this is hdEmbree's assumption too. It stops being true the day
  a tile scheme overlaps.
- **`allocate()` does not clear.** `resize` zero-fills, and zero is wrong for `depth` (1.0) and
  for id AOVs (-1). Always go through `allocate_aov`, or call `clear` yourself.
- **`clear()` takes the value in the *resolved* format's terms** and also zeroes the accumulation.
  Calling it mid-render restarts the pixel's average, which is exactly what §9's "AOV bindings
  changed → Clear" requires.

No CMake change: `tracer` is an INTERFACE library exporting its directory, so a new header is
picked up with no edit.

---

# Step 2 — GATE 1: the buffer is bit-compatible with `HdRenderBuffer`

**Why this is the most important step:** it converts "the layout should match USD" into a number.
Everything downstream assumes a `static_cast<HdFormat>` is valid and that `Resolve()` means what
hdEmbree means by it. Both are checkable in ~250 lines, and they are checkable *now*, before the
delegate exists.

A working copy exists: `$S/test_render_buffer_hd.cpp`. It does four things:

1. `static_assert`s twelve anchor points of `buffer_format` against `HdFormat`.
2. Walks every `HdFormat` and compares `component_count` / `format_size` / `sample_format_of`
   against `HdGetComponentCount` / `HdDataSizeOfFormat` / hdEmbree's `_GetSampleFormat` rule,
   asserting that anything we map to `unsupported` really is float16/int16/uint16 or the
   `HdFormatFloat32UInt8` depth-stencil block format.
3. Defines the delegate shim from Appendix A and exercises it **through an `HdRenderBuffer*`**:
   allocate, dims, format round-trip, `depth != 1` rejected, float16 rejected, nested
   `Map()`/`Unmap()` mapper counting, `SetConverged`, clear-value readback.
4. Compares `resolve()` against a double-precision mean over random sample counts including zero,
   plus the single-sampled overwrite path, the int32 AOV path, and a unorm8 round trip.

```bash
cd /home/nick/git/weekend-raytracer && source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$S -Itracer \
    -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/test_rb $S/test_render_buffer_hd.cpp \
    -L$USD_ROOT/lib -lusd_hd -lusd_gf -lusd_tf -lusd_sdf -lusd_vt -lpython3.12
$S/test_rb
```

**Expect exactly:**

```
format arithmetic: 16 formats agree with pxr/imaging/hd
resolve vs double mean: worst |d| = 1.34e-06  (float32 accumulation)

all checks passed
```

| Symptom | Cause |
|---|---|
| `static_assert` fires | the enum was renumbered, or a component group is out of order |
| `format_size(X): 4 vs 16` style failures | `component_count`'s `% 4`, or `component_size` |
| `sample_format_of` mismatches on unorm8 | the sample format must be **float32** with the same arity, not "the same format" |
| `resolve drifted from the double mean` | dividing by the wrong count, or resolving the sample buffer's stride with the resolved format's |
| `unsampled pixel got written` | the `count == 0 → continue` guard is missing; it is what keeps a partial render's untouched pixels at the clear value |
| `mapper count did not return to zero` | `unmap` decrementing something else, or `map` not being the only increment |

Also run the accumulation-precision check, so the number in step 5's gate is yours and not this
document's:

```bash
g++ -std=c++17 -O2 -o $S/precision $S/precision.cpp && $S/precision
```

Expect the worst 8-bit channel delta to be **1** at every sample count from 50 to 100 000. That
is the entire cost of moving from double to float32 accumulation.

---

# Step 3 — Rewrite `tracer/renderer.h`

**Why:** this is the deliverable's other half. The renderer stops knowing about one image and
starts knowing about a *list of bound targets*, which is the shape §9 requires; it owns the
y-flip; and `ray_color` becomes `trace`, which reports the primary hit so non-colour AOVs have
something to write.

A working copy exists at `$S/renderer_rb.h`. It is the file below with the struct renamed
`renderer_rb` so it can coexist with the repo's `renderer` during verification — the only
difference.

```cpp
#pragma once

#include <chrono>
#include <cstdint>

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include "camera.h"
#include "hittable.h"
#include "material.h"
#include "render_buffer.h"
#include "rng.h"
#include "tracer.h"
#include "vec3.h"

using namespace std::chrono;

struct renderer
{
  int max_bounces = 20;
  bool multithread = true;

  uint64_t frame_seed = 0; // -1 means "nondeterministic".

  // Accumulates `samples` more samples per pixel into every bound AOV over the
  // camera's data window. Returns elapsed milliseconds.
  double render(const camera &cam, const hittable &world, const aov_bindings &aovs, int samples)
  {
    auto start = high_resolution_clock::now();
    const rect2i &window = cam.data_window;

    if (!validate(cam, aovs))
    {
      return 0;
    }

    // With nothing multisampled bound there is no accumulation to do, so one
    // pass is the whole render (hdEmbree makes the same early exit after its
    // first pass).
    if (!any_multisampled(aovs))
    {
      samples = 1;
    }

    if (multithread)
    {
      tbb::parallel_for(
          tbb::blocked_range2d<int>(window.min_y, window.max_y + 1, 16, window.min_x, window.max_x + 1, 16),
          [&](const tbb::blocked_range2d<int> &tile) {
            render_region(cam, world, aovs, samples, tile.cols().begin(), tile.cols().end(), tile.rows().begin(), tile.rows().end());
          });
    }
    else
    {
      render_region(cam, world, aovs, samples, window.min_x, window.max_x + 1, window.min_y, window.max_y + 1);
    }

    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }

private:
  // Every bound buffer must be the same size, and the data window must fit
  // inside it (hdEmbree: _PreRenderSetup's _IsContained check).
  bool validate(const camera &cam, const aov_bindings &aovs)
  {
    if (aovs.empty() || aovs[0].buffer == nullptr)
    {
      return false;
    }

    const int w = aovs[0].buffer->width(), h = aovs[0].buffer->height();
    for (const aov_binding &b : aovs)
    {
      if (b.buffer == nullptr || b.buffer->width() != w || b.buffer->height() != h)
      {
        return false;
      }
    }

    const rect2i &win = cam.data_window;
    return win.min_x >= 0 && win.min_y >= 0 && win.max_x < w && win.max_y < h;
  }

  static bool any_multisampled(const aov_bindings &aovs)
  {
    for (const aov_binding &b : aovs)
    {
      if (b.buffer->is_multisampled())
      {
        return true;
      }
    }
    return false;
  }

  void render_region(const camera &cam, const hittable &world, const aov_bindings &aovs, int samples, int x0, int x1, int y0, int y1)
  {
    const int height = aovs[0].buffer->height();
    const int width = aovs[0].buffer->width();

    // The pixel whose sample count drives seeding: the first multisampled
    // buffer, i.e. color in every configuration we generate.
    const render_buffer *counter = nullptr;
    for (const aov_binding &b : aovs)
    {
      if (b.buffer->is_multisampled())
      {
        counter = b.buffer;
        break;
      }
    }

    for (int y = y0; y < y1; y++)
    {
      for (int x = x0; x < x1; x++)
      {
        // The data window is y-down; render_buffer row 0 is the bottom image
        // line. One subtraction is the whole flip.
        const int by = height - 1 - y;

        // Seeds are keyed on the y-DOWN pixel, so the noise pattern is a
        // property of the framing, not of the buffer's line order.
        const int seed_index = y * width + x;
        const int sample_base = counter ? int(counter->samples_at(x, by)) : 0;

        for (int sample = 0; sample < samples; sample++)
        {
          rng generator = rng(sample_seed(seed_index, sample_base + sample, frame_seed));
          ray r = cam.get_ray(generator, x, y);

          hit_info primary;
          bool did_hit = false;
          const color radiance = trace(generator, r, max_bounces, world, &primary, &did_hit);

          const bool first_ever = sample_base == 0 && sample == 0;

          for (const aov_binding &binding : aovs)
          {
            render_buffer &buffer = *binding.buffer;

            if (buffer.is_converged())
            {
              continue;
            }

            // Single-sampled AOVs are written once and never again.
            if (!buffer.is_multisampled() && !first_ever)
            {
              continue;
            }

            switch (binding.name)
            {
              case aov::color: {
                // Premultiplied alpha (HdAovTokens comment, tokens.h:365). The
                // sky gradient is an environment we own, so a camera miss is
                // opaque sky, not a transparent hole - same as hdEmbree with a
                // dome light bound.
                const float rgba[4] = {float(radiance.x()), float(radiance.y()),
                                       float(radiance.z()), 1.0f};
                buffer.write(x, by, 4, rgba);
                break;
              }
              case aov::depth: {
                if (!did_hit) break;
                // world -> view -> clip, then map [-1,1] to [0,1].
                const vec3 clip = cam.proj_matrix().transform(cam.view_matrix().transform(primary.p));
                const float d = float((clip.z() + 1) / 2);
                buffer.write(x, by, 1, &d);
                break;
              }
              case aov::camera_depth: {
                if (!did_hit) break;
                const float d = float(primary.t);
                buffer.write(x, by, 1, &d);
                break;
              }
              case aov::normal: {
                if (!did_hit) break;
                const float n[3] = {float(primary.normal.x()), float(primary.normal.y()),
                                    float(primary.normal.z())};
                buffer.write(x, by, 3, n);
                break;
              }
              case aov::n_eye: {
                if (!did_hit) break;
                const vec3 ne = unit_vector(cam.view_matrix().transform_dir(primary.normal));
                const float n[3] = {float(ne.x()), float(ne.y()), float(ne.z())};
                buffer.write(x, by, 3, n);
                break;
              }
            }
          }
        }
      }
    }
  }

  // ray_color, plus an optional report of the primary hit for the non-color
  // AOVs. The recursive calls pass nullptr, so only the camera ray is reported.
  color trace(rng &generator, const ray &r, int depth, const hittable &world,
              hit_info *primary, bool *did_hit)
  {
    if (depth <= 0)
    {
      return color(0, 0, 0);
    }

    hit_info hit;

    if (world.hit(r, interval(0.001, infinity), hit))
    {
      if (primary)
      {
        *primary = hit;
        *did_hit = true;
      }

      color attenuation;
      ray bounced;
      if (hit.mat->scatter(generator, r, hit, attenuation, bounced))
      {
        return attenuation * trace(generator, bounced, depth - 1, world, nullptr, nullptr);
      }

      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }
};
```

Notes on the choices in there, in the order you will question them:

- **`seed_index` uses the y-down row, not the buffer row.** This is why step 5's images are
  effectively identical rather than merely statistically equivalent: the ray for pixel (x,y) is
  bit-identical to the current renderer's. It is also the right ownership — a pixel's noise should
  be a property of the framing, not of a buffer layout the host chose.
- **The primary hit is reported by an out-param, not by returning a struct.** `trace` recurses on
  itself with `nullptr`, so only the camera ray reports, and the hot path adds one branch. The
  alternative — tracing the primary ray separately for the depth AOV — doubles the primary
  intersection cost.
- **`first_ever` gates single-sampled AOVs.** hdEmbree gets this from its one-sample-per-pass
  loop plus "after the first pass, mark the single-sampled attachments as converged". We take
  batches of N samples, so the equivalent test is "this is the pixel's very first sample". When
  the interruptible loop arrives and passes become single-sample, this collapses back into
  hdEmbree's shape.
- **`is_converged()` is honoured but never set here.** Nothing in this task sets it; the render
  pass will (§9's `MarkAovBuffersUnconverged`, §10.2's end-of-render `SetConverged(true)`).
  Honouring it now is two lines and means the skip logic is already correct then.
- **`validate()` returns false instead of asserting.** hdEmbree emits `TF_CODING_ERROR`; the
  tracer has no error channel and must not gain a USD one. Returning 0 ms with nothing rendered is
  the honest tracer-side equivalent — and the delegate will do the `TF_CODING_ERROR` itself.

---

# Step 4 — Update the call sites and delete `framebuffer.h`

### `tracer/main.cpp`

```cpp
#include <cstdlib>
#include <iostream>

#include "camera.h"
#include "example_scenes.h"
#include "hittable_list.h"
#include "render_buffer.h"
#include "renderer.h"

int main(int argc, char *argv[])
{
  int i_scene = argc > 1 ? atoi(argv[1]) : 0;
  bool multithread = !(argc > 2 && atoi(argv[2]) > 0);
  int width  = argc > 3 ? atoi(argv[3]) : 400;
  int height = argc > 4 ? atoi(argv[4]) : 225;

  hittable_list world;
  camera_desc desc;
  load_scene(i_scene, world, desc);

  camera cam = desc.build(width, height);

  renderer r;
  r.max_bounces = 10;
  r.multithread = multithread;

  // color accumulates; depth is written once. Binding both here is what keeps
  // the single-sampled path exercised by the shipping cli.
  render_buffer color_buf, depth_buf;
  aov_bindings aovs = {allocate_aov(color_buf, aov::color, width, height),
                       allocate_aov(depth_buf, aov::depth, width, height)};

  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  double render_duration = r.render(cam, world, aovs, 50);

  color_buf.resolve();

  // ppm is top-to-bottom; render_buffer row 0 is the bottom image line.
  std::cout << "P3\n" << width << " " << height << "\n255\n";
  for (int y = height - 1; y >= 0; y--) {
    for (int x = 0; x < width; x++) {
      float rgba[4];
      color_buf.read(x, y, 4, rgba);
      write_color(std::cout, color(rgba[0], rgba[1], rgba[2]));
    }
  }

  auto per_pixel = render_duration / (double(height) * width);
  std::clog << "\rRendered scene " << i_scene << " in " << render_duration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;
}
```

Optional, and worth ten lines while you are here: print the depth AOV's coverage and range to
stderr, since it is otherwise unobservable from the cli. `$S/render_e2e.cpp` has that loop —
`written / width*height` pixels below 1.0, with min and max — and it is what produced the depth
numbers in "Pre-verified facts".

### `viewer/main.cpp`

Three edits.

```cpp
#include "render_buffer.h"          // was framebuffer.h

bool blit_buffer_to_texture(SDL_Texture *texture, const render_buffer &buffer)
{
  void *raw = nullptr;
  int pitch = 0;

  if (!SDL_LockTexture(texture, nullptr, &raw, &pitch)) {
    return false;
  }

  for (int y = 0; y < buffer.height(); y++) {
    // pitch is BYTES per row and may exceed width*4 (alignment padding),
    // so step in bytes and only then reinterpret
    uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(raw) + y * pitch);

    // the texture is top-down; render_buffer row 0 is the bottom image line
    const int by = buffer.height() - 1 - y;

    for (int x = 0; x < buffer.width(); x++) {
      float rgba[4];
      buffer.read(x, by, 4, rgba);
      int r, g, b;
      color_to_rgb8(color(rgba[0], rgba[1], rgba[2]), r, g, b);
      row[x] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
  }

  SDL_UnlockTexture(texture);
  return true;
}
```

Allocation, where `framebuffer buffer; buffer.allocate(...)` was:

```cpp
  render_buffer buffer;
  aov_bindings aovs = {allocate_aov(buffer, aov::color, render_width, render_height)};
```

And in the loop: `r.render(camera, world, aovs, samples_to_do)`, then `buffer.resolve();` before
the blit, and `buffer.height()` / `buffer.width()` instead of `.height` / `.width` in the texture
size check. The viewer deliberately binds **only** `color`, which exercises the "no single-sampled
AOV bound" path.

**`resolve()` is the one to drop by accident, and gate 2 cannot see it** — the cli resolves
independently. The viewer still compiles, runs and exits cleanly; the symptom is a permanently
black window, because `color` is multisampled so `write()` only ever touches the accumulation
while the blit reads the resolved buffer, which never leaves its `(0,0,0,0)` clear value. The
only way to catch it is to look at the window.

### Delete the old buffer

```bash
git rm tracer/framebuffer.h
grep -rn "framebuffer" tracer/ viewer/ hydra/     # must be empty
```

---

# Step 5 — GATE 2: the images did not change

```bash
cmake --build build --config Release
for s in 0 1 2; do
  ./build/tracer/Release/tracer_cli $s > $S/new_$s.ppm
done
for s in 0 1 2; do python3 $S/cmp.py $S/gold_$s.ppm $S/new_$s.ppm; done
```

`$S/cmp.py` exists — [[camera-refactor]] step 7's comparator, with the file name added to its output line.

**Expect, to the channel:**

```
new_0.ppm: 400x225 max|d|=1 rms=0.0027 differing=2/270000 (0.001%)
new_1.ppm: 400x225 max|d|=1 rms=0.0027 differing=2/270000 (0.001%)
new_2.ppm: 400x225 max|d|=0 rms=0.0000 differing=0/270000 (0.000%)
```

This is a much tighter gate than the camera refactor's, because the rays are bit-identical this
time; the two differing channels are float32-vs-double accumulation landing across an 8-bit
boundary. `max|d| > 1`, or more than a handful of differing channels, means a real bug:

| Symptom | Cause |
|---|---|
| Image is vertically mirrored | one of the two flips is missing — the renderer's `by` or the ppm loop's reversed `y`. Both missing looks *correct*; see step 6 |
| `size changed` | `allocate_aov` called with the window's size instead of the buffer's |
| Uniform black | `resolve()` not called, or `clear` called after the render |
| Uniformly dark/washed | reading `rgba` but writing gamma twice, or dividing by samples again after `resolve` |
| Top or bottom row wrong only | off-by-one in `height - 1 - y` |
| Every pixel differs slightly, `rms` ≈ 0.5 | seeds keyed on the *buffer* row instead of the y-down row: statistically fine, but you lost the tight gate — fix it rather than accepting it |
| `rms` ≈ 4.5 on ~40% of channels, **and two runs of the same binary disagree** | `samples_at(x, y)` where it should be `samples_at(x, by)`. Counts live at the *buffer* row, so the seed base comes from the mirrored pixel — 0 or 50 depending on whether that tile has run yet. Serial is stable but still wrong for half the image |
| `max\|d\|` in the tens | `max_bounces` is not 10, or samples are being counted twice (`write` bumps the count itself — do not also bump it) |

Then check it serially, which after the camera refactor's fix should agree just as closely:

```bash
./build/tracer/Release/tracer_cli 0 1 > $S/new_serial_0.ppm
python3 $S/cmp.py $S/gold_0.ppm $S/new_serial_0.ppm     # same 2/270000
```

---

# Step 6 — GATE 3: the flip is real, the sub-window works, the depth AOV is sane

**Why:** step 5 cannot distinguish "both flips correct" from "both flips missing", and it never
exercises a data window smaller than the buffer — which is the case §9 actually requires and the
only case where `height-1-y` and `max_y-y` differ.

Two scratchpad programs exist for this; both compile against the repo headers only.

```bash
g++ -std=c++17 -O2 -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/rowprobe $S/rowprobe.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
g++ -std=c++17 -O2 -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/subwindow $S/subwindow.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
$S/rowprobe 0 && $S/rowprobe 2 && $S/subwindow
```

`rowprobe` renders through `render_buffer` and prints the mean luminance of buffer row 0 and row
`h-1`. **Expect row 0 to be the dark one:**

```
scene 0: buffer row 0 mean = 0.311, buffer row 224 mean = 0.863
scene 2: buffer row 0 mean = 0.298, buffer row 224 mean = 0.434
```

Compare against the gold ppm's own rows — the ppm's *bottom* row means are 0.311 and 0.294, its
*top* rows 0.858 and 0.432. Row 0 of the buffer is the bottom of the image. If your numbers are
the other way round, both flips are missing and step 5 passed by cancellation.

`subwindow` renders scene 0 with `data_window = {100, 50, 299, 174}` into a full 400×225 buffer.
**Expect:**

```
window 200x125 at (100,50): sampled inside = 25000/25000, sampled outside = 0
window top line (y=50) mean = 0.864, bottom line (y=174) mean = 0.327  -> right side up
```

`sampled outside > 0` means the flip is computed from the window instead of the buffer height.
`UPSIDE DOWN` with `sampled outside = 0` means you used `window.max_y - y`, which is the flip that
is right only when the window fills the buffer.

Depth, from the cli's stderr (or `$S/e2e`):

```
scene 0: depth 74995/90000 pixels in [0.979987, 0.998495]
scene 1: depth 67473/90000 pixels in [0.799915, 0.953419]
scene 2: depth 90000/90000 pixels in [0.958198, 0.983129]
```

Coverage is the fraction of pixels that hit geometry — 83%, 75%, 100% — and the values are crushed
towards 1 because `camera_desc`'s `near_clip` is 0.1 against a `far_clip` of 1000. That is correct
clip-space depth, not a bug. Anything outside [0,1], or a scene 2 count below 90 000, means the
view/proj multiply order is transposed.

Then performance and the viewer:

```bash
./build/tracer/Release/tracer_cli 0 > /dev/null      # read ms/px on stderr
./build/viewer/Release/viewer
```

**Do not compare against 0.0622 as an absolute** — it is not reproducible across days; this
machine has run the *same* pre-refactor build at 0.095 ms/px under load. Build the pre-refactor cli
into the scratchpad and alternate runs, so both share the load:

```bash
git archive HEAD tracer | tar -x -C $S/pre
g++ -std=c++17 -O3 -DNDEBUG -I$S/pre/tracer -Ibuild/_deps/tbb-src/include \
    -o $S/pre_cli $S/pre/tracer/main.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
for i in 1 2 3; do $S/pre_cli 0 >/dev/null; ./build/tracer/Release/tracer_cli 0 >/dev/null; done
```

Expect the two series to interleave. A real regression means `resolve()` is being called inside the
render loop, or the per-sample `write` is hitting the format dispatch on a non-inlined path
(check you built Release). The viewer should converge exactly as before; it now costs one extra
full-frame pass per presented frame for `resolve()`, which at 90 000 pixels is not observable.

---

# Step 7 — GATE 4: the delegate-side shim compiles and behaves

Step 2 already compiled Appendix A's `HdWeekendRenderBuffer` and drove it through an
`HdRenderBuffer*`, so this step is a re-read rather than new work — but re-read it deliberately,
because it is the thing this whole task exists to make cheap:

- all 12 pure virtuals are one line each,
- `Map()` returns the tracer's resolved bytes, so the host reads what the renderer wrote,
- `GetFormat()` is a `static_cast`, not a switch,
- and the only USD types in it are `SdfPath`, `GfVec3i` and `HdFormat`.

Nothing from Appendix A is committed in this task. `hydra/` still declares no Bprim types; wiring
`CreateBprim`, `GetDefaultAovDescriptor` and the `HdAovTokens` mapping is `hydra wrapper` (0.3.0).

What *is* required here is that the plugin still builds, since you changed headers it includes:

```bash
source env.sh && cmake --build build-hydra -j8
```

**Expect** `hdWeekend` and `testHdWeekend` to build clean.

---

# Step 8 — Commit

```bash
git add tracer/render_buffer.h tracer/renderer.h tracer/main.cpp viewer/main.cpp \
        docs/plans/render-target.md docs/Roadmap.md
git rm tracer/framebuffer.h
git commit -m "render target: HdRenderBuffer-shaped buffer, AOV bindings, bottom-up line order

render_buffer implements HdRenderBuffer's semantics without USD: three
allocations (resolved output in the requested format, float32/int32
accumulation with the same component count, per-pixel sample counts),
resolve(), atomic map/unmap counting, a converged flag, and hdEmbree's
write/clear helpers. buffer_format's values are HdFormat's values, so the
delegate's render buffer is a forwarding shim and Map() hands the host the
exact bytes the renderer wrote.

Row 0 is now the bottom image line, matching HdRenderBuffer; the flip is one
subtraction in the renderer, so camera.h is unchanged. The ppm writer and the
sdl blit flip on read.

renderer takes an aov_bindings list instead of a framebuffer and fills every
bound AOV from one traced ray; ray_color becomes trace(), which reports the
primary hit so depth/normal have something to write. color is premultiplied
float32vec4, multisampled; depth is single-sampled clip-space [0,1].

Verified: 16 format-arithmetic cases agree with pxr/imaging/hd, the delegate
shim satisfies all 12 pure virtuals through an HdRenderBuffer*, resolve()
matches a double mean to 1.3e-06, and scenes 0-2 render to within 1/255 on 2 of
270000 channels of the pre-refactor images."
```

Then tick `render target refactor` in [[Roadmap]].

---

## Definition of done

- [x] `$S/test_rb` prints `16 formats agree`, `all checks passed` — worst \|d\| **7.01e-07** (the
      exact value tracks the test's random data; anything under ~1e-4 is float32 accumulation)
- [x] `$S/precision` shows worst 8-bit delta **1** — 0 at most sample counts
- [x] Scenes 0–2 within `max|d| <= 1` and under 10 differing channels of the step 0 goldens, multithreaded **and** serial
- [x] `$S/rowprobe` shows buffer row 0 matching the ppm's *bottom* row — in all three scenes
- [x] `$S/subwindow` shows 25000/25000 inside, 0 outside, `right side up`
- [x] Depth coverage 74995 / 67473 / 90000 with all values in [0,1]
- [x] `tracer_cli 0` ms/px interleaves with the pre-refactor build run alternately (see step 6)
- [x] `viewer` converges as before — **checked by eye**, it prints nothing per frame
- [x] `git diff --stat tracer/camera.h` — comment-only: the stale `framebuffer.h` reference is gone
- [x] `grep -rn "pxr/" tracer/ viewer/` is empty — the tracer is still USD-free
- [x] `grep -rn "framebuffer" tracer/ viewer/ hydra/` is empty
- [x] `grep -rn "get_pixel\|\.pixels\[" tracer/ viewer/` is empty — nothing divides on read any more
- [x] `build-hydra` still builds `hdWeekend` and `testHdWeekend`
- [x] No test file or test CMake target added to the repo

---

## As executed — 2026-08-26

All four gates pass. The numbers below are this run's; where they differ from the predictions
above, the difference is explained.

| Gate | Result |
|---|---|
| 1 — buffer vs `HdRenderBuffer` | `16 formats agree`, `resolve vs double mean: worst |d| = 7.01e-07`, `all checks passed` |
| 2 — images unchanged | scenes 0 and 1 `max|d|=1`, `differing=2/270000`; scene 2 byte-identical; serial identical |
| 3 — flip / sub-window / depth | buffer row 0 == the gold ppm's *bottom* row in all three scenes; `25000/25000` inside, `0` outside, right side up; depth 74995 / 67473 / 90000, all in [0,1] |
| 4 — `build-hydra` | `hdWeekend` and `testHdWeekend` clean |

**The scratchpad did not survive.** Only `gold_*.ppm` and `gold.md5` were still in
`…/da9c9438-…/scratchpad`; `render_buffer.h`, `renderer_rb.h`, `render_e2e.cpp`, `cmp.py`,
`precision.cpp`, `rowprobe.cpp`, `subwindow.cpp` and `test_render_buffer_hd.cpp` were gone and were
rewritten from this document. The goldens still reproduce byte-for-byte from the pre-refactor
build, so the gate itself was intact. Read "a working copy exists at `$S/…`" as a convenience,
never as a dependency — the document has to stand on its own, and it did.

**Three defects the gates caught, in the order they surfaced.**

1. `raycast(…, *hit_info, *did_hit)` — out-params dereferenced instead of addressed, so the tree
   did not compile. `did_hit` was also uninitialised, which would have fed a garbage `hit_info.p`
   to the depth AOV on every camera-ray miss.
2. `counter->samples_at(x, y)` where it should be `samples_at(x, by)`. This one is the reason to
   keep gate 2 tight: it showed up as `max|d|=63`, `rms≈4.5`, ~40% of channels differing, *and*
   two runs of the same binary disagreeing with each other. See the new row in step 5's table.
3. `viewer/main.cpp` never called `buffer.resolve()` — a permanently black window that compiles,
   runs and exits 0. Gate 2 is blind to it; only opening the window catches it.

**Two claims in this document read stronger than they are.**

- The `unsupported` set is float16 / int16 / uint16 **and `HdFormatFloat32UInt8`**, the
  depth-stencil block format at the tail of the enum. `component_of` sends it to `unsupported` and
  `allocate()` rejects it, which is the right behaviour — only the description was narrow.
- Gate 4 cannot fail yet. `hydra/` includes no tracer header (`mesh.h`, `renderPass.h`,
  `renderDelegate.h`, `rendererPlugin.h` only), so nothing in this task can break it. It becomes a
  real gate in `hydra wrapper` (0.3.0).

**Performance**, measured by alternating runs against the pre-refactor cli built from `HEAD`:
old 0.0663 / 0.0664 / 0.0690 vs new 0.0660 / 0.0677 / 0.0685 ms/px. Interleaved — no regression.

**The viewer prints nothing per frame**, so "converges as before" is an eyeball check. Its
`render_to_buffer()` helper — which logged `Rendered scene N (S samples) in Xs (Y ms/px)` — was
deleted in `d2e6fbc` (camera refactor), before this task. What survives is `Opening window...`, a
single `Finished rendering 1000 samples.` (~6 s in the Debug build `viewer/debug.sh` runs), and
`Closing window`. `ms_per_sample` is computed for the adaptive sample budget and never shown; if
the tile-driven loop wants progress visible, that is the number to print.

**`tracer/example_scenes.h` is part of this commit** and is missing from step 8's `git add` list
above: it included `framebuffer.h`, so the commit does not build without it. `tracer/camera.h` is
in it too, comment-only — the old comment claimed `framebuffer.h` is top-left origin, which is
wrong twice over now.

---

## Design notes — decisions made, recorded so they aren't re-litigated

**Row 0 is the bottom line.** The alternative (keep top-left origin, flip in the delegate) puts a
per-frame flip on the hot host-facing path and makes `Map()` a lie about the tracer's memory. The
flip has to exist somewhere because `dataWindow` is y-down; putting it on the renderer's write
index costs one subtraction per pixel and leaves `camera.h` alone.

**The tracer mirrors `HdFormat`'s enum values rather than translating them.** It is one comment
and a `static_assert` away from being obviously safe, and it removes a switch from the delegate
plus the possibility of the two tables drifting. The cost is a tracer enum with three groups
(`float16`, `int16`, `uint16`) it deliberately rejects — better than an enum that silently
renumbers if USD adds a format, which is exactly what the `static_assert` catches.

**float32 accumulation, not double.** §8.2 forces it (the sample buffer's component count must
match the resolved format's, and hdEmbree's sample format is float32). Measured cost: relative
error 2e-06 at 1000 spp, worst 8-bit channel delta 1. Keeping doubles would mean a separate tracer
accumulation buffer plus a convert on every resolve — paying memory and bandwidth for precision
that does not reach the output.

**The renderer takes `aov_bindings`, not a `render_buffer&`.** §9 requires `color` + `depth` at
minimum, and one traced ray must feed both — that is the whole reason `ray_color` grew a primary
hit report. A single-buffer signature would force either a second trace per AOV or a fake
"multi-channel" buffer, and neither survives contact with `normal`/`primId`.

**`aov` is a tracer enum, not a `TfToken`.** Tokens are USD. The enum's members are named after
`HdAovTokens` so the delegate's mapping is one switch in `hydra/`, and unsupported names get
rejected there — which is where hdEmbree rejects them too (`_ValidateAovBindings`).

**The AOV default table lives in the tracer.** `default_aov_descriptor` is §8.3's table, and it
has two consumers: the cli/viewer (to allocate and clear) and, later,
`GetDefaultAovDescriptor` in the delegate. One table means the delegate cannot declare a format
the renderer does not write.

**`depth` is clip-space [0,1]; `camera_depth` is ray t.** hdEmbree's `_ComputeDepth` distinguishes
exactly these two by token, with the same `(z+1)/2` mapping and the same "assume [0,1] depth
range" assumption. Both are single-sampled, so they are written on a pixel's first sample and
never averaged — averaging depth across jittered samples is wrong at silhouettes.

**Misses write opaque sky.** See "Alpha" above. Revisit when a dome-light toggle exists.

**`primId` is not implemented, and the switch has no default case.** Leaving the `switch` total
over the `aov` enum means adding a member is a compiler error at every write site, which is where
you want to be reminded. Ids need the scene graph; the two lines that will write them are obvious.

**No `render_tile()` yet.** §17.6 still requires the tracer to stop owning its parallel loop, and
that is the next item. It is now a smaller task than it was: `render_region` takes a rectangle and
a sample count, writes only through `write()`, and honours `is_converged()`. What is left is tile
indexing, `WorkParallelForN`, the `IsStopRequested()` cancellation points, and
`WorkGetConcurrencyLimitSetting()`.

---

## Appendix A — the delegate shim, for 0.3.0

Compiled and exercised by step 2's gate. Not committed in this task; it belongs to `hydra wrapper`
alongside `CreateBprim`, `GetSupportedBprimTypes` and `GetDefaultAovDescriptor`.

```cpp
class HdWeekendRenderBuffer : public HdRenderBuffer
{
public:
  HdWeekendRenderBuffer(SdfPath const &id) : HdRenderBuffer(id) {}

  bool Allocate(GfVec3i const &dims, HdFormat format, bool multiSampled) override
  {
    if (dims[2] != 1) {
      return false;
    }
    return _buf.allocate(dims[0], dims[1], buffer_format(int(format)), multiSampled);
  }

  unsigned int GetWidth() const override { return _buf.width(); }
  unsigned int GetHeight() const override { return _buf.height(); }
  unsigned int GetDepth() const override { return 1; }
  HdFormat GetFormat() const override { return HdFormat(int(_buf.format())); }
  bool IsMultiSampled() const override { return _buf.is_multisampled(); }

  void *Map() override { return _buf.map(); }
  void Unmap() override { _buf.unmap(); }
  bool IsMapped() const override { return _buf.is_mapped(); }

  void Resolve() override { _buf.resolve(); }
  bool IsConverged() const override { return _buf.is_converged(); }
  void SetConverged(bool cv) { _buf.set_converged(cv); }

  render_buffer &Buffer() { return _buf; }

protected:
  void _Deallocate() override { _buf.deallocate(); }

private:
  render_buffer _buf;
};
```

Two overrides it will also want, both for the same reason hdEmbree has them: `Sync()` and
`Finalize()` must stop the render thread before the buffer can be reallocated or destroyed, since
the render thread writes into it directly (`hdEmbree/renderBuffer.cpp:31-53`). That is
`HdRenderParam` work, hence 0.3.0.

---

## Next up

`interruptible tile-driven render loop` — §10.1, §10.2, §17.7. `render_region` is already the
right shape and already checks `is_converged()`; what is missing is a tile entry point the caller
drives (`WorkParallelForN` in the delegate, `tbb::parallel_for` in the viewer),
`IsStopRequested()` polled once per sample pass *and* inside the tile loop so pass 0 is
interruptible, `IsPauseRequested()` in a 10 ms sleep loop, and
`WorkGetConcurrencyLimitSetting()` / `threadLimit` honoured instead of saturating every core.

The one thing to decide there that reaches back into this task: who calls `set_converged(true)`.
It is the render *pass*, after the sample loop ends — not the renderer, which only reads the flag.
