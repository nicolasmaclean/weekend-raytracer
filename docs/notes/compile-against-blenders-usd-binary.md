# Compiling `hdWeekend` against Blender's USD binary

**Date:** 2026-09-07
**Question that framed this:** to write the Blender add-on, do I need to compile Blender from source, or just pin a Blender version?

**Answer:** neither, quite. No Blender source build is needed — Blender publishes its USD build as
binaries. But pinning a version is not sufficient either: Blender renames USD's internal C++
namespace, so `hdWeekend` needs a *second* build against Blender's headers and lib. One source
tree, two build directories.

> **Partly superseded — see [[blender-addon]].** The add-on shipped targeting **Blender 4.5.13 LTS**
> (USD 25.02, Python 3.11), not 5.2.x. The reasoning below on namespaces, the monolithic lib, and one
> `.so` per Blender version (§1, §2, §7) still holds. What changed:
>
> - **§3 version choice:** 4.5 was chosen for the LTS install base and support window. The
>   `IsSupported` shim this section treated as a reason to avoid older versions now exists as
>   `hydra/compat.h` (`HDW_HAS_RENDERER_CREATE_ARGS`).
> - **§5.2 C++20:** does not apply. Blender 4.5 builds its dependencies with C++17, so the existing
>   `set(CMAKE_CXX_STANDARD 17)` is correct for both flavours.
> - **§4 sparse checkout:** the branch is `blender-v4.5-release` and the set is
>   `usd tbb python/include`. `materialx` is not needed, but `python/include` is, because Blender's
>   USD is built with Python support and its headers include `<Python.h>`.
> - **§6 snippet:** `bl_use_preview` shipped as `False` (no materials yet). The `aovToken:` keys are
>   **mandatory** for a CPU engine rather than just worth copying. Depth maps to `cameraDepth`, not
>   `depth`.

**Sources** (all verified 2026-09-07):

- `build_files/build_environment/cmake/usd.cmake` and `versions.cmake` on Blender `main` and at tags `v4.2.0`…`v5.2.0`
- `build_files/cmake/platform/platform_unix.cmake` @ `v5.1.0`
- `lib-linux_x64.git`, branches `blender-v5.1-release` / `blender-v5.2-release`, path `usd/`
- `scripts/addons_core/hydra_storm/engine.py` @ `v5.1.0`
- [`bpy.types.HydraRenderEngine`](https://docs.blender.org/api/current/bpy.types.HydraRenderEngine.html) (docs `current` = 5.2)
- OpenUSD `pxr/imaging/hd/rendererPlugin.h` @ tags `v25.08`, `v26.03`, `v26.05`

---

## 1. Why our existing 26.05 build cannot be reused

`usd.cmake` builds USD with a custom internal namespace, specifically so Blender's USD does not
clash with a pip-installed `usd-core` when `bpy` is used as a module:

```cmake
string(REPLACE "." "_" USD_NAMESPACE "pxrBlender_v${USD_VERSION}")
-DPXR_SET_INTERNAL_NAMESPACE=${USD_NAMESPACE}
```

Confirmed in the shipped headers (`usd/include/pxr/pxr.h`, branch `blender-v5.2-release`):

```c
#define PXR_INTERNAL_NS pxrBlender_v26_03__pxrReserved__   // ours: pxrInternal_v0_26_05__pxrReserved__
```

`pxr::` is only an alias for `PXR_INTERNAL_NS`, so delegate *source* compiles unchanged either way —
but every mangled symbol differs. Even at an identical USD version, our `hdWeekend.so` would fail to
resolve against Blender's USD. This is the whole reason a separate build config is required.

## 2. Why external delegates work at all

`usd.cmake` passes both:

```
-DBUILD_SHARED_LIBS=ON
-DPXR_BUILD_MONOLITHIC=ON
```

producing a single **monolithic shared** `libusd_ms.so`, which `platform_unix.cmake` folds into the
release with `add_bundled_libraries(usd/lib)`. One `.so`, one `TfType` registry — so a delegate
loaded into Blender shares Blender's registry. A static monolithic build would have made this
impossible.

Blender also harvests the `pxr` Python bindings into its own `site-packages` (`Plug`, `Sdf`, `Tf`,
`Usd`, `UsdImagingGL` all present), which is what makes the add-on's `register()` hook work (§5).

## 3. Version choice: Blender 5.2.x

| Blender | USD | Python |
| --- | --- | --- |
| 4.5 LTS (4.5.13) | 25.02 | 3.11 |
| 5.0.x | 25.08 | 3.11 |
| 5.1.x | 25.08 | 3.13 |
| **5.2.x (5.2.1, current)** | **26.03** | 3.13.13 |

**Target 5.2.x**, and not merely because it is newest. `hydra/rendererPlugin.cpp` overrides

```cpp
bool IsSupported(HdRendererCreateArgs const &, std::string *reasonWhyNot) const override;
```

and that overload is the pure virtual only from USD 26.x. Checked against upstream `rendererPlugin.h`:

- `v25.08` — pure virtual is `virtual bool IsSupported(bool gpuEnabled = true) const = 0;`. No `HdRendererCreateArgs` at all.
- `v26.03` / `v26.05` — `IsSupported(HdRendererCreateArgs const &, std::string *)` is the pure virtual; the `bool gpuEnabled` form is present but deprecated.

So a 5.0/5.1 target (USD 25.08) needs a `#if PXR_VERSION` shim in the plugin, while 5.2's 26.03 is
two months of drift from our 26.05 and should need approximately nothing. Revisit if we ever want
4.5 LTS coverage — that is a 25.02 shim, not a recompile.

## 4. Getting the SDK without building Blender

Blender's precompiled dependencies live in a git-LFS repo with a branch per release. Sparse-checkout
just what we need:

```bash
git clone --filter=blob:none --no-checkout \
  https://projects.blender.org/blender/lib-linux_x64.git -b blender-v5.2-release
cd lib-linux_x64
git sparse-checkout set usd tbb materialx
git checkout
```

`usd/` contains exactly three directories — and that limitation matters:

```
usd/include/            # headers, with the pxrBlender namespace baked in
usd/lib/libusd_ms.so    # the monolithic shared lib (LFS pointer until checkout)
usd/lib/usd/            # core plugInfo tree: hd, hdSt, hdsi, usdImaging, ...
usd/plugin/usd/         # hdStorm, hioOiio, usdShaders, sdrGlslfx
```

These are the same binaries that ship inside the release download, so a delegate built against them
matches a stock Blender 5.2.1 install — no source build, no custom Blender.

**Do not try to reproduce Blender's USD build ourselves.** `usd.cmake` applies a stack of patches
(`usd.diff`, `usd_ctor.diff`, `usd_noboost.diff`, `usd_core_profile.diff`, three Vulkan patches, and
three cherry-picked upstream commits) on top of a namespace rename. Use the harvested binaries.

## 5. Changes needed for a `build-hydra-blender` config

Against `hydra/CMakeLists.txt` as it stands:

1. **`find_package(pxr)` will not work.** No `pxrConfig.cmake` and no `cmake/` directory is
   harvested — only `include`, `lib`, `plugin`. Set the include path and link
   `usd/lib/libusd_ms.so` directly, replacing `target_link_libraries(hdWeekend PRIVATE hd tf hdsi)`;
   the monolithic lib is the single link target.
2. **C++20, not C++17.** Blender 5.1+ sets `CMAKE_CXX_STANDARD 20` for core (`CMakeLists.txt`) and
   `-std=c++20` for the dependency builds (`options.cmake`). Our current `set(CMAKE_CXX_STANDARD 17)`
   comment cites the USD 26.05 build; the Blender variant needs its own value.
3. **TBB include path.** USD's public headers reach into TBB, so add `tbb/include` from the same lib
   checkout rather than the system oneTBB. (The tracer being header-only and TBB-free still helps
   here — see the existing "never link the `tracer` target" note.)
4. Keep `build-hydra/` (our own USD 26.05, for `usdrecord`/`usdview` work) and add
   `build-hydra-blender/` beside it. `env.sh` will want a parallel `BLENDER_USD_ROOT`.

## 6. The add-on itself

Pure Python, and small. Blender's in-tree `hydra_storm` add-on is ~50 lines total and is the
template; the C++ side of scene export is Blender's own Hydra scene delegate, and
`HydraRenderEngine` drives update/render/draw, so progressive sampling should reach the viewport for
free.

```python
import bpy

class WeekendHydraRenderEngine(bpy.types.HydraRenderEngine):
    bl_idname = "WEEKEND"
    bl_label = "Weekend"
    bl_delegate_id = "HdWeekendRendererPlugin"   # matches hydra/plugInfo.json
    bl_use_gpu_context = False                   # CPU tracer; Storm sets True
    bl_use_preview = True

    @classmethod
    def register(cls):
        bpy.utils.expose_bundled_modules()       # makes pxr importable under bpy-as-module
        import pxr.Plug
        pxr.Plug.Registry().RegisterPlugins(['/path/to/install/plugin/usd'])

    def get_render_settings(self, engine_type):
        return {...}                             # our existing HdRenderSettingsMap keys

    def update_render_passes(self, scene, render_layer):
        if render_layer.use_pass_combined:
            self.register_pass(scene, render_layer, 'Combined', 4, 'RGBA', 'COLOR')
```

`bl_delegate_id` is the `TfType` name registered in `rendererPlugin.cpp`
(`HdRendererPluginRegistry::Define<HdWeekendRendererPlugin>()`), i.e. `HdWeekendRendererPlugin` —
*not* the `hdWeekend` plugin name or the `Weekend` display name. The scene-index plugins keep
working via their existing `"loadWithRenderer": "Weekend"`, which matches on display name.

Storm passes final-render AOV routing through the same settings dict
(`'aovToken:Combined': "color"`, `'aovToken:Depth': "depth"`), worth copying.

## 7. Structural consequence to plan around

The delegate binary is bound to one Blender minor version's USD namespace. Distributing the add-on
therefore means one `.so` per Blender minor version per OS — the namespace changes on every USD bump
(`pxrBlender_v26_03` → `pxrBlender_v26_05` → …), so a 5.3 release invalidates a 5.2 binary even
though the source is unchanged. For our own use: pin 5.2 and rebuild deliberately.
