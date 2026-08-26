# Hydra Render Delegate — Requirements Spec

What it actually takes to wrap a custom renderer as a Hydra render delegate, derived
by reading the OpenUSD source rather than the (thin) prose docs.

**Source of truth:** OpenUSD `v26.05` (`0.26.5`), commit `2095fafaf`, checked out at
`~/opt/OpenUSD`, installed at `~/opt/usd_src_build`. Hydra ABI marker:
`HD_API_VERSION 97` (`pxr/imaging/hd/version.h:146`).

**Bias of this document:** CPU renderers. Where the API assumes a GPU, that is called
out explicitly (see §17).

---

## 1. Reference implementations in the tree

Read these in this order. They are the real specification.

| Path | What it is | Use it for |
|---|---|---|
| `extras/imaging/examples/hdTiny/` | ~630 LOC skeleton (incl. comments) that renders nothing and prints Hydra callbacks | The exact minimum bar to compile, register, and load |
| `pxr/imaging/plugin/hdEmbree/` | Full CPU ray tracer, explicitly maintained as "living documentation of that API" (`overview.dox`) | Everything real: threading, AOVs, progressive convergence, UsdLux, instancing |
| `third_party/renderman/plugin/hdPrman/` | Production delegate, ~100 files | Materials, scene-index-heavy (Hydra 2.0) design, motion blur, render settings prims |
| `third_party/renderman/plugin/hdPrmanLoader/` | Thin plugin that `dlopen`s the real delegate | Decoupling your renderer's ABI/deps from USD's plugin load |

`hdEmbree` is the closest analogue to a hand-written CPU tracer and should be treated
as the primary model throughout.

---

## 2. Execution model you are plugging into

`HdEngine::Execute()` has three phases (`pxr/imaging/plugin/hdEmbree/overview.dox`):

1. **Sync** — `HdRenderIndex::SyncAll()` walks dirty prims and calls `Sync()` on each.
   Prims pull scene data from an `HdSceneDelegate` and push it into *your* scene
   representation. **Sync runs multithreaded across prims.**
2. **Commit resources** — `HdRenderDelegate::CommitResources()`. One serial hook after
   all sync, before any task runs. No scene updates happen after this.
3. **Task execution** — tasks run; `HdxRenderTask` (or your own `HdTask`) drives
   `HdRenderPass::_Execute()`, which is where you actually start rendering.

Object ownership chain:

```
plugInfo.json  ──discovery──▶  HdRendererPlugin   (singleton per library)
                                    │ CreateRenderDelegate()
                                    ▼
                              HdRenderDelegate     (owns your scene + render thread)
                                    │ CreateRprim/Sprim/Bprim/Instancer/RenderPass
                                    ▼
        HdMesh subclass ── HdCamera ── HdRenderBuffer ── HdRenderPass
                                    │ GetRenderParam()
                                    ▼
                              HdRenderParam        (passed to every Sync(); your
                                                    scene-edit gateway)
```

**Constraint:** the render delegate owns the renderer's scene, so **one render delegate
per `HdRenderIndex`** (`renderDelegate.h` class comment, hdEmbree).

---

## 3. R1 — Plugin registration and discovery

### 3.1 C++ side

One `TF_REGISTRY_FUNCTION(TfType)` in the plugin's `rendererPlugin.cpp`:

```cpp
TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<MyRendererPlugin>();
}
```

Everything else is discovered through `TfType`, so the class name string in
`plugInfo.json` **must** match the registered C++ type name exactly.

### 3.2 `plugInfo.json`

Modeled on `pxr/imaging/plugin/hdEmbree/plugInfo.json`:

```json
{
    "Plugins": [
        {
            "Info": {
                "Types": {
                    "MyRendererPlugin": {
                        "bases": ["HdRendererPlugin"],
                        "displayName": "MyRenderer",
                        "priority": 99
                    }
                }
            },
            "LibraryPath": "@PLUG_INFO_LIBRARY_PATH@",
            "Name": "myRenderer",
            "ResourcePath": "@PLUG_INFO_RESOURCE_PATH@",
            "Root": "@PLUG_INFO_ROOT@",
            "Type": "library"
        }
    ]
}
```

Requirements:

- `bases` **must** be `["HdRendererPlugin"]` — that is how the registry finds it.
- `displayName` is load-bearing in three ways: it is the string `usdview`/`usdrecord`
  accept for `--renderer` (`pxr/usdImaging/usdAppUtils/rendererArgs.py`), it is the key
  that scene index plugins match on via `loadWithRenderer`, and it is returned by
  `HdRenderDelegate::GetRendererDisplayName()`.
- `priority` breaks ties when multiple plugins are available; Storm and Embree both
  use `99`.
- The `@PLUG_INFO_*@` tokens are CMake `configure_file` substitutions performed by
  `_plugInfo_subst()` in `cmake/macros/Private.cmake:159`. Out of tree you must
  substitute them yourself. The installed, substituted result looks like:

```json
"LibraryPath": "../hdEmbree.so",  "ResourcePath": "resources",  "Root": ".."
```

(verbatim from `~/opt/usd_src_build/plugin/usd/hdEmbree/resources/plugInfo.json`).

### 3.3 Install layout and discovery

The canonical on-disk shape (from the installed tree):

```
<root>/plugin/usd/plugInfo.json          → { "Includes": [ "*/resources/" ] }
<root>/plugin/usd/myRenderer.so
<root>/plugin/usd/myRenderer/resources/plugInfo.json
```

Discovery paths are assembled once at library-load time by `Plug_InitConfig`
(`pxr/base/plug/initConfig.cpp:64`), in this order:

1. `$PXR_PLUGINPATH_NAME` (the env var *name* is itself configurable at USD build time
   via `PXR_OVERRIDE_PLUGINPATH_NAME`, `cmake/defaults/CXXDefaults.cmake:43`; the default
   name is literally `PXR_PLUGINPATH_NAME`)
2. `PXR_BUILD_LOCATION`, then `PXR_PLUGIN_BUILD_LOCATION`
3. `PXR_INSTALL_LOCATION` if compiled in

**First path containing a plugin wins**, and relative entries are anchored to the
directory of the `libplug` shared library, not the CWD. For an out-of-tree plugin the
practical requirement is:

```sh
export PXR_PLUGINPATH_NAME=/path/to/myRenderer/resources
```

pointing at the directory *containing* `plugInfo.json`. Debug discovery with
`TF_DEBUG=PLUG_INFO_SEARCH` and `TF_DEBUG=PLUG_REGISTRATION`.

---

## 4. R2 — `HdRendererPlugin` subclass

`pxr/imaging/hd/rendererPlugin.h`. Required overrides (Hydra 1.0 surface, which is
still the supported path):

| Method | Requirement |
|---|---|
| `HdRenderDelegate *CreateRenderDelegate()` | Return a fresh delegate |
| `HdRenderDelegate *CreateRenderDelegate(HdRenderSettingsMap const&)` | Same, honoring construction-time settings |
| `void DeleteRenderDelegate(HdRenderDelegate*)` | `delete` it |
| `bool IsSupported(HdRendererCreateArgs const&, std::string *reasonWhyNot) const` | **Pure virtual.** Runtime capability check |

`HdRendererCreateArgs` (`pxr/imaging/hd/rendererCreateArgs.h`) is only two fields:

```cpp
struct HdRendererCreateArgs { bool gpuEnabled{true}; Hgi* hgi{nullptr}; };
```

A CPU renderer returns `true` unconditionally — this is exactly what makes it usable
under `usdrecord --disable-gpu`, where GPU-requiring plugins are filtered out. hdEmbree
returns `true` with the comment "we assume if the plugin loads correctly it is
supported."

The plugin class is a **singleton per library** managed by the registry; make it
non-copyable and keep it stateless.

Hydra 2.0 additions on this class (`IsSupported(HdContainerDataSourceHandle const&)`,
`GetSceneIndexInputArgs()`, `_CreateRenderer`) are **optional**. If `_CreateRenderer` is
not implemented, `CreateRenderer()` falls back to creating a 1.0 render delegate plus
back-end emulation automatically.

---

## 5. R3 — `HdRenderDelegate` subclass

`pxr/imaging/hd/renderDelegate.h`. **16 pure virtuals — all must be implemented:**

```
GetSupportedRprimTypes()      GetSupportedSprimTypes()     GetSupportedBprimTypes()
GetResourceRegistry()
CreateRenderPass(index, collection)
CreateInstancer(delegate, id)          DestroyInstancer(instancer)
CreateRprim(typeId, rprimId)           DestroyRprim(rPrim)
CreateSprim(typeId, sprimId)           CreateFallbackSprim(typeId)    DestroySprim(sprim)
CreateBprim(typeId, bprimId)           CreateFallbackBprim(typeId)    DestroyBprim(bprim)
CommitResources(tracker)
```

`hdTiny` satisfies all 16 by `TF_CODING_ERROR`-ing on everything except `mesh`, and
still loads in `usdview`. That is the floor.

### 5.1 Minimum supported prim types

Per the `hdEmbree` class docs, the minimum for a *useful* delegate is **one Rprim** plus
the **`camera` Sprim** (required by `HdxRenderTask`). Add `renderBuffer` as a Bprim as
soon as you want AOVs, which is immediately in practice.

hdEmbree's actual sets:

```cpp
RPRIM: mesh
SPRIM: camera, extComputation,
       cylinderLight, diskLight, distantLight, domeLight, rectLight, sphereLight
BPRIM: renderBuffer
```

Applications are expected to consult these lists and not call `Create*` for
unsupported types — but you should still `TF_CODING_ERROR` on the unexpected.

`CreateFallbackSprim`/`CreateFallbackBprim` must return a default-constructed prim bound
to `SdfPath::EmptyPath()` — a prim with no scene delegate backing that will never sync.

### 5.2 Recommended non-pure overrides

| Method | Why it matters |
|---|---|
| `GetRenderParam()` | Hand your scene/render-thread handle to prims during `Sync()` (§6) |
| `GetRenderSettingDescriptors()` | Populates the settings UI in `usdview` |
| `GetDefaultAovDescriptor(name)` | Declares the format/multisampling of each AOV (§8.3) |
| `GetMaterialBindingPurpose()` | Ray tracers return `HdTokens->full` (Storm returns `preview`) |
| `GetRenderStats()` | Surfaces e.g. `HdPerfTokens->numCompletedSamples` to the host |
| `IsPauseSupported()` / `Pause()` / `Resume()` | Forward to `HdRenderThread` |
| `IsStopSupported()` / `Stop(blocking)` / `Restart()` | Needed for well-behaved batch/interactive hosts |
| `CreateRenderPassState()` | Only if you need a custom render pass state |
| `GetMaterialRenderContexts()`, `GetShaderSourceTypes()`, `GetRenderSettingsNamespaces()` | Material and namespaced-settings routing (§13, §15) |

Hydra 2.0 hooks (`SetTerminalSceneIndex`, `Update`, `IsParallelSyncEnabled`,
`RequiresStormTasks`) all have working base implementations. `RequiresStormTasks()`
returning `false` (the default) is correct for a non-Storm renderer.

---

## 6. R4 — `HdRenderParam` and the scene-edit gateway

`HdRenderParam` is an opaque per-delegate object handed to every `Sync()` call. The
required pattern (`pxr/imaging/plugin/hdEmbree/renderParam.h`) is to make it the *only*
way a prim can touch renderer scene state, so that stopping the render before an edit
cannot be forgotten:

```cpp
class MyRenderParam final : public HdRenderParam {
public:
    MyScene* AcquireSceneForEdit() {
        _renderThread->StopRender();     // blocks until the render thread is idle
        (*_sceneVersion)++;              // invalidates in-flight render passes
        return _scene;
    }
};
```

Two requirements fall out of this:

- **`StopRender()` before every mutation.** Sync is multithreaded; the render thread may
  be reading your BVH.
- **A monotonic `std::atomic<int> sceneVersion`** owned by the delegate, bumped on every
  edit. The render pass compares it against its own last-seen value to decide whether to
  restart the render (§9).

---

## 7. R5 — Rprims (geometry)

Subclass `HdMesh` (or `HdBasisCurves`/`HdPoints`). Contract from
`extras/imaging/examples/hdTiny/mesh.h` and `pxr/imaging/plugin/hdEmbree/mesh.cpp`:

### 7.1 Required overrides

```cpp
HdDirtyBits GetInitialDirtyBitsMask() const override;
void Sync(HdSceneDelegate*, HdRenderParam*, HdDirtyBits*, TfToken const& reprToken) override;
void Finalize(HdRenderParam*) override;            // release scene handles
protected:
void _InitRepr(TfToken const& reprToken, HdDirtyBits*) override;
HdDirtyBits _PropagateDirtyBits(HdDirtyBits) const override;
```

### 7.2 Hard rules

- **`Sync()` is called in parallel from worker threads.** It must be threadsafe. Calls
  *into* `HdSceneDelegate` are safe; calls into your shared scene are not (hence
  `AcquireSceneForEdit`).
- **Only pull data whose dirty bit is set.** Scene delegates implement just-in-time
  schemes; pulling clean data is "at best incorrect, and at worst a crash"
  (`hdTiny/mesh.h`).
- **Clear the bits you consumed** before returning; leave the rest.
- State is populated lazily in `Sync()` and released in `Finalize()`, so existence and
  population are decoupled.

### 7.3 Initial dirty bits (hdEmbree's set, a good default)

```
Clean | InitRepr | DirtyPoints | DirtyTopology | DirtyTransform | DirtyVisibility
      | DirtyCullStyle | DirtyDoubleSided | DirtyDisplayStyle | DirtySubdivTags
      | DirtyPrimvar | DirtyNormals | DirtyInstancer
```

### 7.4 What you must actually consume

Queried through `HdChangeTracker::Is*Dirty(...)` helpers:

| Data | Accessor | Note |
|---|---|---|
| Points | `GetPrimvar(id, HdTokens->points)` | Gated on `IsPrimvarDirty` for `points`, not a topology bit |
| Topology | `GetMeshTopology(id)` → `HdMeshTopology` | Carries scheme, orientation, face counts/indices, hole indices |
| Triangulation | `HdMeshUtil(&topology, id).ComputeTriangleIndices(...)` | **Required** for a triangle-only renderer; also produces the primitive-param map needed to look up face-varying/uniform primvars per hit |
| Transform | `sceneDelegate->GetTransform(id)` | `GfMatrix4d` |
| Visibility | `sceneDelegate->GetVisible(id)` | |
| Double-sidedness / cull style | `GetDoubleSided(id)`, `GetCullStyle(id)` | hdEmbree implements culling with an Embree intersection filter |
| Display style | `GetDisplayStyle(id).refineLevel` | Drives subdivision refinement level |
| Subdiv tags | `topology.GetSubdivTags()` | Creases/corners/vertex interpolation rules |
| Primvars | `GetPrimvarDescriptors(id, interp)` for each of the 5 interpolations | |
| Instancing | see §14 | |

**Primvar interpolation is your problem.** Hydra hands you raw `VtValue` arrays plus an
interpolation mode (`constant`, `uniform`, `varying`, `vertex`, `faceVarying`);
correctly evaluating one at a hit point requires the interpolation mode, the triangulated
index buffer, and the primitive-param table. `pxr/imaging/plugin/hdEmbree/sampler.h`
(`HdEmbreeBufferSampler`, `HdEmbreePrimvarSampler`, `HdEmbreeTypeHelper`) and
`meshSamplers.h` are a directly reusable design for this and worth porting rather than
reinventing.

### 7.5 Reprs

`_InitRepr` must accept the repr tokens the host asks for. `HdxRenderTask` collections
in practice request `HdReprTokens->refined`. A renderer with a single shading path can
treat all reprs identically (hdEmbree effectively does), but the override must exist and
must add any extra dirty bits it needs before propagation.

---

## 8. R6 — Bprims: `HdRenderBuffer` and AOVs

### 8.1 Required overrides

`pxr/imaging/hd/renderBuffer.h` — 12 pure virtuals:

```
Allocate(dimensions, format, multiSampled)
GetWidth() GetHeight() GetDepth() GetFormat() IsMultiSampled()
Map() Unmap() IsMapped()
Resolve()
IsConverged()
_Deallocate()                                  // protected
```

Plus, for a CPU renderer, you will want hdEmbree's additions: `SetConverged(bool)`,
typed `Write(pixel, numComponents, value)` and `Clear(...)` helpers.

Only `depth == 1` need be supported.

### 8.2 The multisample/resolve split

hdEmbree's buffer holds three allocations (`renderBuffer.h`):

- `_buffer` — the resolved output, in the requested `HdFormat`
- `_sampleBuffer` — the accumulation buffer, always float32 or int32 with the *same
  component count* as the requested format
- `_sampleCount` — per-pixel sample counts

`Resolve()` divides accumulation by count and converts into `_buffer`. This is the
mechanism that makes progressive refinement legible to the host: the host reads
`_buffer` at any time and gets a correct, if noisy, image.

`Map()`/`Unmap()` maintain an atomic mapper count; `IsMapped()` reports it. The render
thread maps all bound buffers for the duration of a render and unmaps at the end.

### 8.3 AOV descriptors

`HdRenderDelegate::GetDefaultAovDescriptor(name)` declares format, multisampling, and
clear value per AOV. hdEmbree's table, which is a reasonable baseline:

| AOV token | Format | Multisampled | Clear |
|---|---|---|---|
| `color` | `HdFormatFloat32Vec4` | **yes** | `(0,0,0,0)` |
| `normal`, `Neye` | `HdFormatFloat32Vec3` | no | `(-1,-1,-1)` |
| `depth` | `HdFormatFloat32` | no | `1.0` |
| `cameraDepth` | `HdFormatFloat32` | no | `0.0` |
| `primId`, `instanceId`, `elementId` | `HdFormatInt32` | no | `-1` |
| `primvars:<name>` (via `HdParsedAovToken`) | `HdFormatFloat32Vec3` | no | `(0,0,0)` |

Return a default-constructed `HdAovDescriptor()` for anything unsupported. The full token
list is `HD_AOV_TOKENS` in `pxr/imaging/hd/tokens.h:362`.

**`color` must be premultiplied alpha** for correct compositing (comment at
`tokens.h:365`).

---

## 9. R7 — `HdRenderPass`

Subclass `HdRenderPass`; override `_Execute(renderPassState, renderTags)`,
`IsConverged()`, and `_MarkCollectionDirty()`.

Critically, `_Execute()` is **not** "draw a frame". For a progressive renderer it is
"reconcile requested state against current state, and (re)start the background render if
anything changed." `pxr/imaging/plugin/hdEmbree/renderPass.cpp` does exactly this, and
the required change detection is:

```
needStartRender = false
1. sceneVersion    != last  →  needStartRender                    (§6)
2. settingsVersion != last  →  StopRender; re-read every render setting; needStartRender
3. view/proj matrix changed →  StopRender; SetCamera;              needStartRender
4. data window changed      →  StopRender; SetDataWindow; (re)allocate fallback buffers
5. AOV bindings changed     →  StopRender; SetAovBindings; Clear;  needStartRender
if (needStartRender) { MarkAovBuffersUnconverged(); renderThread->StartRender(); }
```

Requirements embedded in that:

- **Render settings are polled, not pushed.** Compare
  `renderDelegate->GetRenderSettingsVersion()` against a cached value and re-read via
  `GetRenderSetting<T>(token, fallback)`.
- **Empty AOV bindings are legal input but not a legal render state.** If
  `renderPassState->GetAovBindings()` is empty, synthesize a `color` + `depth` binding
  against render buffers the pass owns itself.
- **Support both framing APIs.** Prefer `renderPassState->GetFraming().dataWindow`
  (`CameraUtilFraming`); fall back to `GetViewport()` for older hosts. Data window
  coordinates are **y-down**, while image line order is bottom-to-top — the flip is
  explicit in hdEmbree's `_RenderTiles`.
- **`IsConverged()`** returns the AND of `IsConverged()` over all bound render buffers
  (or the pass's own cached flag when using fallback buffers). Hosts use this to decide
  whether to keep re-executing the task.
- The destructor must `StopRender()` — the render thread may still be writing into
  pass-owned buffers.

hdEmbree does **not** implement collection include/exclude paths, render tags, or clip
planes, and does not support multiple concurrent render passes (they would overwrite
each other). Those are legitimately optional for a first delegate.

---

## 10. R8 — Threading and cancellation

This is the part a CPU renderer must get right, and Hydra provides the machinery.

### 10.1 `HdRenderThread`

`pxr/imaging/hd/renderThread.h` — an optional but strongly recommended utility. State
machine:

```
StateInitial ──StartThread()──▶ StateIdle ──StartRender()──▶ StateRendering
                                    ▲                            │
                                    └────── StopRender() ────────┘
              StopThread(): Idle|Rendering → StateTerminated → StateInitial
```

Wiring in the delegate constructor:

```cpp
_renderThread.SetRenderCallback(std::bind(_RenderCallback, &_renderer, &_renderThread));
_renderThread.SetShutdownCallback(...);   // optional, for thread-local cleanup
_renderThread.StartThread();
// destructor: _renderThread.StopThread();
```

Required behaviours inside the render callback:

- Poll `IsPauseRequested()` in a sleep loop (hdEmbree: 10 ms) and break on
  `IsStopRequested()`.
- Poll `IsStopRequested()` as a **cancellation point** at least once per sample pass,
  and again inside the inner tile loop so the *first* pass is also interruptible.
- Use `LockFramebuffer()` if the host must not observe a torn buffer. A CPU renderer
  writing into an accumulation buffer that the host reads via `Resolve()` can often skip
  this — hdEmbree does, relying on multisample `Resolve()` instead.

Semantics that matter: `StopRender()` is fully threadsafe and callable from many Hydra
threads; `StartRender()` is not — call it from the render pass only. The design intent is
that a *static* scene is never interrupted: you only stop the render when a prim is about
to be edited.

### 10.2 Parallelism

Use `WorkParallelForN` from `pxr/base/work` rather than raw threads or a private pool.
hdEmbree tiles the data window and schedules `numTilesX * numTilesY` tiles:

```cpp
WorkParallelForN(numTilesX*numTilesY,
    std::bind(&MyRenderer::_RenderTiles, this, renderThread, sampleIdx,
              std::placeholders::_1, std::placeholders::_2));
```

Requirements:

- **Honor the host's concurrency limit.** `WorkGetConcurrencyLimitSetting()` /
  `PXR_WORK_THREAD_LIMIT` and the `HdRenderSettingsTokens->threadLimit` setting. A
  renderer that saturates all cores inside a DCC is a bug. hdEmbree additionally scopes a
  `tbb::task_scheduler_init` for `TBB_INTERFACE_VERSION_MAJOR < 12`.
- **RNG must be per-work-item, not global.** hdEmbree seeds a
  `std::default_random_engine` per tile from
  `TfHash::Combine(seed, tileStart, sampleNum)`. This also gives deterministic output
  for a fixed seed regardless of thread scheduling — expose the seed as a render setting
  (`randomNumberSeed`, `-1` meaning nondeterministic).
- Progressive loop shape: `for (i in 0..samplesToConvergence)` → one full-frame pass of
  one sample per pixel → publish `_completedSamples.store(i+1)` → cancellation point.
  After pass 0, mark all *non*-multisampled AOVs converged; if none are multisampled,
  stop. At the end, unmap every buffer and `SetConverged(true)`.

---

## 11. R9 — Camera

Use the built-in `HdCamera` Sprim (`CreateSprim` returns `new HdCamera(sprimId)`) — do
not write your own unless you need something exotic. The render pass gets what it needs
from `HdRenderPassState`:

```cpp
GfMatrix4d view = renderPassState->GetWorldToViewMatrix();
GfMatrix4d proj = renderPassState->GetProjectionMatrix();
```

Your renderer therefore has to generate rays **from matrices**, not from a fov/aperture
description. hdEmbree's recipe (`renderer.cpp`, `_RenderTiles`) is the reference:

```cpp
// pixel → NDC, with optional sub-pixel jitter, over the data window
GfVec3f ndc(2*((x + jx - minX)/w) - 1, 2*((y + jy - minY)/h) - 1, -1);
GfVec3f nearPlaneTrace = inverseProj.Transform(ndc);

bool isOrthographic = round(proj[3][3]) == 1.0;
if (isOrthographic) { origin = nearPlaneTrace;      dir = {0,0,-1}; }
else                { origin = {0,0,0};             dir = nearPlaneTrace; }

origin = inverseView.Transform(origin);
dir    = inverseView.TransformDir(dir).GetNormalized();
```

Consequences for a renderer with its own camera model: it needs an entry point that
accepts an arbitrary inverse-projection matrix, supports orthographic and perspective
from the same code path, and renders only within a **data window sub-rect** of a
possibly larger buffer. Depth of field / motion blur, if supported, must come from
`HdCamera` attributes (focus distance, f-stop, shutter) rather than the projection
matrix, which carries neither.

---

## 12. R10 — Lights (optional, tiered)

Lights arrive as Sprims typed `sphereLight`, `rectLight`, `diskLight`, `cylinderLight`,
`distantLight`, `domeLight`, plus the generic `light`.
`pxr/imaging/plugin/hdEmbree/light.h` is explicitly "a reference implementation of USD
Lux support ... useful reference for understanding how to implement USD Lux support for
other renderers," and its header documents precisely which `UsdLux` attributes it does
and does not honor:

Supported there: `LightAPI` (`intensity`, `exposure`, `diffuse`, `normalize`, `color`,
`enableColorTemperature`, `colorTemperature`), per-type geometry
(`radius`/`width`/`height`/`length`/`angle`), `texture:file` on rect and dome, and the
full `ShapingAPI` including IES profiles (`pxrIES/`).

Not supported there: light shaders, mesh/volume/geometry/portal/plugin lights,
`LightFilter`, `ShadowAPI`, `light:shaderId`, `inputs:specular`, light linking.

Treat that split as a sane scope boundary for a first implementation. Two related pieces
that come free: the `domeLightCameraVisibility` render setting, and
`hdsi/domeLightCameraVisibilitySceneIndex.h`.

---

## 13. R11 — Materials (optional)

hdEmbree ships **no material support at all** — every surface is a 100% diffuse BRDF,
optionally tinted by the `displayColor` primvar (`enableSceneColors`). A delegate is
fully functional without materials.

When you do want them:

- Add `HdPrimTypeTokens->material` to the Sprim list and implement an `HdMaterial`
  subclass.
- `GetMaterialBindingPurpose()` → `HdTokens->full` for ray tracers.
- `GetMaterialRenderContexts()` and `GetShaderSourceTypes()` declare which shading
  network flavors you accept; `GetMaterialNetworkSelector()` is the legacy single-value
  form.
- Useful reusable scene indices: `hdsi/materialBindingResolvingSceneIndex.h`,
  `materialRenderContextFilteringSceneIndex.h`, `materialPrimvarTransferSceneIndex.h`,
  `nodeIdentifierResolvingSceneIndex.h`, `sceneMaterialPruningSceneIndex.h`,
  `unboundMaterialPruningSceneIndex.h`.
- MaterialX flows through `pxr/imaging/hdMtlx`; see hdPrman's `matfilt*` files for how a
  production delegate rewrites `UsdPreviewSurface` into native shading.

---

## 14. R12 — Instancing

Subclass `HdInstancer` and implement `Sync()`. The reference approach
(`pxr/imaging/plugin/hdEmbree/instancer.h`) is:

- Cache instancer primvars as `HdVtBufferSource*` keyed by token.
- Expose `ComputeInstanceTransforms(prototypeId)` returning a `VtMatrix4dArray`, one
  matrix per instance, composed from the scene delegate's `instancerTransform` and the
  primvars `hydra:instanceTransforms`, `hydra:instanceTranslations`,
  `hydra:instanceRotations`, `hydra:instanceScales`.
- **Nested instancing must be flattened** by recursing to parent instancers and taking
  the cartesian product of transform arrays at each level.
- The Rprim's `Sync()` calls this and inserts the prototype into the scene once per
  transform. Rprims must re-sync when `DirtyInstancer` or `DirtyTransform` is set, and
  must call `Sync()` on parent instancers first.

hdEmbree supports **transform only** as an instance-varying attribute. That is a fine
starting scope.

---

## 15. R13 — Render settings

Three distinct mechanisms, easily confused (the distinction is spelled out in
`pxr/imaging/hd/renderSettings.h:39`):

1. **`HdRenderSettingsMap`** — token→`VtValue` dictionary passed to the delegate
   constructor.
2. **`HdRenderSettingDescriptorList`** — the reflection API. Build it in the delegate's
   `_Initialize()`, pass it to `_PopulateDefaultSettings()`, return it from
   `GetRenderSettingDescriptors()`. This is what drives the settings UI, and the
   descriptor's `VtValue` type determines the widget. Bump-detected by the render pass
   through `GetRenderSettingsVersion()`.
3. **`HdRenderSettings` Bprim** — render settings *scene description* (`UsdRender`), the
   direction Hydra is moving. It is deliberately a Bprim so it syncs before Sprims and
   Rprims. Carries `renderProducts` (each with resolution, camera, and flattened
   `renderVars`), `includedPurposes`, `materialBindingPurposes`, `renderingColorSpace`,
   `frameNumber`, `disableMotionBlur`, `disableDepthOfField`. Pair with
   `hdsi/sceneGlobalsSceneIndex.h` (which designates the *active* render settings prim)
   and `hdsi/renderSettingsFilteringSceneIndex.h`. Required for a proper batch-rendering
   story; not required for interactive viewport work.

Standard setting tokens worth honoring (`HD_RENDER_SETTINGS_TOKENS`,
`pxr/imaging/hd/tokens.h:438`):

```
enableShadows  enableSceneMaterials  enableSceneLights  enableExposureCompensation
domeLightCameraVisibility  convergedVariance  convergedSamplesPerPixel
threadLimit  enableInteractive
```

`convergedSamplesPerPixel` and `threadLimit` are the two a CPU path tracer must not
ignore. Renderer-specific settings should additionally be exposed as
`TfGetEnvSetting` env vars for headless debugging — see
`pxr/imaging/plugin/hdEmbree/config.cpp` (`HDEMBREE_SAMPLES_TO_CONVERGENCE`,
`HDEMBREE_TILE_SIZE`, `HDEMBREE_JITTER_CAMERA`, …).

---

## 16. R14 — Scene index plugins (how to avoid work)

Hydra will hand you `sphere`, `cube`, `cone`, `cylinder`, `capsule`, `plane`, tet meshes,
NURBS, pinned curves, and ext-computation-driven primvars unless you ask for them to be
converted first. Registering scene index plugins keyed to your `displayName` is how you
narrow the input language to what your renderer supports.

The full pattern is ~70 lines — `pxr/imaging/plugin/hdEmbree/implicitSurfaceSceneIndexPlugin.cpp`:

```cpp
TF_REGISTRY_FUNCTION(TfType) {
    HdSceneIndexPluginRegistry::Define<MyImplicitSurfaceSceneIndexPlugin>();
}
TF_REGISTRY_FUNCTION(HdSceneIndexPlugin) {
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "MyRenderer",                     // must equal plugInfo displayName
        _tokens->sceneIndexPluginName,
        /* inputArgs = */ nullptr,
        /* insertionPhase = */ 0,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}
// _AppendSceneIndex() returns HdsiImplicitSurfaceSceneIndex::New(inputScene, args)
// with each of sphere/cube/cone/cylinder/capsule/plane mapped to "toMesh".
```

...plus a matching `HdSceneIndexPlugin`-based entry in `plugInfo.json` carrying
`"loadWithRenderer": "MyRenderer"`.

Ready-made scene indices in `pxr/imaging/hdsi/` worth adopting immediately:
`implicitSurfaceSceneIndex`, `extComputationPrimvarPruningSceneIndex`,
`tetMeshConversionSceneIndex`, `nurbsApproximatingSceneIndex`,
`pinnedCurveExpandingSceneIndex`, `legacyDisplayStyleOverrideSceneIndex`,
`primTypePruningSceneIndex`, `velocityMotionResolvingSceneIndex`,
`sceneGlobalsSceneIndex`, `debuggingSceneIndex`.

`third_party/renderman/plugin/hdPrman/plugInfo.json` shows the mature form: ~24 scene
index plugins with explicit `tags` and `ordering` (`after`/`before`/`position`) forming a
phased pipeline. Worth reading for insertion-order semantics, not for first-pass scope.

---

## 17. CPU-renderer-specific considerations

This is where the API's GPU assumptions surface, and the news is mostly good.

1. **The delegate needs no GPU dependency.** hdTiny links only `hd` and `tf`. hdEmbree
   links `tf vt gf work hd hdsi hio` plus Embree. `pxr/imaging/hd` and `pxr/imaging/hdsi`
   are built unconditionally; only `hdSt` and `hdx` are gated on
   `PXR_BUILD_GPU_SUPPORT`/`PXR_ENABLE_GL_SUPPORT` (`pxr/imaging/hdx/CMakeLists.txt:6`).
   `HdRenderDelegate::SetDrivers(HdDriverVector const&)` — the `Hgi` hand-off — can be
   left unimplemented.

2. **CPU AOV buffers are a first-class supported case.** `HdxAovInputTask` explicitly
   handles it: "The aov render buffer can be a GPU or CPU buffer, while the resulting
   output HgiTexture will always be a GPU texture"
   (`pxr/imaging/hdx/aovInputTask.h:23`). It calls `HdRenderBuffer::GetResource()`; if
   that returns an empty `VtValue` (the base-class default, and what a CPU buffer should
   return), it falls back to `buffer->Map()` and uploads the CPU pixels into an
   `HgiTexture`. So a CPU delegate gets viewport presentation, color correction, and
   picking for free without touching Hgi. The cost is a full-frame CPU→GPU upload per
   presented frame.

3. **`gpuEnabled == false` is a supported mode.** `usdrecord --disable-gpu` sets
   `UsdImagingGLEngine`'s `gpuEnabled` to false and restricts the renderer list to
   plugins whose `IsSupported()` accepts it. A CPU renderer that returns `true`
   unconditionally is automatically the correct choice in that mode — this is the
   cleanest headless/batch path.

4. **hdEmbree's `PXR_BUILD_GPU_SUPPORT` guard is about its test, not the delegate.** Its
   `CMakeLists.txt` returns early when GPU support is off because `testHdEmbree` links
   `hdSt`/`hdx` and uses `HdSt_UnitTestGLDrawing` for a window. Do not copy that guard
   into a CPU-only plugin.

5. **`hdx` is only needed for the *host application*, not the delegate.** If your own
   harness supplies tasks (see the `MyDrawTask` sketch in
   `extras/imaging/examples/hdTiny/testenv/testHdTiny.cpp`), you can drive `HdEngine`
   with `hd` alone. `usdview` integration is what pulls in `hdx`.

6. **Thread-pool coexistence is a real constraint.** USD is built against oneTBB; a
   renderer with its own TBB arena or a private thread pool will oversubscribe inside a
   DCC. Route parallelism through `pxr/base/work` and respect
   `WorkGetConcurrencyLimitSetting()`.

7. **Progressive rendering is the expected interaction model.** `IsConverged()`,
   `convergedSamplesPerPixel`, multisampled render buffers with `Resolve()`,
   `numCompletedSamples` in `GetRenderStats()`, and `Pause`/`Resume`/`Stop`/`Restart`
   together assume a renderer that refines over time and can be interrupted at fine
   granularity. A renderer whose `Render()` is an uninterruptible blocking call will make
   the viewport unusable.

---

## 18. R15 — Build and link requirements

1. **ABI compatibility is exact.** The plugin must be built with the same compiler,
   standard library, C++ standard, and USD version as the host USD. `PXR_NAMESPACE` is
   version-mangled (`pxrInternal_v0_26_5__pxrReserved__`), so a mismatch fails at
   link/`dlopen` time rather than silently.

2. **In-tree** is the easy path: drop the plugin under `pxr/imaging/plugin/` (or
   `extras/imaging/examples/`) and use the `pxr_plugin()` macro, which handles the
   `plugInfo.json` substitution, install layout, and `RESOURCE_FILES` wiring:

```cmake
pxr_plugin(myRenderer
    LIBRARIES     tf vt gf work hd hdsi hio  <your renderer lib>
    INCLUDE_DIRS  <your includes>
    PUBLIC_CLASSES  renderDelegate rendererPlugin renderPass renderBuffer
                    mesh instancer renderer sampler light
    PUBLIC_HEADERS  renderParam.h
    PRIVATE_CLASSES debugCodes implicitSurfaceSceneIndexPlugin
    RESOURCE_FILES  plugInfo.json
)
```

3. **Out of tree**, you must reproduce what the macro does:
   - `find_package(pxr REQUIRED)` against `~/opt/usd_src_build/pxrConfig.cmake`
     (targets in `cmake/pxrTargets.cmake`).
   - Build a `MODULE`/`SHARED` library.
   - `configure_file()` your `plugInfo.json`, setting `PLUG_INFO_LIBRARY_PATH` to the
     library path *relative to the resources directory* (e.g. `../myRenderer.so`),
     `PLUG_INFO_RESOURCE_PATH` to `resources`, `PLUG_INFO_ROOT` to `..`.
   - Install to `<prefix>/plugin/usd/myRenderer.so` +
     `<prefix>/plugin/usd/myRenderer/resources/plugInfo.json`.
   - Point `PXR_PLUGINPATH_NAME` at that resources directory.

4. **`TF_REGISTRY_FUNCTION` blocks must not be stripped.** They rely on static
   initializers in the shared object; avoid `--gc-sections`/LTO configurations that drop
   them, and do not hide the registration TU's symbols.

5. **Precompiled headers**: `pxr_plugin` expects a `pch.h` unless you pass
   `DISABLE_PRECOMPILED_HEADERS` (hdTiny does).

---

## 19. Conformance and testing

| Level | How |
|---|---|
| Loads at all | `TF_DEBUG=PLUG_INFO_SEARCH,PLUG_REGISTRATION` and confirm your type is registered |
| Discoverable | `python -c "from pxr import UsdImagingGL; print(UsdImagingGL.Engine.GetRendererPlugins())"` |
| Minimal headless render | Copy `extras/imaging/examples/hdTiny/testenv/testHdTiny.cpp`: get the plugin from `HdRendererPluginRegistry`, `CreateRenderDelegate()`, `HdRenderIndex::New(delegate, {})`, populate with `HdUnitTestDelegate` (`AddCube`), add an `HdxRenderTask` with `HdxRenderTaskParams` and a `geometry`/`refined` collection, `HdEngine::Execute()`. Assert with `TfErrorMark::IsClean()` |
| Interactive | `usdview --renderer MyRenderer scene.usda` |
| Batch | `usdrecord --renderer MyRenderer [--disable-gpu] scene.usda out.png` |
| Diagnostics | Register `TF_DEBUG_CODES` (hdEmbree's `debugCodes.h`) so failures are traceable |

The `hdTiny` test also demonstrates the fallback when you do not want `hdx`: a
hand-written `HdTask` with `Sync`/`Prepare`/`Execute`.

---

## 20. Suggested implementation tiers

Ordered so that each tier is independently verifiable.

- **T0 — Loads.** `plugInfo.json` + `HdRendererPlugin` + an `hdTiny`-shaped delegate that
  stubs all 16 pure virtuals and prints callbacks. Verify it appears in
  `GetRendererPlugins()` and can be selected in `usdview`.
- **T1 — First pixels.** `HdRenderBuffer` Bprim, `HdCamera` Sprim, a render pass that
  reconciles camera + data window + AOV bindings, and a single-sample synchronous render
  that fills `color`. Verify with `usdrecord`.
- **T2 — Geometry.** `HdMesh` subclass: points, topology, `HdMeshUtil` triangulation,
  transform, visibility. `HdRenderParam` + `sceneVersion` gateway.
- **T3 — Progressive + threaded.** `HdRenderThread`, `WorkParallelForN` over tiles,
  multisampled `color` with `Resolve()`, `convergedSamplesPerPixel`, `IsConverged()`,
  `Pause`/`Resume`/`Stop`, per-tile RNG seeding, `GetRenderStats()`. This is the tier
  where the delegate becomes pleasant to use interactively.
- **T4 — Scope narrowing + fidelity.** Implicit-surface and ext-computation scene index
  plugins; primvar samplers with correct interpolation; `depth`/`normal`/`primId` AOVs;
  instancing; `HdRenderSettingDescriptorList`.
- **T5 — Production.** UsdLux lights, materials, `HdRenderSettings` prim + render
  products for batch, motion blur, `Restart()`/`InvokeCommand()`.

---

## 21. File index

Everything cited above, relative to `~/opt/OpenUSD`:

**Core API**
- `pxr/imaging/hd/rendererPlugin.h` — plugin base, 1.0 and 2.0 surfaces
- `pxr/imaging/hd/rendererPluginRegistry.h` — `Define<>()`, lookup
- `pxr/imaging/hd/rendererCreateArgs.h` — `gpuEnabled`, `hgi`
- `pxr/imaging/hd/renderDelegate.h` — the 16 pure virtuals + optional hooks
- `pxr/imaging/hd/renderPass.h`, `renderPassState.h` — `_Execute`, framing, AOV bindings
- `pxr/imaging/hd/renderBuffer.h` — 12 pure virtuals, `GetResource()`
- `pxr/imaging/hd/renderThread.h` — state machine, cancellation, worked example
- `pxr/imaging/hd/renderSettings.h` — settings-prim schema and the three-mechanism note
- `pxr/imaging/hd/tokens.h` — `HD_AOV_TOKENS` (:362), `HD_RENDER_SETTINGS_TOKENS` (:438)
- `pxr/imaging/hd/mesh.h`, `meshUtil.h`, `changeTracker.h`, `instancer.h`, `camera.h`, `light.h`
- `pxr/imaging/hd/sceneIndexPluginRegistry.h`
- `pxr/imaging/hdsi/` — reusable scene indices

**Reference plugins**
- `extras/imaging/examples/hdTiny/` (+ `README.md`, `testenv/testHdTiny.cpp`)
- `pxr/imaging/plugin/hdEmbree/` — `overview.dox` first, then `renderDelegate.{h,cpp}`,
  `renderPass.cpp`, `renderer.{h,cpp}`, `renderBuffer.h`, `renderParam.h`, `mesh.cpp`,
  `sampler.h`, `meshSamplers.h`, `instancer.h`, `light.h`, `config.cpp`,
  `implicitSurfaceSceneIndexPlugin.cpp`
- `third_party/renderman/plugin/hdPrman/` — production scale; `plugInfo.json` for scene
  index ordering
- `third_party/renderman/plugin/hdPrmanLoader/rendererPlugin.cpp` — `dlopen` decoupling

**Plumbing**
- `pxr/base/plug/initConfig.cpp:64` — plugin search path assembly
- `cmake/macros/Private.cmake:159` — `_plugInfo_subst`
- `cmake/defaults/CXXDefaults.cmake:43` — `PXR_PLUGINPATH_NAME`
- `pxr/imaging/hdx/aovInputTask.{h,cpp}` — CPU buffer → `HgiTexture` upload
- `pxr/usdImaging/usdImagingGL/engine.h` — `GetRendererPlugins`/`SetRendererPlugin`
- `pxr/usdImaging/usdAppUtils/rendererArgs.py` — how `--renderer` resolves display names
- `pxr/usdImaging/bin/usdrecord/usdrecord.py` — batch path, `--disable-gpu`
