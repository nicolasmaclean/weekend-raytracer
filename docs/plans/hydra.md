# Hydra render delegate — step-by-step

**Roadmap item:** `0.3.0 - hydra delegate` → `hydra delegate + usdview` (see [[Roadmap]])
**Context:** [[hydra-spec]] §5–§11, §13, §16, §17, §19, §20 · [[hdtiny-stub-delegate]] · [[roadmap-discussion-8-26]] §5, §6
**Environment and API facts below were re-verified against the real tree on 2026-08-31, at
commit `baefc6e` — i.e. with the BVH landed. See "Design notes → BVH".**

---

## What this task is

Spec §20 tiers **T1 through T4**, in one roadmap checkbox:

- **T1 — First pixels.** `HdRenderBuffer` Bprim, `HdCamera` Sprim, a render pass that reconciles
  camera + data window + AOV bindings, pixels in `color`.
- **T2 — Geometry.** `HdMesh` subclass: points, topology, triangulation, transform, visibility,
  through an `HdRenderParam` edit gateway.
- **T3 — Progressive + threaded.** `HdRenderThread`, `WorkParallelForN`, multisampled `color`,
  `IsConverged()`, pause/stop, render settings, `GetRenderStats()`.
- **T4 — Scope narrowing + fidelity.** Implicit-surface + ext-computation scene index plugins,
  the remaining AOVs, `displayColor` → material, instancing.

At the end you have a renderer you can drive from `usdview` and `usdrecord`, on real USD assets.

## The one thing to understand before starting

**Almost all of this is already written.** 0.2.0 was not "prep" in the loose sense — every item on
it was chosen to be a piece of a Hydra delegate that happens to also be usable from the CLI and the
SDL viewer. The delegate is mostly a *translation layer*, and the interesting question at each step
is "which tracer facility does this Hydra concept already correspond to", not "how do I implement
this".

Read this table before writing a line. Each row is a spec requirement that is already satisfied:

| hydra-spec requirement | Already in `tracer/` | Work left in `hydra/` |
|---|---|---|
| §6 `AcquireSceneForEdit()`: stop render, bump version, hand back scene | `scene::edit()` — its ctor calls `_stop_render()`, takes `_edit_lock`, bumps `_version`, sets `_dirty` (`scene.h:276-286`) | Wire `set_stop_render()` to `HdRenderThread::StopRender` |
| §6 monotonic scene version | `scene::version()`, `std::atomic<uint64_t>` | Compare it in `_Execute` |
| §8.2 resolved / accumulation / per-pixel count split | `render_buffer::_resolved`, `_samples`, `_sample_count`, `resolve()` | Wrap in an `HdRenderBuffer` subclass |
| §8.2 `Map`/`Unmap`/`IsMapped` mapper count | `render_buffer::map/unmap/is_mapped`, atomic `_mappers` | Forward, 3 one-liners |
| §8.3 the AOV descriptor table | `default_aov_descriptor()` — hdEmbree's table verbatim | Token ↔ enum switch |
| §9 data window, y-down, inclusive max | `rect2i` — "the same semantics as pxr GfRect2i" (`mat4.h:237`) | Field-for-field copy |
| §9 y-down window vs. bottom-up line order | `renderer::render_tiles` writes at `by = height-1-y`; `camera::get_ray` uses `ndc_y = 1 - 2*(…)` | **Nothing.** Pass the window through unflipped |
| §10.1 cancellation / pause polling | `render_control` — "USD-free stand-in for HdRenderThread's cancellation functionality" | 6-line adapter |
| §10.2 `WorkParallelForN` over tiles | `renderer::schedule`, a `tile_scheduler` injection point; `tile_grid` | 5-line lambda |
| §10.2 per-work-item RNG, `randomNumberSeed` | `rng`, `sample_seed(pixel, sample, frame_seed)`; `renderer::frame_seed` | Expose `frame_seed` as a render setting |
| §10.2 progressive pass loop, mark non-multisampled converged after pass 0 | `renderer::render()`, the `for (pass…)` loop | Nothing |
| §11 rays from matrices, ortho + perspective from one path | `camera::set_camera`, `is_orthographic()` (`round(proj.m[3][3])==1`), `get_ray` | `GfMatrix4d` → `mat4` copy |
| §7.4 triangulation, per-hit face index | `mesh::verts/tris/face`, `hit_info::element_id` | `HdMeshUtil::ComputeTriangleIndices` |
| §7.4 per-prim transform | `instance` — transforms the *ray*, sets `instance_id` | Wrap the mesh in an `instance` |
| §8.3 `primId`/`instanceId`/`elementId` | `hit_info::prim_id/instance_id/element_id`, filled by `scene`/`instance`/`mesh` | Nothing |
| §17.6 one thread pool | `tracer` links `TBB::tbb`; the delegate never links `tracer` and never includes `schedulers.h` | Keep it that way (step A0) |

Two layout coincidences are load-bearing and were deliberate:

1. **`mat4` is element-for-element `GfMatrix4d`** — row-major, row-vector, translation in
   `m[3][0..2]` (`mat4.h:8-13`; `GfMatrix4d` is `GfMatrixData<double,4,4>`, `matrix4d.h:691`).
   Conversion is a 128-byte `memcpy`, **no transpose**.
2. **`buffer_format` has the same integer values as `HdFormat`** — `unorm8 = 0` through
   `int32_vec4`, in the same order (`render_buffer.h:14-51` vs `hd/types.h:408-458`). Conversion is
   a `static_cast`, with one guard: `HdFormat` has a trailing `HdFormatFloat32UInt8` that
   `buffer_format` does not, and `float16`/`int16`/`uint16` exist in both but `allocate()` rejects
   them.

If either of those ever stops being true, the delegate breaks in a way that looks like a renderer
bug. Step A1 puts a static assertion on the second one.

---

## Starting state — verified 2026-08-31

T0 is done and still live. Confirmed on this machine today:

```bash
source env.sh
$USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -o 'renderer {[^}]*}'
# renderer {Storm,Embree,Weekend,GL}
```

`hydra/` currently holds the hdTiny-shaped stub: `rendererPlugin`, `renderDelegate`, `renderPass`,
`mesh`, `plugInfo.json`, `tests/testHdWeekend.cpp`, and a standalone `CMakeLists.txt` (793 lines
total, most of it Pixar's comments). It prints callbacks and renders nothing.
`SUPPORTED_SPRIM_TYPES` and `SUPPORTED_BPRIM_TYPES` are both empty; `GetRenderParam()` returns
`nullptr`.

Environment facts from [[hdtiny-stub-delegate]] all still hold: USD `v26.05` at `~/opt/OpenUSD`,
installed at `~/opt/usd_src_build`, `HD_API_VERSION 97`, C++17, GCC 13.3, namespace
`pxrInternal_v0_26_5__pxrReserved__`, `$USD_PY = ~/opt/usd-build-venv/bin/python`.

### The standing verification loop

```bash
source env.sh
cmake --build build-hydra --target install -j && ./build-hydra/testHdWeekend
```

Run it after every step. Each stage below ends with a **GATE** that is stronger than this.

### What a single-frame gate cannot see, and the test that saw it

Both the loop above and every `usdrecord` gate in this document render **one frame**. A whole class
of delegate bug is invisible to one frame, because it only exists once there is state left over
from a previous one — the accumulator trap in step A4 is the archetype, and it was found by hand in
`usdview` at the end of stage B rather than by any gate here.

There is no such test in the tree right now. This is the recipe, so it can be rebuilt when it is
next needed — most likely at gate C, where the render thread makes the same state persist across
frames for real:

- **The invariant.** *Rendering camera B as frame N must be pixel-identical to rendering camera B
  as frame 1 of a fresh delegate.* Anything that leaks between frames breaks it, and the assertion
  needs no golden image — just two runs compared to each other. Our render is deterministic
  (`renderer::frame_seed = 0`, and `sample_seed(pixel, sample_base, frame_seed)`), so with the bug
  fixed the two are bit-identical and the tolerance can be ~1e-6 rather than a perceptual one.
- **The harness.** Build the usual delegate + `HdRenderIndex` + `HdUnitTestDelegate` (with
  `AddCube`, which adds a *mesh*), then an `HdxTaskController` — the same controller `usdview`
  drives, so this is a real reproduction and not a mock. `SetRenderBufferSize`, `SetFraming`,
  `SetRenderOutputs({color, depth})`, `SetEnablePresentation(false)`. A frame is
  `SetFreeCameraMatrices(view, proj)` then `HdEngine::Execute`. Wrap the whole thing in a class so
  each "session" is an independent delegate that can be torn down — comparing sessions is the
  entire point.
- **The one trap.** Do **not** hand `HdEngine::Execute` the controller's full
  `GetRenderingTasks()`. `HdxAovInputTask`, colorCorrection and present read the AOVs back through
  Hgi, and this delegate is deliberately GPU-free (§17.5) — `HdxAovInputTask::Prepare` segfaults on
  the null Hgi. Filter `GetRenderingTaskPaths()` down to the paths whose name starts with
  `renderTask` (`HdxTaskController::_GetRenderTaskPath` names them `renderTask_<materialTag>`) and
  look each up with `renderIndex->GetTask()`.
- **Reading pixels back.** `taskController->GetRenderOutput(HdAovTokens->color)`, then `Resolve()`,
  `Map()`, copy, `Unmap()`. Our default `color` descriptor is `float32_vec4`, `depth` is
  `float32`. Compare with a mean-absolute-difference — ghosting is a blend, so it shows up as a
  small but unmistakably non-zero average.
- **Cases worth having.** A guard that the two cameras actually produce different images (or every
  other assertion passes vacuously); one camera move; three moves ending on B; and a return to a
  previously-rendered camera, which must be idempotent.
- **What it measured.** With the clear missing, mean |diff| was 0.0245 after one move, 0.0405 after
  three — compounding, as the `1/N` blend predicts — and `depth` was off by 0.0339, the stranded
  single-sample AOV. With the clear in place, all four are exactly 0.
- **The gotcha that will cost you twenty minutes.** The test loads the delegate as a *plugin* from
  `$HDW_INSTALL`. `cmake --build build-hydra --target <test>` does not update it. Always build
  `--target install` when you change delegate code, or you will be testing the previous `.so` and
  drawing confident conclusions from it.

---

## What is explicitly NOT in this task

| Not now | Why / when |
|---|---|
| UsdLux lights | §12, tiered and optional. Everything renders against the sky gradient, as hdEmbree does against ambient |
| Material *networks* / `UsdPreviewSurface` | §13. hdEmbree ships no material support at all. We do `displayColor` → `lambert` in stage D and stop |
| Texture mapping / primvar interpolation at a hit | §7.4, needs `hdEmbree/sampler.h` + `meshSamplers.h` ported. 0.4.0 |
| `HdRenderSettings` Bprim (`UsdRender` scene description) | §15 mechanism 3. Mechanism 2 (descriptors) is enough for interactive + `usdrecord` |
| Motion blur, `Restart()`, `InvokeCommand()` | §20 T5 |
| `HdBasisCurves`, `HdPoints`, volumes | One Rprim is the documented minimum (§5.1) |
| Multiple concurrent render passes | hdEmbree does not support them either (§9) |
| Hydra 2.0 `_CreateRenderer` / scene-index-native delegate | §4: falls back to 1.0 + emulation automatically |

---

## New files

Everything lands in `hydra/`, with exactly one sanctioned exception: the commit epoch in step
D4a, which is a `tracer/` change [[bvh]] deferred to this task. Otherwise, if you find yourself
editing `tracer/`, stop and check whether you are about to break the CLI or the viewer.

| File | Stage | Roughly |
|---|---|---|
| `convert.h` | A | `inline ToMat4` / `ToAov` — the helpers `renderer.cpp`, `mesh.cpp` and `renderDelegate.cpp` all need |
| `renderBuffer.h` / `.cpp` | A | `HdWeekendRenderBuffer` — wraps `render_buffer` |
| `renderParam.h` | A | `HdWeekendRenderParam` — the §6 edit gateway |
| `renderer.h` / `.cpp` | A | `HdWeekendRenderer` — owns `camera`, `renderer`, `aov_bindings`, the control adapter |
| `config.h` / `.cpp` | C | Render setting tokens + `TfGetEnvSetting` defaults |
| `instancer.h` / `.cpp` | D | `HdWeekendInstancer` |
| `implicitSurfaceSceneIndexPlugin.h` / `.cpp` | D | §16 scope narrowing |
| `debugCodes.h` / `.cpp` | D | `TF_DEBUG_CODES` |

Add each new `.cpp` to `add_library(hdWeekend SHARED …)` as you create it.

---

# Stage A — T1, first pixels

**Goal:** `usdrecord --renderer Weekend --disableGpu` on a scene with no geometry writes a PNG
containing the sky gradient, at the right resolution, right way up.

An empty scene is the ideal T1 subject: every ray misses, `renderer::raycast` returns the
`(1,1,1) → (0.5,0.7,1)` gradient off `unit_vector(r.direction()).y()`, and a **vertically
asymmetric image you can eyeball for the y-flip** falls out for free. Do not add geometry until
gate A passes — a black image with geometry in it has four possible causes instead of one.

## Step A0 — Lock the tracer/USD boundary

**Why:** the delegate compiles against `tracer/` headers but must never link the `tracer` CMake
target, because that target does `target_link_libraries(tracer INTERFACE TBB::tbb)` against
vendored oneTBB 2023.1 (`libtbb.so.12`) while USD's `hd` brings its own `TBB::tbb`, which here is
TBB 2020.3 (`libtbb.so.2`). Two pools in one process is §17.6's oversubscription bug, and two
`add_library(TBB::tbb …)` calls in one CMake project is a hard configure error.

The rule that keeps it safe: **`hydra/` must never include `tracer/schedulers.h`.** That is the
only tracer header that includes TBB (`grep -rn tbb tracer/*.h` finds `schedulers.h` and nothing
else). The delegate gets its parallelism from `WorkParallelForN` instead (step C2), which is the
whole reason `renderer::schedule` is an injection point.

Put that in a comment at the top of `hydra/renderer.h` so the next person doesn't "tidy up" the
includes.

## Step A0.1 — Fix the include path before writing a second header

The line the stub ships is **not** usable as-is:

```cmake
# hydra/CMakeLists.txt:23 — replace this
target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tracer)
# with this
target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
```

and spell every tracer include with a `tracer/` prefix: `#include "tracer/renderer.h"`.

**Why.** `hydra/` and `tracer/` both contain a `renderer.h` and a `mesh.h`. A quoted include
searches the *including file's own directory first*, so `hydra/renderer.h` writing
`#include "renderer.h"` resolves to itself, `#pragma once` makes it a no-op, and the tracer
`renderer` type is simply absent:

```
hydra/renderer.h:3:16: error: 'renderer' does not name a type
```

`hydra/mesh.cpp` hits the identical wall in stage B, where it needs both its own `mesh.h` and
tracer's. Verified on this machine, GCC 13.3, 2026-08-31.

Three fixes compile; the prefix is the one to take:

| | Form | |
|---|---|---|
| A | `#include <renderer.h>` | works — angle brackets skip the current directory — but reads like a system header |
| B | `#include "../tracer/renderer.h"` | works, unambiguous, brittle if the include dir moves |
| C | `-I` the repo root + `#include "tracer/renderer.h"` | **take this one** |

C removes the whole class of collision rather than the two instances of it, and every use site
says which header it means. Two things verified before recommending it:

- **It does not disturb `tracer/`'s own includes.** Tracer headers include each other with plain
  quotes (`#include "camera.h"` inside `tracer/renderer.h`), which still resolve relative to the
  including tracer header's directory. The full `scene.h` / `renderer.h` / `camera.h` stack
  compiles through the prefix.
- **It does not weaken step A0.** `-I` at the repo root does *not* put TBB on the search path —
  `#include <tbb/parallel_for.h>` still fails to find a header, and `vendor/tbb/` holds only a
  `CMakeLists.txt`.

Cost is two lines: the CMake line above, and `renderBuffer.h`'s `#include "render_buffer.h"` →
`#include "tracer/render_buffer.h"`.

## Step A1 — `HdWeekendRenderBuffer` (Bprim)

**Why first:** it is the least coupled piece and it is almost entirely forwarding. §8.1's 12 pure
virtuals map one-to-one onto `render_buffer`.

`hydra/renderBuffer.h`:

```cpp
// hydra/renderBuffer.h
#include "pxr/imaging/hd/renderBuffer.h"
#include "tracer/render_buffer.h"   // tracer, via the repo-root -I from step A0.1

PXR_NAMESPACE_OPEN_SCOPE

// tracer's buffer_format is HdFormat with the same integer values
// (render_buffer.h:14 vs hd/types.h:408). Verified for the range we allocate.
static_assert(int(buffer_format::unorm8)       == int(HdFormatUNorm8), "");
static_assert(int(buffer_format::float32_vec4) == int(HdFormatFloat32Vec4), "");
static_assert(int(buffer_format::int32_vec4)   == int(HdFormatInt32Vec4), "");

class HdWeekendRenderBuffer final : public HdRenderBuffer
{
public:
    HdWeekendRenderBuffer(SdfPath const& id) : HdRenderBuffer(id) {}

    bool Allocate(GfVec3i const& dims, HdFormat format, bool multiSampled) override
    {
        if (dims[2] != 1) {                       // §8.1: depth==1 only
            TF_WARN("Only 2D render buffers are supported");
            return false;
        }
        return _buf.allocate(dims[0], dims[1],
                             static_cast<buffer_format>(int(format)), multiSampled);
    }

    unsigned int GetWidth()  const override { return _buf.width();  }
    unsigned int GetHeight() const override { return _buf.height(); }
    unsigned int GetDepth()  const override { return 1; }
    HdFormat GetFormat()     const override { return static_cast<HdFormat>(int(_buf.format())); }
    bool IsMultiSampled()    const override { return _buf.is_multisampled(); }

    void* Map()            override { return _buf.map(); }
    void  Unmap()          override { _buf.unmap(); }
    bool  IsMapped() const override { return _buf.is_mapped(); }
    void  Resolve()        override { _buf.resolve(); }
    bool  IsConverged() const override { return _buf.is_converged(); }
    void  SetConverged(bool c)      { _buf.set_converged(c); }

    // The renderer writes through this, not through HdRenderBuffer.
    render_buffer& Buffer() { return _buf; }

protected:
    void _Deallocate() override { _buf.deallocate(); }

private:
    render_buffer _buf;
};

PXR_NAMESPACE_CLOSE_SCOPE
```

Notes that matter:

- **Do not reimplement `Sync()` — but from stage C you must wrap it.** The base
  `HdRenderBuffer::Sync` already pulls `HdRenderBufferDescriptor` from the scene delegate and calls
  your `Allocate` (`hd/renderBuffer.cpp`), then clears `AllDirty`. Free correctness, and it stays
  free: never replace that body, always delegate to it. What the base cannot know is that *we* hand
  the render thread a raw `render_buffer*` and let it write into that memory directly (see A3.3's
  `_TracerBuffer`). So both points where the memory can move or vanish have to stop the render
  first, exactly as hdEmbree does (`hdEmbree/renderBuffer.cpp`):

  ```cpp
  void Sync(HdSceneDelegate *sd, HdRenderParam *rp, HdDirtyBits *dirtyBits) override
  {
      // A description change means Allocate() is about to resize _buf.
      if (*dirtyBits & DirtyDescription) {
          static_cast<HdWeekendRenderParam *>(rp)->StopRender();
      }
      HdRenderBuffer::Sync(sd, rp, dirtyBits);
  }
  void Finalize(HdRenderParam *rp) override
  {
      static_cast<HdWeekendRenderParam *>(rp)->StopRender();
      HdRenderBuffer::Finalize(rp);
  }
  ```

  Write these in **step A1**, even though nothing is threaded until C1 — in stages A and B they are
  dead code, and by stage C the symptom is a SIGSEGV at teardown inside `render_buffer::write`, on a
  buffer the render index has already destroyed. Nothing in that stack points back at Hydra.

  Note it is `StopRender()`, not `AcquireSceneForEdit()` — render buffers are not scene data and
  must not bump `_sceneVersion`. That is the second method `HdWeekendRenderParam` exists to provide,
  and what its otherwise-unused `_renderThread` member is for (§6).
- **Do not implement `GetResource()`.** Returning the base class's empty `VtValue` is what makes
  `HdxAovInputTask` fall back to `Map()` and upload the CPU pixels into an `HgiTexture` itself
  (§17.2). That is how a GPU-free delegate gets viewport presentation and picking.
- `SetConverged` is not part of the base API; hdEmbree adds it and so do we (§8.1).

Then in `renderDelegate.cpp`:

```cpp
// hydra/renderDelegate.cpp
const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_BPRIM_TYPES = {
    HdPrimTypeTokens->renderBuffer,
};
```

`CreateBprim` returns `new HdWeekendRenderBuffer(bprimId)` for `renderBuffer` and
`TF_CODING_ERROR`s otherwise; `CreateFallbackBprim` returns
`new HdWeekendRenderBuffer(SdfPath::EmptyPath())` (§5.1: a fallback prim is default-constructed and
bound to the empty path, and will never sync); `DestroyBprim` deletes.

## Step A2 — `camera` Sprim

Three lines, and it is the difference between "loads" and "can be asked to draw" — `HdxRenderTask`
requires it (§5.1).

```cpp
// hydra/renderDelegate.cpp
const TfTokenVector HdWeekendRenderDelegate::SUPPORTED_SPRIM_TYPES = {
    HdPrimTypeTokens->camera,
};
```

`CreateSprim` returns `new HdCamera(sprimId)`; `CreateFallbackSprim` returns
`new HdCamera(SdfPath::EmptyPath())`; `DestroySprim` deletes. **Do not subclass `HdCamera`** (§11) —
it is concrete (`hd/camera.h:97`, no pure virtuals) and the render pass gets everything it needs
from `HdRenderPassState`, not from the Sprim.

## Step A3 — `HdWeekendRenderer`

**Why a wrapper at all**, given `tracer/renderer.h` already exists: the tracer's `renderer` is a
struct whose `render()` takes `(camera, world, aovs, control)` fresh on every call. Hydra needs an
object that *holds* that state across `_Execute` calls so the pass can diff against it, and that
owns the translation from Hydra types. This is hdEmbree's `renderer.h` role, at about a fifth the
size.

`hydra/renderer.h` — the includes first. These are hdEmbree's `renderer.h` list minus Embree and
its own context/light headers, plus the tracer side. Everything the class declaration names, with
nothing left to arrive transitively — the same self-sufficiency `renderBuffer.h` needed for
`GfVec3i` and `TF_WARN`:

```cpp
// hydra/renderer.h — includes
#include "pxr/pxr.h"                      // PXR_NAMESPACE_OPEN_SCOPE
#include "pxr/imaging/hd/aov.h"           // HdRenderPassAovBinding{,Vector} (aov.h:100,137)
#include "pxr/imaging/hd/renderThread.h"  // HdRenderThread
#include "pxr/base/gf/matrix4d.h"         // GfMatrix4d
#include "pxr/base/gf/rect2i.h"           // GfRect2i

#include "tracer/scene.h"                 // scene
#include "tracer/camera.h"                // camera
#include "tracer/renderer.h"              // renderer, mark_unconverged()
#include "tracer/render_buffer.h"         // aov_bindings
```

`HdRenderThread` appears only as a pointer parameter, so `class HdRenderThread;` would do — include
it anyway, as hdEmbree does, because stage C1's `hd_render_control` needs the complete type. The
last two tracer headers arrive transitively via `tracer/renderer.h`; list them regardless.

`renderer.cpp` adds `<cstring>` (the `memcpy`), `"pxr/imaging/hd/tokens.h"` (`HdAovTokens`),
`"convert.h"` and `"renderBuffer.h"`.

The `tracer/` prefix on those includes is not decoration — see step A0.1, without it
`#include "renderer.h"` silently includes this file.

Then the shape:

```cpp
// hydra/renderer.h
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
```

### A3.1 — matrices

Because `mat4` and `GfMatrix4d` have identical layout (see "layout coincidences" above), this is a
copy, **not** a transpose.

**The conversion goes in `hydra/convert.h`, not in `renderer.cpp`.** It is called from three
translation units — here, from `mesh.cpp` at B2.2 and B2.3, and from `instancer.cpp` at D4 — so a
file-static has the wrong linkage and you would end up with three copies of the one thing that must
never drift. `inline` in a shared header, and note the name loses its underscore: `_ToMat4` at
namespace scope is a reserved identifier, tolerable for a `.cpp` file-static and not for a header.

```cpp
// hydra/convert.h
inline mat4 ToMat4(const GfMatrix4d &m)
{
    mat4 out;
    std::memcpy(&out.m[0][0], m.GetArray(), 16 * sizeof(double));
    return out;
}
```

Only `SetCamera` itself lives in `renderer.cpp`:

```cpp
// hydra/renderer.cpp
void HdWeekendRenderer::SetCamera(const GfMatrix4d &view, const GfMatrix4d &proj)
{
    _cam.set_camera(ToMat4(view), ToMat4(proj));   // computes inverses, detects ortho
}
```

`camera::set_camera` inverts both matrices itself and sets `orthographic` from
`round(proj.m[3][3]) == 1.0` — the same test hdEmbree does, on the same element, because the
layouts match.

**All four claims were compiled against real `GfMatrix4d` on 2026-08-31**, rather than inferred
from the headers:

```
element-for-element (no transpose): ok
translation in m[3][0..2]: ok  (7 8 9)
Transform() agrees: bit-exact  usd=(2.9848542201551278 -3.7 5.135498472191534)
                             tracer=(2.9848542201551278 -3.7 5.135498472191534)
proj.m[3][3]: perspective=0  orthographic=1
```

(`GfMatrix4d::Transform` against `mat4::transform` on a rotate-plus-translate, and the ortho
discriminator on both projection shapes.)

> **If the image comes out transposed or mirrored, do not "fix" it with a transpose here.** That
> would paper over a layout drift between `mat4` and `GfMatrix4d` that will bite again in
> `GetTransform`. Diagnose it instead: build the same camera from `camera_desc` in the CLI and from
> `usdrecord`, and compare the matrices element by element.

### A3.2 — data window

`rect2i` and `GfRect2i` are both y-down with an **inclusive** max (`mat4.h:237`;
`GfRect2i::GetMaxX` is inclusive). So:

```cpp
// hydra/renderer.cpp
void HdWeekendRenderer::SetDataWindow(const GfRect2i &w)
{
    _dataWindow = w;
    _cam.data_window = { w.GetMinX(), w.GetMinY(), w.GetMaxX(), w.GetMaxY() };
}
```

**Do not flip anything here.** hdEmbree flips the window into buffer space and then iterates with
y-up NDC; the tracer instead iterates in y-down window space, computes `ndc_y = 1 - 2*(…)`
(`camera.h:48-49`) and flips on write with `by = height - 1 - y` (`renderer.h:152`). The two are
algebraically identical — for `minY=2, maxY=5, height=10`, hdEmbree touches buffer rows `{4,5,6,7}`
and so does the tracer — but only if you pass the window through untouched. This is the flip
[[roadmap-discussion-8-26]] §5.2 warned would be easy to lose six weeks out; it was not lost, it
was solved inside `tracer/`, and the delegate's job is to not re-solve it.

### A3.3 — AOV bindings

Translate `HdRenderPassAovBindingVector` into tracer's `aov_bindings`, keeping both. The token
switch is the only real content — and it goes in `convert.h` alongside `ToMat4`, for the same
reason: `GetDefaultAovDescriptor` in `renderDelegate.cpp` needs it too at step D2.

```cpp
// hydra/convert.h
inline bool ToAov(TfToken const &name, aov *out)
{
    if      (name == HdAovTokens->color)       *out = aov::color;
    else if (name == HdAovTokens->depth)       *out = aov::depth;
    else if (name == HdAovTokens->cameraDepth) *out = aov::camera_depth;
    else if (name == HdAovTokens->normal)      *out = aov::normal;
    else if (name == HdAovTokens->Neye)        *out = aov::n_eye;
    else if (name == HdAovTokens->primId)      *out = aov::prim_id;
    else if (name == HdAovTokens->instanceId)  *out = aov::instance_id;
    else if (name == HdAovTokens->elementId)   *out = aov::element_id;
    else return false;
    return true;
}
```

The eight `aov` enumerators were named to match `HdAovTokens` for exactly this switch
(`render_buffer.h:313`). For each binding, `static_cast` the `HdRenderBuffer*` down to
`HdWeekendRenderBuffer*` and push `{name, &buf->Buffer()}`. The cast is safe because every render
buffer in this render index came from this delegate's `CreateBprim` — but `TF_VERIFY` a
`dynamic_cast` in debug anyway, because the fallback buffers in step A4 are a second source of
them.

**Skip bindings whose token you don't recognise.** Hosts request AOVs you never declared; dropping
them is correct, `TF_CODING_ERROR`ing on them is not.

`Clear()` walks `_aovBindings` and uses each binding's `clearValue` if it holds one, falling back
to `default_aov_descriptor(name).clear_value`. `render_buffer::clear` already distinguishes "set
every resolved pixel to the clear value and zero the accumulation" from a `memset`, which is what
`depth` clearing to `1.0` needs.

## Step A4 — `HdWeekendRenderPass::_Execute`

This is §9, and it is the piece whose shape is genuinely non-obvious: **`_Execute` is not "draw a
frame", it is "reconcile requested state against current state, and restart the render if anything
changed."** Write it in the five-way form now even though stage A has no render thread —
retrofitting the structure later is worse than carrying a couple of `nullptr`s for a stage.

Port `pxr/imaging/plugin/hdEmbree/renderPass.cpp` `_Execute` almost verbatim. The order is
load-bearing:

```
# hydra/renderPass.cpp — HdWeekendRenderPass::_Execute(), in this order
needStartRender = false
1. sceneVersion    != last  ->  needStartRender
2. settingsVersion != last  ->  StopRender; re-read every setting; needStartRender   [stage C]
3. view/proj       changed  ->  StopRender; SetCamera;             needStartRender
4. data window     changed  ->  StopRender; SetDataWindow; (re)allocate fallbacks; needStartRender
5. AOV bindings    changed  ->  StopRender; SetAovBindings;        needStartRender
if (needStartRender) { MarkAovBuffersUnconverged(); Clear(); StartRender(); }
```

Note where `Clear()` sits. hdEmbree writes it inside branch 5 *and* again at the top of its render
callback (`hdEmbree/renderDelegate.cpp:85`, `renderer->Clear(); renderer->Render(renderThread);`) —
the branch-5 one only exists so that one clear happens on this thread before the thread starts. The
clear that actually matters is the unconditional one on every restart. In stage A/B, where `Render`
is a synchronous call on this thread, the single unconditional `Clear()` above covers both; in
stage C it migrates into the render callback (step C1) and this line goes with it. Getting this
wrong is the ghosting bug described below.

Four things people get wrong here:

**Every restart must clear, not just a binding change.** This one does not show up in stage A at
all, and it is the reason `usdrecord` is a weak gate. `color` is multisampled:
`render_buffer::write` *accumulates* into `_samples` and bumps `_sample_count`, and
`render_buffer::resolve()` divides the running sum by the running count. If `_Execute` restarts the
render without clearing — which is what happens on a camera move, since the AOV bindings are
identical frame to frame — the second frame lays a sample from the new camera on top of the sample
from the old one, and the host is shown their average. In `usdview` that reads as **ghost images of
where the object used to be, getting worse the more you tumble** (the blend is `1/N` weighted, so
it compounds). It also strands the *single-sampled* AOVs: `renderer::render_tiles`
(`tracer/renderer.h:154`) takes `sample_base` from the multisampled color buffer's `samples_at()`
and only writes a non-multisampled buffer when `sample_base == 0`, so `depth` freezes at frame 1
and never updates again. Found on 2026-09-01, at the end of stage B.

**Empty AOV bindings are legal input but not a legal render state.** If
`renderPassState->GetAovBindings()` is empty, synthesize `color` + `depth` bindings against two
`HdWeekendRenderBuffer`s the pass *owns as members*. hdEmbree allocates its fallback color as
`HdFormatUNorm8Vec4` multisampled and depth as `HdFormatFloat32` single-sampled, and only when
`!renderPassState->GetFraming().IsValid()`. Note the extra clause in hdEmbree's binding check:
`if (_aovBindings != aovBindings || _renderer->GetAovBindings().empty())` — the second clause
forces the first pass through even when both are empty.

**Support both framing APIs.** Copy `_GetDataWindow` exactly:

```cpp
// hydra/renderPass.cpp — file-static helper
static GfRect2i _GetDataWindow(HdRenderPassStateSharedPtr const& renderPassState)
{
    const CameraUtilFraming &framing = renderPassState->GetFraming();
    if (framing.IsValid()) {
        return framing.dataWindow;
    }
    const GfVec4f vp = renderPassState->GetViewport();
    return GfRect2i(GfVec2i(0), int(vp[2]), int(vp[3]));
}
```

`usdview` uses the framing API; older hosts and some test harnesses use the viewport. You need
both, and the fallback-buffer allocation is gated on which one you got.

**The destructor must stop the render** (stage C, once there is a thread) — the render thread may
still be writing into the pass's own fallback buffers.

`IsConverged()` returns the AND over all bound render buffers, or the pass's cached flag when using
fallbacks. In stage A, with a synchronous render, return `true`.

For stage A only, "start the render" is a direct synchronous call:

```cpp
// hydra/renderPass.cpp — in HdWeekendRenderPass::_Execute()
if (needStartRender) {
    _renderer->MarkAovBuffersUnconverged();
    _renderer->Clear();              // a restart is a NEW accumulation — see above
    _renderer->Render(nullptr);      // blocking; becomes StartRender() in stage C
}
```

Set `renderer::samples_to_converge = 1` for now. A blocking multi-thousand-sample render inside
`_Execute` will hang `usdview` (§17.7) — that is what stage C fixes, and it is why stage A is
verified with `usdrecord`, not `usdview`.

**Delete this line in step C4**, when branch 2 starts reading `convergedSamplesPerPixel`. Left in,
it silently pins the image to one sample: the render thread runs, the HUD counter reaches 1 and
stops, and the viewport looks static — which reads exactly like "the thread never started" and
sends you debugging the wrong thing.

## Step A5 — Wire the renderer into the delegate

The delegate owns `HdWeekendRenderer` and hands it to the pass:

```cpp
// hydra/renderDelegate.cpp
HdRenderPassSharedPtr HdWeekendRenderDelegate::CreateRenderPass(
    HdRenderIndex *index, HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr(
        new HdWeekendRenderPass(index, collection, &_renderThread, &_renderer, &_sceneVersion));
}
```

with `std::atomic<int> _sceneVersion{1}` as a delegate member. Keep `_renderThread` as a member
from now even though it is unused until stage C — the pass's constructor signature then doesn't
change.

## GATE A — a picture of the sky

Write `/tmp/empty.usda` holding just a `defaultPrim` Xform and nothing else:

```
#usda 1.0
( defaultPrim = "World" upAxis = "Y" )
def Xform "World" {}
```

then build and record:

```
source env.sh
cmake --build build-hydra --target install -j
$USD_PY $USD_ROOT/bin/usdrecord --renderer Weekend --disableGpu --imageWidth 400 /tmp/empty.usda /tmp/out.png
```

**Expected:** a 400-wide PNG holding a vertical gradient — white at the bottom, light blue at the
top, per `renderer::raycast`'s miss path (`(1-a)·(1,1,1) + a·(0.5,0.7,1)` with
`a = 0.5·(dir.y+1)`).

The gate is not "a PNG exists", it is **the gradient is the right way up**. USD's Y-up default puts
`+Y` at the top of frame, `dir.y` is positive there, so the *top* is blue. If it is inverted, the
flip is wrong — and the fix is in the delegate (you flipped something in `SetDataWindow`), not in
`tracer/`, which the CLI and the viewer have already validated.

Check, in this order, if it fails:

1. `TF_DEBUG=HD_RENDER_PASS_EXECUTE` — is `_Execute` called at all?
2. Print `_dataWindow` and the buffer dimensions in `SetAovBindings`. A zero-area window means
   `_GetDataWindow` took the wrong branch.
3. `renderer::render()` silently returns an empty `render_stats` if `validate()` fails — the window
   must be non-empty and fully inside the buffer (`renderer.h:302-303`). Add a `TF_WARN` when
   `stats.completed_samples == 0`; you will want it again later.
4. Empty AOV bindings now fail `renderer::validate` cleanly and return an empty `render_stats`.
   They used to be undefined behaviour — `validate` read `aovs[0].buffer` before testing
   `aovs.empty()` — fixed on 2026-08-31 as a standalone `tracer/` commit, ahead of this task and
   separate from D4a's epoch. Verified byte-identical on all 7 example scenes. You still want the
   fallback synthesis in step A4: a clean `false` renders nothing, which is a black frame rather
   than a crash, but it is still a black frame.
---

# Stage B — T2, geometry

**Goal:** a cube in a `.usda` renders as a cube.

## Step B1 — `HdWeekendRenderParam`, the edit gateway

§6 is the one piece of Hydra design you cannot shortcut, because `Sync()` runs **in parallel across
prims** while the render thread may be reading the scene. The required pattern is that the render
param is the *only* route to the scene, so that "stop the render first" cannot be forgotten.

The tracer already implements the gateway. `scene_edit`'s constructor (`scene.h:276-286`) calls
`_stop_render()`, takes `_edit_lock`, bumps `_version`, and sets `_dirty` — in that order — and the
lock is held for the object's lifetime, so concurrent `Sync()` calls serialize on it. So
`HdWeekendRenderParam` is a thin thing:

```cpp
// hydra/renderParam.h
class HdWeekendRenderParam final : public HdRenderParam
{
public:
    HdWeekendRenderParam(scene *s, HdRenderThread *thread, std::atomic<int> *sceneVersion)
        : _scene(s), _renderThread(thread), _sceneVersion(sceneVersion) {}

    // The ONLY way a prim may touch the scene. Stops the render, takes the
    // edit lock, and bumps both version counters. Move-only; hold it for the
    // shortest span you can — it serializes every other prim's Sync().
    scene_edit AcquireSceneForEdit()
    {
        (*_sceneVersion)++;          // the int the render pass diffs against
        return _scene->edit();       // stops the render, locks, bumps scene::version()
    }

private:
    scene *_scene;
    HdRenderThread *_renderThread;
    std::atomic<int> *_sceneVersion;
};
```

**Wire the stop callback once, in the delegate's `_Initialize()`:**

```cpp
// hydra/renderDelegate.cpp — in HdWeekendRenderDelegate::_Initialize()
_renderer.Scene().set_stop_render([this]() { _renderThread.StopRender(); });
```

That single line is what turns `scene::edit()` into hdEmbree's `AcquireSceneForEdit()`.
`HdRenderThread::StopRender()` is documented as threadsafe and callable from many Hydra threads
(§10.1), which is exactly what a parallel `SyncAll()` needs. Until stage C the callback can be a
no-op, but write it now and leave a `TODO`, because forgetting it later produces a data race that
reproduces once an hour.

**Why two version counters.** `scene::version()` is a `uint64_t` the tracer bumps for its own
consumers (the SDL viewer polls it to restart). The render pass wants the §6 `std::atomic<int>`
owned by the delegate. Keeping both is a few bytes and avoids widening the pass's comparison or
teaching `tracer/` about Hydra. If you prefer one, diff `scene::version()` in the pass and delete
`_sceneVersion` — just don't half-do it, or a mutation will fail to restart the render and the
image will silently be stale.

Return it from the delegate:

```cpp
// hydra/renderDelegate.cpp
HdRenderParam *HdWeekendRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}
```

## Step B2 — `HdWeekendMesh::Sync`

Replace the stub's `std::cout` with a real sync. The rules from §7.2, none of them optional:

- **Only pull data whose dirty bit is set.** Pulling clean data is "at best incorrect, and at worst
  a crash" — scene delegates implement just-in-time schemes.
- **Clear the bits you consumed**, leave the rest.
- **It runs on worker threads.** Calls into `HdSceneDelegate` are safe; calls into the scene are
  not — hence `AcquireSceneForEdit()`.

Initial dirty bits — take hdEmbree's set (§7.3), minus what we don't consume yet:

```cpp
// hydra/mesh.cpp
HdDirtyBits HdWeekendMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::Clean
         | HdChangeTracker::InitRepr
         | HdChangeTracker::DirtyPoints
         | HdChangeTracker::DirtyTopology
         | HdChangeTracker::DirtyTransform
         | HdChangeTracker::DirtyVisibility
         | HdChangeTracker::DirtyDisplayStyle
         | HdChangeTracker::DirtyPrimvar
         | HdChangeTracker::DirtyNormals
         | HdChangeTracker::DirtyInstancer;
}
```

The body, following `hdEmbree/mesh.cpp` `_PopulateRtMesh`'s "1. pull scene data" block:

```cpp
// hydra/mesh.cpp — in HdWeekendMesh::Sync()
SdfPath const& id = GetId();

if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
    _points = sceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
}
if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
    _topology = GetMeshTopology(sceneDelegate);
}
if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
    /* authored normals, if any -> _normals */
}
if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
    _transform = sceneDelegate->GetTransform(id);      // GfMatrix4d
}
if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
    _UpdateVisibility(sceneDelegate, dirtyBits);       // sets _sharedData.visible
}
```

Note that **points arrive as a primvar**, gated on `IsPrimvarDirty(..., HdTokens->points)`, *not*
on a topology bit. That trips everyone once.

### B2.1 — triangulation

Mandatory for a triangle-only renderer (§7.4):

```cpp
// hydra/mesh.cpp — in HdWeekendMesh::Sync()
HdMeshUtil meshUtil(&_topology, id);
VtVec3iArray triIndices;
VtIntArray   primitiveParams;
meshUtil.ComputeTriangleIndices(&triIndices, &primitiveParams);
```

`triIndices` fills `mesh::tris`; `_points` fills `mesh::verts`. `primitiveParams` is the map from
each generated triangle back to its **authored** face, decoded with

```cpp
// hydra/mesh.cpp — in HdWeekendMesh::Sync(), once per generated triangle
m->face.push_back(HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(primitiveParams[i]));
```

which is exactly what `mesh::face` is for — `mesh::hit` sets
`info.element_id = face.empty() ? f : face[f]` (`mesh.h:211`), feeding the `elementId` AOV.
This is the same shape `obj_loader.h:105` already produces, so the two geometry sources agree.

One invariant the BVH made subtler: `mesh::commit()` permutes `geom` into BVH order but leaves
`verts`, `tris`, `normals` and `face` in **authored** order, bridging them with `tri_index`
(`mesh.h:190` — `f = tri_index[slot]`, then `face[f]`). So `face` must be pushed in the same order
`ComputeTriangleIndices` emits triangles, and must never be sorted to match anything. Get it wrong
and `elementId` is quietly permuted; nothing else breaks, which is what makes it hard to spot.

Skip subdivision entirely for now: treat every mesh as its coarse triangulated hull, as hdEmbree
does when `refineLevel == 0`. `_InitRepr` can stay a no-op that accepts any repr token —
`HdxRenderTask` asks for `refined` in practice, and a renderer with a single shading path may treat
all reprs identically (§7.5), but the override must exist.

### B2.2 — the transform, and why `instance` is the right home for it

Do **not** bake `_transform` into the vertices. Hold both the `mesh` and its wrapping `instance`
as members, and construct them once:

```cpp
// hydra/mesh.h — members on HdWeekendMesh
shared_ptr<mesh>     _mesh;
shared_ptr<instance> _instance;

// hydra/mesh.cpp — in HdWeekendMesh::Sync(), first Sync only:
_mesh = make_shared<mesh>();
/* … fill verts / tris / normals / face / mat … */
_instance = make_shared<instance>(_mesh, ToMat4(_transform));
```

`instance::hit` transforms the *ray* into object space, transforms the hit normal back by the
inverse transpose, and stamps `info.instance_id` (`instance.h:34-48`). Three consequences:

1. A transform change is one call, `_instance->set_transform(...)`: no re-triangulation, and no
   BLAS rebuild. Only `instance::bounds()` moves, so the next `commit()` is a TLAS refit over the
   prim count — 0.030 ms at 400 prims, against 0.297 ms for a rebuild.
2. It is what makes §14 instancing nearly free in stage D: one prototype `mesh`, N `instance`s.
3. The mesh's BLAS is built once in object space and shared by every instance. Baking transforms
   into vertices would force a rebuild per instance.

`instance::set_transform` already guards a singular matrix (`valid = is_finite(inv)`), so a
degenerate scale makes the prim invisible rather than producing NaNs.

### B2.3 — insert, update, remove — and why in-place mutation matters

Hold a `prim_handle` member on the mesh, alongside `_mesh` and `_instance`.

Before writing the update path, know how `scene::commit()` decides between a TLAS **refit**
(0.030 ms at 400 prims) and a full **rebuild** (0.297 ms). It compares the *raw pointers* of the
visible prims against the previous commit (`scene.h:60-66`):

```cpp
// tracer/scene.h:60-66 — existing tracer code, quoted for reference (nothing to write)
const bool same_set = !_tlas.empty()
  && visible.size() == _visible.size()
  && std::equal(visible.begin(), visible.end(), _visible.begin(),
       [](const entry &a, const entry &b) {
         return a.prim == b.prim && a.prim_id == b.prim_id; });
```

So handing `set_prim` a freshly constructed `instance` on every `Sync` **forces a full rebuild on
every frame you drag a gizmo** — exactly the cost [[bvh]] exists to remove. Mutate in place
instead, and reserve `set_prim` for the one edit that genuinely replaces the prim:

```cpp
// hydra/mesh.cpp — in HdWeekendMesh::Sync()
{
    scene_edit edit = renderParam->AcquireSceneForEdit();

    if (_handle == null_prim) {                    // first Sync: insert
        _handle = edit.insert(_instance);
        edit.set_prim_id(_handle, /* see below */);
    }

    // Points moved: assign in place, so mesh::commit() takes its refit branch
    // (mesh.h:60 — `!accel.empty() && tri_index.size() == count`) instead of
    // rebuilding the BLAS. The index buffer and tree topology are untouched.
    if (pointsDirty)    _mesh->verts.assign(_points.begin(), _points.end());

    // Transform moved: one matrix. BLAS untouched; the TLAS refits.
    if (transformDirty) _instance->set_transform(ToMat4(_transform));

    // Topology changed: the triangle count moves and the BLAS must be rebuilt
    // anyway, so a new prim pointer costs nothing extra.
    if (topologyDirty)  TF_VERIFY(edit.set_prim(_handle, _instance));

    edit.set_visible(_handle, _sharedData.visible);
}   // lock released here — keep this scope tight
*dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
```

**Why reaching past `scene_edit` into `_mesh` and `_instance` is safe here:** because
`AcquireSceneForEdit()` has already returned. `set_stop_render()` is wired to
`HdRenderThread::StopRender()`, which blocks until the render callback returns, so no thread is
inside `scene::hit`. The `_edit_lock` is doing a different job — it serializes concurrent `Sync()`
calls against *each other* — which is the reason to keep the scope tight, not a reason to think
the render thread is still running.

`Finalize(HdRenderParam*)` must release the handle — this is the override the stub does not have
and the one whose absence leaks geometry when a prim is deleted:

```cpp
// hydra/mesh.cpp
void HdWeekendMesh::Finalize(HdRenderParam *renderParam)
{
    if (_handle == null_prim) return;
    auto *param = static_cast<HdWeekendRenderParam*>(renderParam);
    scene_edit edit = param->AcquireSceneForEdit();
    edit.remove(_handle);
    _handle = null_prim;
}
```

`scene_edit::remove` pushes the slot onto the free list and `commit()` rebuilds `_draw`
(`scene.h:37-105`), so a removed prim stops being hit on the next render — no dangling pointer, no
compaction pass.

**`primId`:** the `primId` AOV wants the value Hydra uses for picking, which is
`sceneDelegate->GetPrimId(id)` — *not* the tracer's slot handle. Pass that through
`edit.set_prim_id()` and the `primId` AOV lines up with what `usdview` expects when you click a
pixel. Getting this wrong makes picking select the wrong object, which looks like a `usdview` bug.

### B2.4 — material, for now

Until stage D, give every mesh `make_shared<lambert>(color(0.8, 0.8, 0.8))`. hdEmbree ships **no**
material support at all and shades everything as 100% diffuse (§13), so this is not a shortcut, it
is parity with the reference implementation. Never leave `mesh::mat` null — `mesh::hit` writes
`info.mat = mat.get()` and `renderer::raycast` dereferences it unconditionally
(`renderer.h:343`), so a null material is a segfault on the first hit, on a worker thread.

## GATE B — a cube

Grab any cube `.usda` (or write one with a `UsdGeomCube`, or use `$USD_SRC/extras/usd/examples/`):

```
$USD_PY $USD_ROOT/bin/usdrecord --renderer Weekend --disableGpu --imageWidth 400 cube.usda /tmp/cube.png
```

**Expected:** a grey cube against the sky gradient.

Note the trap: a `UsdGeomCube` is an **implicit surface**, and Hydra hands it to you as a native
`cube` prim type unless a scene index converts it (§16). Until step D1 you have two options — use a
`UsdGeomMesh` for this gate, or accept the `TF_CODING_ERROR` from `CreateRprim` as proof you have
correctly identified why step D1 exists. Use a mesh here; do implicit surfaces in stage D, where
they belong.

Also re-run `./build-hydra/testHdWeekend` — `HdUnitTestDelegate::AddCube` adds a **mesh**, so that
test now renders real geometry and is the cheapest regression you have.

Do not read this gate as evidence *anything stateful* works either. Gate B is one `usdrecord`
frame, so it cannot see the step-A4 accumulator bug — that one only appears on the second
`_Execute`, and the way it was actually caught was tumbling in `usdview` and noticing ghosts. If
you want it caught automatically, build the multi-frame test recipe in "What a single-frame gate
cannot see" above.

Do not read this gate as evidence the BVH works. `scene::linear_threshold = 8` (`scene.h:138`) and
`mesh::linear_threshold = 4` (`mesh.h:15`) bypass the tree below those counts, and one cube is one
prim of twelve triangles — so gate B traverses a BLAS and no TLAS at all. The first gate that
exercises both is gate D.

If the cube is black: check `mesh::mat` is non-null, check `scene::commit()` is being reached
(`renderer::render` calls `world.commit()` at the top, and `commit()` early-returns unless `_dirty`
was set — which `scene_edit`'s constructor does), and check `edit.set_visible` isn't being handed a
stale `false`.

If the cube is in the wrong place, the transform is transposed — see the warning in A3.1, and note
that `sceneDelegate->GetTransform` returns a `GfMatrix4d` in the *same* convention as `mat4`.

---

# Stage C — T3, progressive and threaded

**Goal:** `usdview --renderer Weekend` is pleasant — the image refines while you tumble, and the
camera responds immediately.

This is the tier where the delegate becomes usable rather than merely correct. §17.7 is blunt about
it: a renderer whose `Render()` is an uninterruptible blocking call makes the viewport unusable.

## Step C1 — `HdRenderThread` and the control adapter

Wire the thread in the delegate's `_Initialize()` (§10.1):

```cpp
// hydra/renderDelegate.cpp — in HdWeekendRenderDelegate::_Initialize()
_renderThread.SetRenderCallback([this]() {
    _renderer.Clear();               // the pair, always — hdEmbree/renderDelegate.cpp:85
    _renderer.Render(&_renderThread);
});
_renderThread.StartThread();
// destructor: _renderThread.StopThread();
```

**The callback is `Clear(); Render();`, never `Render()` alone.** `HdRenderThread` has no clear
callback of its own (checked against `hd/renderThread.h` in 26.05 — hdEmbree does the pair by hand
in its own callback), so this lambda is the only thing standing between you and the accumulator
bug from step A4. When you move the clear here, delete the one in `_Execute` — leaving both is
harmless but means the buffer is cleared twice per restart, once from each thread.

The adapter from `HdRenderThread` to tracer's `render_control` is the whole of the integration:

```cpp
// hydra/renderer.h
struct hd_render_control final : render_control
{
    HdRenderThread *thread = nullptr;
    bool is_stop_requested()  const override { return thread && thread->IsStopRequested(); }
    bool is_pause_requested() const override { return thread && thread->IsPauseRequested(); }
};
```

`HdRenderThread::IsStopRequested()` / `IsPauseRequested()` are **non-const** member functions while
`render_control`'s are const — that is fine, the pointer is what's const in a const method, not the
pointee. Don't be tempted to `const_cast` anything or to make `render_control`'s methods non-const;
`tracer/` must not change.

`HdWeekendRenderer::Render(HdRenderThread *thread)` then becomes:

```cpp
// hydra/renderer.cpp — in HdWeekendRenderer::Render()
hd_render_control control;
control.thread = thread;
render_stats stats = _renderer.render(_cam, _scene, _aovs, thread ? &control : nullptr);
```

`renderer::render()` already implements every behaviour §10.1 asks of the render callback:

- the pause loop with a 10 ms sleep and a stop check inside it (`renderer.h:65-72`) — the same 10 ms
  hdEmbree uses;
- a stop check between passes (`renderer.h:75-79`, `renderer.h:99-103`);
- a stop check *inside* the tile loop (`renderer.h:142-145`), so the first pass is interruptible
  too, which is the part that matters for tumbling;
- mark non-multisampled AOVs converged after pass 0 (`renderer.h:86-96`);
- unmap every buffer and `set_converged(true)` at the end (`renderer.h:108-112`).

So there is nothing to write here beyond the adapter. Do **not** add `LockFramebuffer()` — hdEmbree
doesn't use it either, relying on multisample `Resolve()` to keep a partially-rendered buffer
legible (§10.1), which is precisely what `render_buffer::resolve()` does.

## Step C2 — `WorkParallelForN` as the tile scheduler

`renderer::schedule` is a `std::function<void(size_t n, std::function<void(size_t,size_t)>)>`, and
`WorkParallelForN(size_t n, Fn&& callback)` calls its callback with `(begin, end)`. The signatures
line up exactly:

```cpp
// hydra/renderer.cpp — in the HdWeekendRenderer constructor
_renderer.schedule = [](size_t n, const std::function<void(size_t, size_t)> &work) {
    WorkParallelForN(n, [&work](size_t b, size_t e) { work(b, e); });
};
_renderer.tile_size = 8;   // hdEmbree's default is also a small square tile
```

`#include "pxr/base/work/loops.h"`. This is §10.2's requirement and the reason the SDL viewer keeps
`tbb_schedule` while the delegate never touches it: **one thread pool per process, USD's**.

`WorkGetConcurrencyLimitSetting()` / `PXR_WORK_THREAD_LIMIT` are honored automatically by going
through `work` — that is most of what "a renderer that saturates all cores inside a DCC is a bug"
asks for. Honor the `threadLimit` render setting too by calling
`WorkSetConcurrencyLimit(n)` when it changes, and note it is process-wide, so restore it rather
than stacking changes.

## Step C3 — start/stop from the render pass

Replace stage A's synchronous call:

```cpp
// hydra/renderPass.cpp — in HdWeekendRenderPass::_Execute()
if (needStartRender) {
    _converged = false;
    _renderer->MarkAovBuffersUnconverged();
    _renderThread->StartRender();    // stage A/B's Clear() has moved into the callback (C1)
}
```

Semantics that matter (§10.1): `StopRender()` is threadsafe and callable from anywhere;
**`StartRender()` is not — call it only from the render pass.** The design intent is that a static
scene is never interrupted: you stop the render only when a prim is about to be edited.

**`StopRender()` also blocks until the in-flight callback returns**, which the name does not
suggest and §10.1 does not spell out. `_RenderLoop` holds `_requestedStateMutex` across the whole
`_renderCallback()`, and `StopRender()` has to take that same mutex — so it first clears
`_enableRender` (making `IsStopRequested()` true, so the tracer bails at its next tile boundary),
then waits. On return the render thread is *provably* not writing.

That property is the entire basis for the "stop, then mutate" pattern used by `scene_edit`, by the
pass destructor, and by the render buffer's `Sync`/`Finalize` (step A1). It is also why a coarse
`tile_size` shows up as a laggy tumble rather than a torn image: correctness never depended on the
cancellation check being prompt, only responsiveness did.

`IsConverged()`:

```cpp
// hydra/renderPass.cpp
bool HdWeekendRenderPass::IsConverged() const
{
    if (_aovBindings.empty()) return _converged;
    for (auto const& b : _aovBindings) {
        if (b.renderBuffer && !b.renderBuffer->IsConverged()) return false;
    }
    return true;
}
```

Hosts poll this to decide whether to keep re-executing the task; returning `true` too early freezes
the image at one sample, returning `false` forever spins the host.

And the destructor:

```cpp
// hydra/renderPass.cpp
HdWeekendRenderPass::~HdWeekendRenderPass() { _renderThread->StopRender(); }
```

## Step C4 — render settings (§15 mechanism 2)

Build an `HdRenderSettingDescriptorList` in `_Initialize()` and pass it to
`_PopulateDefaultSettings()`; return it from `GetRenderSettingDescriptors()`. The descriptor's
`VtValue` type picks the widget in `usdview`'s settings panel.

Two standard tokens a CPU path tracer **must not ignore** (§15):

| Token | Maps to |
|---|---|
| `HdRenderSettingsTokens->convergedSamplesPerPixel` | `renderer::samples_to_converge` |
| `HdRenderSettingsTokens->threadLimit` | `WorkSetConcurrencyLimit` |

Plus our own, in a `hdWeekend:` namespace:

| Setting | Maps to | Note |
|---|---|---|
| `maxBounces` | `renderer::max_bounces` | default 20 |
| `randomNumberSeed` | `renderer::frame_seed` (`uint64_t`, `renderer.h:31`) | `-1` = nondeterministic. This is the slot [[roadmap-discussion-8-26]] §3 identified as already existing: `sample_seed(pixel, sample, frame_seed)` |
| `tileSize` | `renderer::tile_size` | default 8 |
| `jitterCamera` | `camera::jitter` | off makes A/B image diffs exact |

Mirror each as a `TfGetEnvSetting` env var (`HDWEEKEND_SAMPLES_TO_CONVERGENCE`, …) the way
`hdEmbree/config.cpp` does — that is how you change behaviour in a headless run without editing a
`.usda`. This is what `hydra/config.{h,cpp}` is for.

**Settings are polled, not pushed** (§9): the render pass compares
`renderDelegate->GetRenderSettingsVersion()` against a cached value and re-reads *every* setting
with `GetRenderSetting<T>(token, fallback)` when it changes. That is branch 2 of `_Execute`, which
you already stubbed in step A4 — **the stubbed `SetSamplesToConvergence(1)` goes away here.** The
version is one counter for the whole map, so there is no way to tell which setting moved; re-read
all of them.

Two details worth getting right the first time:

- `threadLimit` must go through **`WorkSetConcurrencyLimitArgument(int)`**, not
  `WorkSetConcurrencyLimit(unsigned)`. The token's convention is 0 = all cores and negative =
  all-but-*n*, and the unsigned overload reads both as enormous thread counts.
- The delegate's settings version starts at **1** (`hd/renderDelegate.cpp:39`) and the pass caches
  **0**, so branch 2 fires on the very first `_Execute` — the same trick as `_sceneVersion`. The
  renderer's constructor still wants seeding from `HdWeekendConfig`, though, so the pre-poll state
  is never a different number from the one the settings panel opens on.

## Step C5 — `GetRenderStats` and pause/stop

```cpp
// hydra/renderDelegate.cpp
VtDictionary HdWeekendRenderDelegate::GetRenderStats() const
{
    VtDictionary stats;
    stats[HdPerfTokens->numCompletedSamples.GetString()] = _renderer.CompletedSamples();
    return stats;
}
```

`renderer::completed_samples()` is already an atomic published once per pass (`renderer.h:105`), so
this is a read. `usdview` shows it in the HUD, and it is the fastest way to tell "converging
slowly" from "not rendering".

Then forward the four thread controls (§5.2):

```cpp
// hydra/renderDelegate.h — HdWeekendRenderDelegate overrides
bool IsPauseSupported() const override { return true; }
bool Pause()  override { _renderThread.PauseRender();  return true; }
bool Resume() override { _renderThread.ResumeRender(); return true; }
bool IsStopSupported() const override { return true; }
bool Stop(bool blocking) override { _renderThread.StopRender(); return true; }
```

These are what make batch and interactive hosts well-behaved on exit; without `Stop`, closing
`usdview` mid-render can hang.

Also set `GetMaterialBindingPurpose()` to `HdTokens->full` now — ray tracers return `full`, Storm
returns `preview` (§5.2). It costs one line and changes which material bindings you'll be handed in
stage D.

## GATE C — usdview, interactively

```
source env.sh
$USD_PY $USD_ROOT/bin/usdview --renderer Weekend scene.usda
```

**Expected**, and check all five:

1. The image appears noisy and **visibly refines** over a few seconds.
2. Tumbling the camera restarts the render **immediately** — no multi-second freeze. This is the
   in-tile cancellation point doing its job. Watch specifically for **ghosts of the object's
   previous position** blended into the new frame: that is the step-A4 accumulator bug, and it
   means the render callback is `Render()` alone instead of `Clear(); Render();`.
3. The HUD sample counter climbs and then stops at `convergedSamplesPerPixel`.
4. Changing `convergedSamplesPerPixel` in the settings panel restarts the render.
5. Closing the window exits cleanly, with no hang and no crash on shutdown.

Then the headless regression, which is the one you can automate:

```
$USD_PY $USD_ROOT/bin/usdview --renderer Weekend --quitAfterStartup scene.usda
```

If tumbling is sluggish, the cancellation point is not being reached: confirm the `hd_render_control`
is actually being passed (a `nullptr` control disables every check in `renderer::render`), and that
`tile_size` is small enough that a single tile is not seconds of work.

If the image never converges, `set_converged` is being reset every `_Execute` — you are hitting a
spurious `needStartRender`, most likely because the AOV binding comparison sees a difference every
frame. Log which of the five branches fired.
---

# Stage D — T4, scope narrowing and fidelity

**Goal:** real USD assets render, and the AOVs `usdview` needs actually work.

Stages A–C give a renderer that handles hand-written meshes. Stage D is what makes it survive
contact with content you didn't author.

## Step D1 — the implicit-surface scene index plugin

**Why this is the highest-value item in the stage:** Hydra will hand you `sphere`, `cube`, `cone`,
`cylinder`, `capsule`, and `plane` as *native prim types* unless you ask for them to be converted
(§16). Every one of them currently hits `CreateRprim`'s `TF_CODING_ERROR`. Registering
`HdsiImplicitSurfaceSceneIndex` narrows the input language to what the renderer supports, and
Pixar wrote the conversion.

The pattern is ~70 lines, ported from
`pxr/imaging/plugin/hdEmbree/implicitSurfaceSceneIndexPlugin.cpp`:

```cpp
// hydra/implicitSurfaceSceneIndexPlugin.cpp
TF_REGISTRY_FUNCTION(TfType) {
    HdSceneIndexPluginRegistry::Define<HdWeekend_ImplicitSurfaceSceneIndexPlugin>();
}
TF_REGISTRY_FUNCTION(HdSceneIndexPlugin) {
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Weekend",                        // MUST equal plugInfo displayName
        _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr,
        /* insertionPhase = */ 0,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}
// _AppendSceneIndex() returns HdsiImplicitSurfaceSceneIndex::New(inputScene, args)
// with sphere/cube/cone/cylinder/capsule/plane each mapped to "toMesh".
```

plus a matching `HdSceneIndexPlugin`-typed entry in `plugInfo.json` carrying
`"loadWithRenderer": "Weekend"`.

**The failure mode to watch for**, flagged in [[hdtiny-stub-delegate]]'s design notes: if
`_pluginDisplayName` does not match `displayName` **exactly**, geometry silently vanishes — no
error, no warning, just nothing. That is why this was deliberately kept out of the first-load task.
Change one string at a time and re-run gate B between changes.

### The analytic-sphere decision, now due

`tracer/sphere.h` has a real analytic `sphere::hit`, so there is a genuine temptation to keep
`sphere` native and convert only the rest. `HdsiImplicitSurfaceSceneIndex` stores one mode token
per type, so omitting `sphere` from `inputArgs` passes it through untouched and `CreateRprim` would
need an `HdWeekendSphere` Rprim reading `UsdGeomSphere`'s radius.

**Recommendation: convert everything, including spheres, and revisit only if profiling asks.**
Reasons: hdEmbree converts everything; one Rprim type is the documented minimum (§5.1) and the
smallest surface to keep correct; a native sphere needs its own `Sync`, its own dirty bits, and its
own transform handling; and a transformed `UsdGeomSphere` is an ellipsoid, which `sphere::hit`
cannot represent — you'd need the `instance` wrapper anyway, at which point the win is a
ray-triangle test against a ray-quadric test for a shape that is rare in real assets. Record the
decision either way so it isn't re-litigated.

Also register `HdsiExtComputationPrimvarPruningSceneIndex` while you are here — it resolves
ext-computation-driven primvars (skinned meshes, most notably) into plain arrays, which is the
difference between "animated characters render" and "animated characters render in their bind
pose".

## Step D2 — the remaining AOVs

`GetDefaultAovDescriptor(name)` declares format, multisampling, and clear value per AOV (§8.3).
It is a translation of `default_aov_descriptor()`, which is already hdEmbree's table:

```cpp
// hydra/renderDelegate.cpp
HdAovDescriptor
HdWeekendRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    aov which;
    if (!ToAov(name, &which)) {          // convert.h, shared with A3.3
        return HdAovDescriptor();          // unsupported: default-constructed
    }
    const aov_descriptor d = default_aov_descriptor(which);
    return HdAovDescriptor(static_cast<HdFormat>(int(d.format)),
                           d.multisampled,
                           /* clearValue = */ _ToVtValue(d));
}
```

The clear value's *type* must match the format — `GfVec4f` for `color`, `float` for `depth`,
`int` for the id AOVs — or the host's clear does nothing useful.

`renderer::render_tiles` already fills all eight (`renderer.h:181-267`), including the two that
need the camera: `depth` is clip-space (`proj·view·hit.p`, remapped to `[0,1]`) and `cameraDepth`
is the raw ray `t`. So this step is genuinely just the descriptor table.

**`color` must be premultiplied alpha** for correct compositing (`hd/tokens.h:365`). The tracer
writes `alpha = 1` on every hit and on every miss (`renderer.h:185-190`), so it is trivially
premultiplied today. When true transparency arrives, this becomes a real constraint.

Gate: `usdview`'s AOV dropdown shows the list, and selecting `normal` or `primId` gives a sensible
picture. `primId` working is also what makes **clicking to select a prim** work.

## Step D3 — `displayColor`

One primvar, and it is the difference between a grey scene and a scene:

```cpp
// hydra/mesh.cpp — in HdWeekendMesh::Sync()
// constant or vertex-interpolated displayColor -> the mesh's lambert albedo
VtValue v = sceneDelegate->Get(id, HdTokens->displayColor);
```

Take the constant case, and take element 0 for the interpolated case. Do not build the general
primvar-interpolation machinery — that is §7.4 and 0.4.0, and it needs
`hdEmbree/sampler.h` + `meshSamplers.h` ported wholesale.

hdEmbree gates this behind an `enableSceneColors` render setting; add it, defaulting on.

## Step D4 — instancing

Subclass `HdInstancer` and implement `Sync()` (§14):

- Cache instancer primvars as `HdVtBufferSource*` keyed by token.
- Implement `ComputeInstanceTransforms(prototypeId)` returning a `VtMatrix4dArray`, composed from
  the scene delegate's `instancerTransform` and the primvars `hydra:instanceTransforms`,
  `hydra:instanceTranslations`, `hydra:instanceRotations`, `hydra:instanceScales`.
- **Nested instancing must be flattened** by recursing to parent instancers and taking the
  cartesian product of the transform arrays at each level.
- In `HdWeekendMesh::Sync`, when `DirtyInstancer` or `DirtyTransform` is set, call
  `_UpdateInstancer(sceneDelegate, dirtyBits)`, sync the parent instancer **first**, then insert
  one `instance` per returned matrix, all sharing the same prototype `mesh`.

This is where step B2.2's decision pays off: the loop body is
`edit.insert(make_shared<instance>(proto, ToMat4(xf)))`, and every instance shares one
prototype `mesh` — and therefore one prototype BLAS.

Scope it as hdEmbree does: **transform-only** as the instance-varying attribute. Per-instance
colors and primvars are 0.4.0.

Track the handles per prim so `Finalize` removes all of them; an instanced prim owns N slots, not
one. On a transform-only update, call `set_transform` on the existing `instance`s rather than
re-inserting — the same reasoning as B2.3, except that here it is N prim pointers that would
change instead of one, so the TLAS rebuild is guaranteed rather than merely likely.

## Step D4a — the shared-prototype commit epoch

**The one edit to `tracer/` this plan sanctions**, and [[bvh]] deferred it here on purpose: "do it
*with* instancing, where the N gets large enough to matter, not before." That is now.

`scene::commit()` calls `prim->commit()` on every record (`scene.h:52`), `instance::commit()`
forwards to `proto->commit()` (`instance.h:29-32`), and nothing remembers that the prototype
already ran this commit. **Measured in [[bvh]]: 20 instances of one 20 480-triangle mesh cost
9.7 ms per commit where one would do** — 20× the necessary work, and it scales with the instance
count that step D4 exists to make large.

The fix is a `uint64_t` commit epoch on `hittable`, bumped by `scene::commit()` and
checked-and-stamped by `mesh::commit()`, so a prototype reached through N instances commits once.
It touches no `hit()` path, so [[bvh]]'s correctness gate — the tree agrees with a linear scan ray
for ray, across 7 scenes × 200 000 rays — is the regression test, and `tracer_cli` on the goldens
is the second one.

What the epoch does **not** fix, and which is worth knowing before gate D: `mesh::commit()` has no
dirty flag of its own, so *every* mesh does a full refit pass over its triangles on *every* scene
edit, changed or not. The epoch removes the N× on a shared prototype; it does not remove the
once-per-mesh floor. If gate D is sluggish in response to edits rather than in raw render time,
measure that first — the fix is a dirty bit set through `scene_edit`, which is a second `tracer/`
change to scope deliberately rather than slip in here.

## Step D5 — diagnostics

Register `TF_DEBUG_CODES` (`hydra/debugCodes.{h,cpp}`, modeled on `hdEmbree/debugCodes.h`) —
§19 lists it as the diagnostics conformance level, and by this point you have enough moving parts
that `std::cout` is no longer the right tool. Suggested codes: `HDWEEKEND_MESH_SYNC`,
`HDWEEKEND_RENDER_PASS`, `HDWEEKEND_SETTINGS`.

Then **delete the stub's `std::cout` lines.** They were the deliverable of T0; in a per-frame
render loop they are a performance bug and they make `usdview` unusable in a terminal.

## GATE D — a real asset

```
$USD_PY $USD_ROOT/bin/usdview --renderer Weekend <a-real-usd-asset>.usd
```

Use something with implicit surfaces, instancing, and authored `displayColor` — Kitchen_set or
similar from the USD sample assets is the standard choice.

**Expected:** it renders, in color, with no `TF_CODING_ERROR` in the terminal, and clicking a
surface selects the right prim.

**Verified 2026-09-03**, at the end of D4, with `usdrecord --renderer Weekend --disableGpu`:

- `assets/OpenChessSet/chess_set.usda` — a `PointInstancer` with `quath` orientations. Eight pawns
  per side, correctly placed, with exactly one black pawn advanced (matching `positions[0]`'s
  distinct z of -0.094 against -0.032 for the rest). A single Rprim,
  `.../Proto/ForInstancer<hash>`, drives all eight. 26 s at 700 px.
- `assets/Kitchen_set/Kitchen_set_instanced.usd` — 425 `instanceable` references, reaching the
  delegate through `UsdImagingInstanceAdapter` as instancers. Renders complete and in color:
  instancing, implicit surfaces and `displayColor` all working together. 2 min 09 s at 600 px.
- Clicking a surface in `usdview` selects the right prim (verified by hand).
- No `TF_CODING_ERROR` in either run.

D4a is **not** done, and neither timing measures it: both are dominated by ray tracing, not by
`scene::commit()`. The shared-prototype commit epoch still needs its own measurement.

---

## Definition of done

- [x] `usdrecord --renderer Weekend --disableGpu` writes a correct PNG (gate A, then again at D)
- [x] `usdview --renderer Weekend` refines progressively and responds to tumbling within a frame
- [ ] `testHdWeekend` renders `AddCube` and exits clean
- [x] `usdview --renderer Weekend --quitAfterStartup` exits 0
- [x] Implicit surfaces (`UsdGeomSphere`, `UsdGeomCube`, …) render
- [x] `normal` / `depth` / `primId` AOVs are selectable in `usdview` and correct
- [x] Clicking a surface in `usdview` selects the right prim
- [x] Deleting a prim in `usdview` removes it from the render
- [x] Dragging a transform in `usdview` takes the TLAS **refit** path, not a rebuild
- [x] A shared prototype is committed **once** per `scene::commit()`, not once per instance
- [x] No `TF_CODING_ERROR` or `TF_WARN` in a normal session
- [x] `tracer_cli` and `viewer` still build **with USD entirely off the path**
- [x] `hydra/` still links only `hd tf` (+ `gf vt work hdsi` as needed) and never the `tracer` target
- [x] `grep -rn schedulers.h hydra/` is empty
- [x] Every tracer include in `hydra/` is spelled `"tracer/…"` — `grep -rn '#include "' hydra/`
      shows no bare tracer header name
- [x] `ToMat4` and `ToAov` are each defined exactly once, in `convert.h`
- [ ] The only `tracer/` change in the diff is D4a's commit epoch, and the [[bvh]] linear-scan
      gate plus `tracer_cli` on the goldens both still pass
- [ ] No `std::cout` left in `hydra/`

---

## Design notes — decisions recorded so they aren't re-litigated

**BVH.** Landed in `baefc6e`, and the structural prediction held: the delegate needs **no code of
its own** for it. It lives under `scene::commit()` (a TLAS over prims) and `mesh::commit()` (a BLAS
per mesh), both of which the delegate calls indirectly — `renderer::render()` calls
`world.commit()` at the top of every render (`renderer.h:50`), and `scene::commit()` calls
`prim->commit()` on each record (`scene.h:52`). The rebuild is therefore sequenced correctly
against Hydra's `Sync` → `CommitResources` → `Execute` phases for free, because it hangs off the
tracer's own commit rather than off Hydra's — and it runs on the render thread *after*
`StopRender()` has returned, so there is no lock, no atomic, and nothing new to reason about.
`HdRenderDelegate::CommitResources(tracker)` remains §2's one serial hook after `SyncAll()` if a
more expensive step is ever wanted there; it is currently an empty stub that prints.

What the BVH *did* change is that `commit()` is no longer close to free, so **how** the delegate
mutates now matters. Both consequences are written into the steps rather than left to be
rediscovered: B2.3 mutates `_mesh` and `_instance` in place so the refit path stays reachable, and
D4a adds the commit epoch that instancing needs.

Two numbers to carry, measured in [[bvh]]: a cold BLAS build is **10.6 ms** at 20 480 triangles
(≈676 ns/prim, so roughly 0.7 s at a million), and it is **not interruptible** — every stop check
in `renderer::render` is *after* `world.commit()`. On gate D's assets that is invisible; on a
million-triangle asset it would surface as a hitch in gate C's tumbling test, and the answer is
[[bvh]] Appendix B item 1 (a parallel build, injected the way `renderer` takes a `tile_scheduler`),
not anything in `hydra/`.

**Includes: the `tracer/` prefix, and why not the obvious thing.** `hydra/` and `tracer/` both
own a `renderer.h` and a `mesh.h`, and a quoted include searches the including file's own directory
first — so the obvious `#include "renderer.h"` inside `hydra/renderer.h` includes *itself*, and
`#pragma once` turns the mistake into a missing type rather than a duplicate one. Step A0.1 settles
it: `-I` the repo root, `#include "tracer/renderer.h"`. Angle brackets and `../tracer/...` both also
compile and are recorded there; the prefix wins because it removes the class of bug rather than two
instances of it, and because it survives stage B, where `hydra/mesh.cpp` needs both `mesh.h`es.
Verified not to disturb tracer's own internal includes, and not to put TBB on the search path.

**Shared helpers live in `convert.h`, not in whichever `.cpp` needed them first.** `ToMat4` is
called from `renderer.cpp`, `mesh.cpp` and `instancer.cpp`; `ToAov` from `renderer.cpp` and
`renderDelegate.cpp`. Both encode a load-bearing coincidence between a tracer type and a USD type,
so three copies of either is three chances for one of them to be "fixed" with a transpose. One
`inline` definition each, in one header. This is also why the names lost their leading underscore —
`_ToMat4` at namespace scope in a header is a reserved identifier.

**One thread pool.** Settled by step A0 + C2: the delegate uses `WorkParallelForN` inside USD's
arena; the SDL viewer keeps `tbb_schedule`; `tracer/` never chooses. `tracer/CMakeLists.txt`'s
comment that TBB "will automatically use hydra/usd's version" describes the *viewer* build, not the
delegate — the delegate does not link `tracer` at all, so the question never arises for it.

**Two scene-version counters.** See B1. `scene::version()` (`uint64_t`, tracer-owned, polled by the
viewer) and the delegate's `std::atomic<int> _sceneVersion` (§6). Keeping both is intentional;
collapsing to one is fine but must be done in one go.

**Clearing the AOVs is the *restart's* job, not the binding change's.** A restart begins a new
accumulation by definition, so `Clear()` is unconditional on `needStartRender` (stage A/B) or the
first line of the render callback (stage C). Reading hdEmbree's `_Execute` alone suggests otherwise
— its only visible `Clear()` is in the AOV-bindings branch — but the real one is in its render
callback, and copying the pass without the callback is how we shipped ghosting through stage B. The
alternative design, clearing inside `HdWeekendRenderer::Render()` itself, was not taken: it would
make a future progressive `Render()` unable to add samples to a buffer it did not just clear.

**Implicit surfaces are all converted to meshes, spheres included.** Decided at step D1 and
settled: `HdWeekend_ImplicitSurfaceSceneIndexPlugin` maps `sphere`, `cube`, `cone`, `cylinder`,
`capsule` and `plane` to `toMesh`, so `mesh` stays the only Rprim type. `tracer/sphere.h`'s
analytic `sphere::hit` is not reachable from the delegate and that is fine — a transformed
`UsdGeomSphere` is an ellipsoid, which `sphere::hit` cannot represent, so a native sphere Rprim
would need the `instance` wrapper anyway and the remaining win is one ray-quadric test against one
ray-triangle test on a shape that is rare in real assets. Revisit only if profiling asks.

**The ext-computation pruning scene index is not optional.** It ships as a second plugin
(`HdWeekend_ExtComputationSceneIndexPlugin`), and without it a skinned mesh does not render in its
bind pose — it **segfaults**. `HdWeekendMesh::Sync` reads `points` with
`sceneDelegate->Get(id, HdTokens->points)`, a computation-backed primvar comes back as an *empty*
`VtValue`, and `VtValue::Get<VtVec3fArray>()` on an empty value is undefined behaviour rather than
an error. `mesh.cpp` now checks `IsHolding<VtVec3fArray>()` and warns instead, and drops `tris`
whenever `verts` is empty, because `mesh::commit` indexes `verts[tris[i]]` unguarded.

**Missing plugInfo entries fail silently, and it looks exactly like a broken renderer.** Verified
at D1 rather than taken on faith: with the `HdSceneIndexPlugin` entry removed from
`plugInfo.json`, a scene of six implicit surfaces renders as an empty sky — no `TF_CODING_ERROR`,
because Hydra filters unsupported prim types out before `CreateRprim` ever sees them. The three
strings that must agree are `HdWeekendRendererPlugin`'s `displayName`, each scene-index plugin's
`loadWithRenderer`, and `_pluginDisplayName` in the `.cpp`. All three are `"Weekend"`.

**Materials stop at `displayColor`.** §13 and [[roadmap-discussion-8-26]] §4: hdEmbree has no
material support at all, and lambert/metal/glass is already ahead of the reference implementation.
`UsdPreviewSurface` translation, MaterialX, and `hdMtlx` are 0.4.0+.

**No lights.** §12 is explicitly tiered and optional. Everything renders against
`renderer::raycast`'s sky gradient, which is the analogue of hdEmbree's ambient default. It is,
as [[roadmap-discussion-8-26]] §5.5 predicts, the first thing anyone will ask for.

**No `SetDrivers` / Hgi.** Unchanged from T0: leave it unimplemented, let `HdxAovInputTask` upload
the CPU buffer (§17.2). The cost is one full-frame CPU→GPU upload per presented frame.

**No `HdRenderSettings` Bprim.** §15 mechanism 3 is the direction Hydra is moving and is required
for a proper batch story with render products, but mechanism 2 (descriptors) covers interactive use
and `usdrecord`. Revisit if batch rendering with multiple render products becomes a goal.

**Multiple concurrent render passes are unsupported.** They would overwrite each other's AOV
bindings. hdEmbree has the same limitation (§9); it is a legitimate first-delegate boundary, but
worth a `TF_WARN` rather than silent corruption if a second pass is created.

**Instance rotations are sampled at every spelling, and hdEmbree's version is a silent data-loss
bug.** `HdEmbreeInstancer::ComputeInstanceTransforms` samples `hydra:instanceRotations` as
`GfVec4f` and nothing else. `UsdGeomPointInstancer` stores `orientations` as `quath` unless the
asset opts into `orientationsf` (`UsdGeomPointInstancer::UsesOrientationsf`), and a `quath`
array's `HdTupleType` is `HdTypeHalfFloatVec4` — which fails hdEmbree's type check, so the
rotations are dropped with no warning and every instance renders axis-aligned.
`assets/OpenChessSet/chess_set.usda` is exactly that asset. `_SampleQuat` in
`hydra/instancer.cpp` therefore accepts `VtQuathArray`, `VtQuatfArray`, `VtQuatdArray`, and the
raw `VtVec4fArray` spelling `hd/instancer.h` documents. Do not "simplify" it back toward the
reference implementation.

**Instancer primvars are cached as `VtValue`, not `HdVtBufferSource*`.** §14 and hdEmbree both say
buffer source; that class exists to describe GPU buffer layout, and the only thing this delegate
ever does with an instance primvar is index the array — so the wrapper buys nothing and costs a
manual `delete` loop in the destructor. One consequence to know before "fixing" it:
`_primvars[token]` on a `TfHashMap` *inserts* an empty `VtValue` when the key is missing, which is
why `ComputeInstanceTransforms` is not `const` and why each `_Sample*` helper must treat an empty
value as "absent" rather than as an error.

**The instance transform composes as `rprimTransform * instanceTransforms * scales * rotations *
translations * instancerTransform`**, in `mat4`'s row-vector convention where the leftmost factor
applies first. That is a transcription of hdEmbree, not a derivation, and flipping any operand pair
yields a picture that still looks plausible. The fixture that discriminates it is three cubes with
asymmetric per-instance scales — `scales = [(1,1,1), (1,2,1), (1,1,2)]` against orientations of
identity, 45° and 90° about Y. Correct output is a plain cube, a doubled-height cube showing a
vertical corner edge, and a cube doubled in *width* and face-on, because the 90° rotation maps the
scaled Z into X. Scale applied after rotation instead leaves the third box doubled in depth, which
is nearly invisible from a front view.

**`build-hydra/testHdWeekend` and `testHdWeekendAccumulator` are orphaned binaries — the standing
verification loop in this document is a trap.** Neither has a source file anywhere in the tree; the
accumulator was built 2026-09-01 and its source deleted, as §"What a single-frame gate cannot see"
already says. `cmake --build build-hydra --target install` does not rebuild them, so they keep
reporting a verdict from whenever they were last compiled. As of D4 the accumulator binary FAILS
its guard assertion — every frame renders 0 samples — and it fails *identically* against a delegate
built from pre-D1 `HEAD`, which is how that was established as staleness rather than a regression.
Rebuild it from the recipe in this document, or delete the binary; do not read it as a gate.

---

## Next up

0.4.0. The two things this plan deliberately left on the floor, in the order people will ask for
them: **UsdLux lights** (§12 — `hdEmbree/light.h` is explicitly written as a reference
implementation of UsdLux support, and its header documents exactly which attributes it does and
does not honor, which is a ready-made scope boundary), and **texture mapping**, which needs the
§7.4 primvar-interpolation machinery — `hdEmbree/sampler.h` and `meshSamplers.h` are worth porting
rather than reinventing, since correct evaluation at a hit point needs the interpolation mode, the
triangulated index buffer, and the primitive-param table together.
