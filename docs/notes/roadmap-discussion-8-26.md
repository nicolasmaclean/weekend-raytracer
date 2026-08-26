# Roadmap review — Hydra delegate sequencing

**Date:** 2026-08-26
**Sources:** `docs/Roadmap.md`, `docs/hydra-spec.md` (derived from OpenUSD `v26.05`, commit `2095fafaf`, `HD_API_VERSION 97`), and the tracer source at that commit.

**Goal that framed the review:** wrap the tracer as a Hydra render delegate. 0.2.0 should contain *only* work that serves that goal; everything else pushes to 0.4.0.

Section references below (§N) point at `docs/hydra-spec.md`.

---

## 1. Release-boundary corrections (adopted)

### `triangle mesh` and `bvh` moved 0.4.0 → 0.2.0

These were the two genuine ordering errors in the original board.

- §5.1: the minimum *useful* delegate is one Rprim plus the `camera` Sprim. hdEmbree's entire Rprim set is `mesh`.
- §16: Hydra hands you `sphere`/`cube`/`cone`/`cylinder`/`capsule`/`plane` as-is **only if you decline to register** `hdsi/implicitSurfaceSceneIndex`. Real USD assets are mesh-based either way. A sphere-only tracer cannot render a USD scene.
- The moment meshes land, `hittable_list::hit`'s linear scan over `std::vector<shared_ptr<hittable>>` with virtual dispatch per triangle is unusable. The BVH ships *with* meshes, not after.
- The BVH needs a **rebuild** path, not build-once: §6 mandates `StopRender()` → mutate → bump `sceneVersion` → restart on every scene edit.

Design consequence worth deciding alongside the scene graph rather than after: per-triangle virtual `hittable` has to become flat indexed triangle storage.

### `hdTiny stub delegate` moved 0.3.0 → head of 0.2.0

Value is entirely in the *timing*. The highest-risk items in the spec are environmental, not algorithmic:

- §18.1 — ABI compatibility is exact: same compiler, stdlib, C++ standard, USD version. `PXR_NAMESPACE` is version-mangled (`pxrInternal_v0_26_5__pxrReserved__`), so mismatches fail at link/`dlopen`.
- §18.3 — out of tree you must reproduce `_plugInfo_subst()` yourself (`PLUG_INFO_LIBRARY_PATH` relative to the resources dir, `PLUG_INFO_RESOURCE_PATH`, `PLUG_INFO_ROOT`).
- §3.3 — `Plug_InitConfig` assembles search paths once at load; first path containing a plugin wins; relative entries anchor to the `libplug` .so, not CWD. Debug with `TF_DEBUG=PLUG_INFO_SEARCH,PLUG_REGISTRATION`.
- §18.4 — `TF_REGISTRY_FUNCTION` relies on static initializers; `--gc-sections`/LTO configs can strip them silently.

None of that depends on the tracer refactor, all of it is days-long when it bites, and doing it first means the refactor targets an API that has actually been compiled against instead of a guess.

---

## 2. Requirements absent from the original board (all adopted)

### Render target refactor

`tracer/framebuffer.h` is `std::vector<color>` (3 doubles) + `std::vector<int> samples` with `get_pixel()` dividing. Conceptually ~70% of the right shape; wrong in type and missing the host-facing contract.

§8.1/§8.2 require, per `HdRenderBuffer`:
- 12 pure virtuals: `Allocate`, `GetWidth/Height/Depth/GetFormat`, `IsMultiSampled`, `Map`/`Unmap`/`IsMapped`, `Resolve`, `IsConverged`, `_Deallocate`. Only `depth == 1` need be supported.
- Three allocations: resolved `_buffer` in the requested `HdFormat`; `_sampleBuffer` in float32/int32 with the **same component count** as the requested format; `_sampleCount` per pixel.
- `Resolve()` divides accumulation by count and converts. This is what makes progressive refinement legible to the host — it reads `_buffer` at any time and gets a correct-if-noisy image.
- `Map`/`Unmap` maintain an **atomic** mapper count.
- Practical additions from hdEmbree: `SetConverged(bool)`, typed `Write(...)` / `Clear(...)`.

§8.3: `color` is `HdFormatFloat32Vec4`, multisampled, cleared to `(0,0,0,0)`, and **premultiplied alpha** (`tokens.h:365`). Cheapest path is to make the accumulation buffer float32Vec4 directly, since format and sample buffer must agree on component count.

### Interruptible rendering

Progressive ≠ cancellable. `camera::render` runs a `tbb::parallel_for` to completion with no escape hatch.

- §10.1: poll `IsStopRequested()` as a cancellation point at least once per sample pass, **and** inside the inner tile loop so pass 0 is also interruptible. Poll `IsPauseRequested()` in a sleep loop (hdEmbree: 10 ms).
- §17.7: a renderer whose `Render()` is an uninterruptible blocking call "will make the viewport unusable."
- `StopRender()` is threadsafe and called from many Hydra threads; `StartRender()` is not — render pass only.
- Design intent: a *static* scene is never interrupted. You stop only when a prim is about to be edited.

Cheap to build in now, invasive to retrofit.

### The tracer must stop owning its parallel loop

§17.6: USD is built against oneTBB; a renderer with its own TBB arena or private pool oversubscribes inside a DCC. Route parallelism through `pxr/base/work` and honor `WorkGetConcurrencyLimitSetting()` / `PXR_WORK_THREAD_LIMIT` / the `threadLimit` render setting.

The OMP → oneTBB migration was the right call (matches USD, no arena conflict), but the *library* must expose a tile entry point — `render_tile(tile, sample)` — and let the caller drive the loop. hdEmbree does exactly this: `WorkParallelForN(numTilesX*numTilesY, bind(&Renderer::_RenderTiles, ...))`. Then the SDL viewer keeps `tbb::parallel_for` and the delegate uses `WorkParallelForN`.

This is a constraint on the camera/renderer refactor, not an independent task.

---

## 3. What the camera refactor actually entails

Larger than the name suggests. §11: the render pass gets **only** `GetWorldToViewMatrix()` and `GetProjectionMatrix()`.

- `v_fov`, `lookfrom`, `lookat`, `vup`, `aspect_ratio` all become *derived*, not inputs.
- `camera::init()` computing `height_px` from `aspect_ratio` is the ownership inversion that has to go — framing belongs to the host.
- Ray generation from an arbitrary inverse-projection matrix, with ortho and perspective on one code path via `round(proj[3][3]) == 1.0`:

```cpp
GfVec3f ndc(2*((x + jx - minX)/w) - 1, 2*((y + jy - minY)/h) - 1, -1);
GfVec3f nearPlaneTrace = inverseProj.Transform(ndc);
if (isOrthographic) { origin = nearPlaneTrace; dir = {0,0,-1}; }
else                { origin = {0,0,0};        dir = nearPlaneTrace; }
origin = inverseView.Transform(origin);
dir    = inverseView.TransformDir(dir).GetNormalized();
```

- **Data window** (§9): render into a sub-rect of a possibly larger buffer. Prefer `renderPassState->GetFraming().dataWindow` (`CameraUtilFraming`), fall back to `GetViewport()` for older hosts. Data window coordinates are **y-down** while image line order is bottom-to-top — the flip is explicit in hdEmbree's `_RenderTiles`. `render_region(x0,x1,y0,y1)` has the right signature but no data-window offset distinct from buffer size.
- Camera currently owns the render loop, the framebuffer write, and `ray_color`. hdEmbree keeps `renderer.{h,cpp}` separate from the camera; the split falls out of the interruptible-tile-loop item.

**Survives intact:** `defocus_angle` / `focus_dist`. §11 is explicit that DoF and motion blur must come from `HdCamera` attributes (focus distance, f-stop, shutter) precisely *because* the projection matrix carries neither.

**Already conformant:** `tracer/rng.h`. Per-pixel-per-sample seeded splitmix64, no shared state, no locks. §10.2 asks for per-work-item seeding and a `randomNumberSeed` render setting (`-1` = nondeterministic) — `sample_seed(pixel_index, sample_index, frame_seed)`'s unused `frame_seed` slot *is* that setting. Zero work.

---

## 4. Deliberately out of 0.2.0

- **Materials stay as-is.** §13: hdEmbree ships **no** material support at all — every surface is 100% diffuse, optionally tinted by the `displayColor` primvar (`enableSceneColors`). The existing lambert/metal/glass system is already ahead of the reference implementation. The delegate only needs "read `displayColor`, make a lambertian." Material *networks* are 0.4.0+.
- **No lights.** §12 is a tiered, optional feature.
- **No texture mapping.** Needs the §7.4 primvar-interpolation machinery (interpolation mode + triangulated index buffer + primitive-param table); that is spec tier T4. `hdEmbree/sampler.h` and `meshSamplers.h` are worth porting rather than reinventing when the time comes.
- **No float-precision migration.** Not required — `GfMatrix4d` is double anyway. Pure churn.

---

## 5. Open items, not adopted into the board

Recorded here so they are not lost.

1. **0.3.0 is one checkbox over the largest chunk of work in the project.** "hydra wrapper" covers §5's 16 pure virtuals, §6's `HdRenderParam` + scene-version gateway, §7's `HdMesh` subclass, §8's `HdRenderBuffer` Bprim, §9's `HdRenderPass` with 5-way change detection, §10's `HdRenderThread` wiring, §16's scene index plugins, and §18.3's out-of-tree CMake work. §20 already supplies a decomposition explicitly "ordered so that each tier is independently verifiable": T0 loads → T1 first pixels → T2 geometry → T3 progressive+threaded → T4 scope narrowing.

2. **The data window is no longer represented on the board.** The `camera api refactor` sub-items are now `matrix-driven rays` and `ortho/perspective`; an earlier `data-driven camera` line was dropped. The §9 requirement stands regardless — including the y-down/bottom-up flip, which is the kind of detail that is easy to lose six weeks out.

3. **`hit_info` needs prim id and element (face) index.** §8.3's `primId` / `instanceId` / `elementId` AOVs read them (`HdFormatInt32`, cleared to `-1`). Nearly free while already editing `hit_info` for triangles; annoying to add later. Belongs under `scene graph with mutation`.

4. **`usdrecord --renderer <name> --disable-gpu` deserves its own verification checkbox.** §19 lists it as the batch conformance level, and §17.3 notes it is the cleanest headless path — a CPU renderer returning `true` unconditionally from `IsSupported()` is automatically the correct choice in that mode, since GPU-requiring plugins are filtered out.

5. **0.4.0 dropped instancing and UsdLux lights.** Instancing is cheap once transforms and the BVH exist — §14, hdEmbree supports transform-only as the instance-varying attribute, and nested instancing flattens by cartesian product. Without lights every scene renders against the hardcoded sky gradient in `camera::ray_color` (hdEmbree's ambient default, fine for 0.3.0, but the first thing anyone will ask for).

---

## 6. Scene graph requirements, for when that item comes up

From §6 and §7.2, what `scene graph with mutation` has to provide:

- Prims addressable by stable id (`SdfPath`) so `CreateRprim`/`DestroyRprim`/`Finalize` can insert, update, and **remove** by handle. `hittable_list` is currently append-only with no identity and no removal.
- A monotonic `std::atomic<int> sceneVersion` owned by the delegate, bumped on every edit; the render pass compares it against its last-seen value to decide whether to restart.
- A single `AcquireSceneForEdit()`-style gateway that calls `StopRender()` (blocking until the render thread is idle) *before* handing back a mutable scene pointer, so the stop can never be forgotten.
- `Sync()` runs **in parallel across prims**. Calls into `HdSceneDelegate` are safe; calls into your shared scene are not — hence the gateway.
- Only pull data whose dirty bit is set; pulling clean data is "at best incorrect, and at worst a crash" (`hdTiny/mesh.h`). Clear consumed bits, leave the rest.
- Transforms: per-prim `GfMatrix4d` from `sceneDelegate->GetTransform(id)`. Transforming the *ray* into object space rather than the geometry also sets up instancing for free.
- Triangulation is mandatory for a triangle-only renderer: `HdMeshUtil(&topology, id).ComputeTriangleIndices(...)`, which also produces the primitive-param map needed for per-hit face-varying/uniform primvar lookup.

---

## Appendix — board state at end of discussion

```
0.2.0 - hydra prep
  [x] output to pixel buffer
  [x] sdl viewer and make tracer into a library
  [x] thread-safe rng
  [x] progressive rendering
  [ ] hdTiny stub delegate
  [ ] camera api refactor      (matrix-driven rays; ortho/perspective)
  [ ] render target refactor
  [ ] interruptible tile-driven render loop
        [x] switch OMP to TBB
  [ ] transform support
  [ ] scene graph with mutation
  [ ] triangle mesh
  [ ] bvh with rebuild-on-mutation

0.3.0 - hydra delegate       [ ] hydra wrapper   [ ] usdview integration
0.4.0 - more features!       [ ] blender plugin  [ ] texture mapping
Wishlist                     [ ] profiling tools
```
