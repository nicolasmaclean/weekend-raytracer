# Blender add-on — step-by-step

**Roadmap item:** `0.3.0 - hydra delegate` → `blender plugin` (see [[Roadmap]])
**Context:** [[compile-against-blenders-usd-binary]] · [[ci]] · [[hydra]] · [[hydra-spec]] §15, §18 · [[hdtiny-stub-delegate]]
**Every environment and API fact below was verified on 2026-09-07**, against: the local OpenUSD
checkout at tags `v25.02` / `v26.05`; the `blender-v4.5-release` branches of `blender.git` and
`lib-linux_x64.git`; PyPI; and this machine. Sources are cited inline so each claim can be
re-checked rather than re-guessed.

---

## Decisions taken

Settled before planning, so they are not re-opened mid-task:

| Decision | Choice | Consequence |
|---|---|---|
| Blender version | **4.5.13 LTS** (USD 25.02, Python 3.11) | Needs one `#if PXR_VERSION` shim (Stage A2). Buys C++17 — see below |
| Deliverable | **Both, in stages** — build tree first (Stage B), zip last (Stage G) | The dev loop and the shipped artifact are each designed, not one falling out of the other |
| Add-on scope | Viewport render · settings panel · AOV wiring | Lights/material fidelity deferred to 0.4.0 `materialX` |
| Testing | **`bpy` wheel smoke test now** (Stage F) | Reused verbatim by the 0.4.0 CI item |

The multi-version / multi-platform fan-out stays where the roadmap already put it: `0.4.0 → ci to
auto-compile for multiple blender versions`, designed in [[ci]]. This task builds **one** `.so` for
**one** Blender on **one** platform, and does it in a way that CI can parameterise.

---

## What this task is

A second build configuration and about 200 lines of Python.

There is no new rendering code. `hydra/` already contains a delegate that `usdview` and `usdrecord`
drive on real assets (kitchen_set, chess set, all AOVs, instancing). Blender's Hydra integration is
a plain `HdRendererPluginRegistry` client, so from the delegate's point of view Blender is just
another host. Two things separate "works in usdview" from "works in Blender":

1. **An ABI boundary.** Blender renames USD's internal C++ namespace, so the delegate needs a
   second build against Blender's headers and monolithic lib. This is the whole subject of
   [[compile-against-blenders-usd-binary]]; that note's conclusions are taken as given here and
   re-verified for 4.5 in the facts table.
2. **A host that drives Hydra differently from usdview** — and specifically, drives the *CPU*
   path, which is less travelled than the GPU path Storm uses. Three of its behaviours are
   load-bearing for us and none of them are documented. See the next section.

## The one thing to understand before starting

**Read `source/blender/render/hydra/` before writing the add-on, not after.** It is ~1,100 lines and
it is the actual specification for what the Python has to do. The add-on is small precisely because
this C++ does the work — but it also means the add-on's correctness is decided by details that no
Python-side documentation mentions.

Four findings from reading it at `blender-v4.5-release`, each of which would otherwise cost a day:

### 1. External delegates are first-class, and nothing special is needed

`engine.cc:63` — `registry.CreateRenderDelegate(pxr::TfToken(render_delegate_name_))`. A plain
registry lookup on the string from `bl_delegate_id`. No allowlist, no built-in-renderer check. If
`pxr.Plug` can find our `plugInfo.json`, Blender can create our delegate. This is the single
biggest risk in the task and it is already retired.

Corollary from the same constructor: `light_tasks_delegate_` is created **only** when
`render_delegate_name_ == "HdStormRendererPlugin"` (`engine.cc:80`). We get no light tasks and no
skydome task. Since the delegate supports no lights, that is the correct outcome, not a loss.

### 2. Final render binds no AOVs at all unless we ask for them — and the Storm safety net does not cover us

`final_engine.cc:56-66`:

```cpp
LISTBASE_FOREACH (RenderPass *, rpass, &rlayer->passes) {
  pxr::TfToken *aov_token = aov_tokens_.lookup_ptr(rpass->name);
  if (!aov_token) { CLOG_WARN(..., "Couldn't find AOV token for render pass: %s", rpass->name); continue; }
  render_task_delegate_->add_aov(*aov_token);
}
if (bl_engine_->type->flag & RE_USE_GPU_CONTEXT) {
  /* For GPU context engine color and depth AOVs has to be added anyway */
  render_task_delegate_->add_aov(pxr::HdAovTokens->color);
  render_task_delegate_->add_aov(pxr::HdAovTokens->depth);
}
```

`aov_tokens_` is populated from exactly one source: render settings whose key starts with
`aovToken:` (`final_engine.cc:104-110`). And the fallback that adds colour and depth regardless is
gated on `RE_USE_GPU_CONTEXT` — which we do **not** set, because we are a CPU tracer.

So: **`get_render_settings('FINAL')` returning `{'aovToken:Combined': 'color', ...}` is mandatory,
not cosmetic.** Omit it and F12 produces an empty result with one `CLOG_WARN` that is invisible
unless `--log "hydra.render"` is on. Storm never hits this because it takes the GPU branch.

### 3. Blender cannot accept our three ID AOVs, at all

`render_task_delegate.cc:119-127`:

```cpp
if (!ELEM(pxr::HdGetComponentFormat(aov_desc.format), pxr::HdFormatFloat32, pxr::HdFormatFloat16)) {
  CLOG_WARN(..., "Unsupported data format %s for AOV %s", ...); return;
}
```

`read_aov` confirms it — it memcpys float32 or widens float16, and `BLI_assert_unreachable()` on
anything else (`render_task_delegate.cc:146-172`). Against our AOV table
(`tracer/render_buffer.h:358-372`):

| AOV | Our format | Blender |
|---|---|---|
| `color` | `float32_vec4` | ✅ accepted, 4 channels |
| `depth`, `cameraDepth` | `float32` | ✅ accepted, 1 channel |
| `normal`, `Neye` | `float32_vec3` | ✅ accepted, 3 channels |
| `primId`, `instanceId`, `elementId` | `int32` | ❌ refused before allocation |

The ID AOVs are a Blender limitation, not a gap in the delegate — they keep working in `usdview`.
Do not spend time on them here, and do not "fix" our table to float: the int32 formats are
hdEmbree's and usdview's contract.

### 4. `percentDone` is read as a raw string key, cast unchecked to `double`

`engine.cc:129-137`:

```cpp
pxr::VtDictionary render_stats = render_delegate_->GetRenderStats();
auto it = render_stats.find("percentDone");
if (it == render_stats.end()) return 0.0f;
return float(it->second.UncheckedGet<double>());
```

Two consequences. First, `GetRenderStats()` currently publishes only
`HdPerfTokens->numCompletedSamples` (`hydra/renderDelegate.cpp:100-105`), so Blender's progress bar
and the viewport's elapsed-time readout sit at zero forever — and worse, the viewport resets its
timer every draw, because `renderer_percent_done() == 0.0f` is its "render just started" test
(`viewport_engine.cc:270-272`).

Second, `UncheckedGet<double>` is unchecked. There is **no** `percentDone` in `HdPerfTokens` at
25.02 (verified: the only match in `hd/tokens.h` is `numCompletedSamples`), so this is a bare
string key, and publishing it as anything but a `double` is undefined behaviour inside Blender.
Stage C3 is one line, and it must be `double`.

---

## Target — verified facts

| Fact | Value | Source, verified 2026-09-07 |
|---|---|---|
| Blender | **4.5.13** LTS | latest `4.5.x` on PyPI `bpy`; `download.blender.org` HTTP 200 |
| Tarball | `blender-4.5.13-linux-x64.tar.xz`, 378,033,952 B | `content-length`, HTTP 200 |
| USD | **25.02** | `versions.cmake` @ `blender-v4.5-release`: `set(USD_VERSION 25.02)` |
| `PXR_VERSION` | **2502** | shipped `usd/include/pxr/pxr.h` |
| Internal namespace | **`pxrBlender_v25_02__pxrReserved__`** | shipped `pxr.h` (ours: `pxrInternal_v0_26_5__pxrReserved__`) |
| Python | **3.11.15** | `versions.cmake`: `set(PYTHON_VERSION 3.11.15)` |
| C++ standard (deps) | **C++17** | `options.cmake`: `-DCMAKE_CXX_STANDARD=17`, Linux `-std=c++17 -fPIC` |
| USD build shape | `BUILD_SHARED_LIBS=ON` + `PXR_BUILD_MONOLITHIC=ON` | `usd.cmake` @ 4.5 — one `libusd_ms.so`, one `TfType` registry |
| SDK branch | `blender-v4.5-release` @ `1df155d9de29` | `git ls-remote --heads lib-linux_x64.git` |
| `libusd_ms.so` | 57,342,624 B (LFS `sha256:158a1b99…`) | LFS pointer on that branch |
| SDK layout | `usd/{include,lib,plugin}` — **no `cmake/`** | Gitea contents API |
| TBB | `v2021.13.0`; `tbb/{include,lib}`, `libtbb.so.12.13`, **plus `tbb/lib/cmake`** | `versions.cmake`; contents API |
| `pxr` Python bindings | `python/lib/python3.11/site-packages/pxr` — 41 modules, `Plug` present | `usd.cmake:230-233` `harvest_rpath_python`; contents API |
| `bpy` wheel | `bpy==4.5.13`, **cp311 only**, `manylinux_2_28_x86_64`, 372.8 MB | PyPI JSON |
| glibc floor | **2.28** | implied by the `manylinux_2_28` wheel tag; matches [[ci]] §4 for 5.x |
| `--split-platforms` | supported | `blender_ext.py:3174-3177` @ 4.5 |
| `expose_bundled_modules()` | present | `scripts/modules/bpy/utils/__init__.py` @ 4.5, in `__all__` |
| `HydraRenderEngine` | present, with `view_update` / `view_draw` | `scripts/modules/bpy_types.py:1396-1457` @ 4.5 |

### The API drift, measured rather than feared

Every `pxr/` header `hydra/` includes (49 of them), diffed `v25.02` → `v26.05` in the local
checkout. **Unchanged**, and therefore free:

`pxr.h` · `hd/aov.h` · `hd/enums.h` · `hd/instancer.h` · `hd/mesh.h` · `hd/meshTopology.h` ·
`hd/renderBuffer.h` · `hd/renderPass.h` · `hd/renderThread.h` · `hd/resourceRegistry.h` ·
`hd/retainedDataSource.h` · **`hd/types.h`** · `hdsi/implicitSurfaceSceneIndex.h` ·
`hdsi/extComputationPrimvarPruningSceneIndex.h` · `gf/rect2i.h` · `tf/{token,diagnostic,envSetting,hashmap}.h`

`hd/types.h` being untouched is the important one: the `buffer_format` ↔ `HdFormat` value-for-value
correspondence that [[hydra]] Step A1 static-asserts holds identically at 25.02. So does the AOV
descriptor table, the render buffer interface, and both scene-index filters.

Of the 32 headers that *did* change, each was checked against what we actually call:

| Header | Change | Affects us? |
|---|---|---|
| **`hd/rendererPlugin.h`** | `IsSupported` pure virtual differs — see below | **Yes. The only one.** |
| `hd/renderDelegate.h` | +5 virtuals, all with base impls (`SetArbitraryValue`, `GetShadingSystems`, `RequiresStormTasks`, …) | No — purely additive |
| `hd/sceneIndexPlugin.h` | `+IsEnabled` / `+_IsEnabled`, both defaulted; `_AppendSceneIndex` unchanged | No |
| `hd/sceneIndexPluginRegistry.h` | reorganised; our `RegisterSceneIndexForRenderer(name, token, args, phase, order)` overload present at 25.02 | No |
| `hd/meshUtil.h` | `ComputeTriangleIndices(VtVec3iArray*, VtIntArray*, VtIntArray*)` — byte-identical | No |
| `hd/tokens.h` | additive; `convergedSamplesPerPixel`, `threadLimit`, `numCompletedSamples`, and all six implicit-surface prim tokens present at 25.02 | No |
| `hd/camera.h` | +exposure controls | No |
| `gf/vec*.h`, `gf/matrix4*.h`, `gf/quat*.h` | comment-only (3 lines each) | No |
| `vt/*`, `tf/singleton.h`, `work/*`, `sdf/path.h`, `hd/{changeTracker,renderIndex,renderPassState,sceneDelegate}.h` | additive / internal | No |

**One incompatibility in the whole delegate.** At `v25.02`:

```cpp
virtual bool IsSupported(bool gpuEnabled = true) const = 0;   // HdRendererCreateArgs does not exist
```

At `v26.03`+ that form survives, deprecated and non-pure, and
`IsSupported(HdRendererCreateArgs const &, std::string *)` becomes the pure virtual — which is what
`hydra/rendererPlugin.cpp:36` overrides today. Hence Stage A2.

Worth noting for later: `HdRendererPluginRegistry::CreateRenderDelegate(pluginId, settingsMap)` at
25.02 does **not** consult `IsSupported` (only the pick-a-default path at
`rendererPluginRegistry.cpp:53` does). So the shim exists to compile, not to gate.

## What 4.5 costs, and what it buys

The cost is the shim above — one `#if`, in one file. Against that:

- **C++17 stays.** Blender 4.5 builds every dependency with `-DCMAKE_CXX_STANDARD=17`, so
  `hydra/CMakeLists.txt`'s existing `set(CMAKE_CXX_STANDARD 17)` is correct for *both* flavours.
  [[compile-against-blenders-usd-binary]] §5.2 flagged C++20 as a required change for a 5.1+
  target; choosing 4.5 deletes that work item.
- **The shim gets built and exercised now**, on the version with the longest support window,
  rather than being designed speculatively when CI first needs it ([[ci]] §6).
- **LTS.** 4.5 is the version with the largest installed base and the longest remaining life, so
  the first published artifact reaches the most users.

## This machine — verified 2026-09-07, and two blockers

| | |
|---|---|
| glibc | **2.39** (Ubuntu 24.04) |
| g++ | 13.3.0 — newer than Blender's Rocky 8 toolchain, which is [[ci]] §4's safe direction |
| cmake | 3.28.3 |
| python3 | **3.12.3 — and no 3.11 anywhere** |
| `uv` | present at `~/.local/bin/uv` |
| `git-lfs` | **not installed** |
| free space on `~` | 23 GB |

Two things must be fixed before Stage 0 can run, and one is a standing limitation:

- **`git-lfs` is missing.** The SDK harvest is an LFS sparse checkout. Without it, `git checkout`
  silently leaves 133-byte pointer files where `libusd_ms.so` should be, and the first link error
  is deeply unhelpful. Step 0.1.
- **No Python 3.11.** The `bpy` wheel is cp311-only, so Stage F needs an interpreter this box does
  not have. `uv` solves it without touching the system Python. Step F1.
- **glibc 2.39 > Blender's 2.28 floor.** A `.so` built here loads on this box and any
  glibc ≥ 2.39 system, and *not* on much of what Blender itself supports. That is fine for this
  task and fatal for distribution. Stage G's zip is therefore explicitly a **personal-install
  artifact**; producing a publishable one is [[ci]] §4's manylinux container, in 0.4.0. Say so in
  the commit message so nobody later mistakes it for a release.

Budget roughly 4 GB: SDK checkout ~1 GB, Blender tarball + extraction ~1.4 GB, `bpy` venv ~1.5 GB.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| Multi-version / multi-platform matrix, manylinux container, tracks | 0.4.0 `ci to auto-compile…`, designed in [[ci]] |
| Lights — Blender's exporter emits them, we ignore them | 0.4.0 |
| Materials beyond `displayColor`; `bl_use_materialx` | 0.4.0 `materialX or something…` |
| Textures | 0.4.0 `texture mapping` |
| `primId` / `instanceId` / `elementId` passes | never, in Blender — §3 above |
| Publishing to extensions.blender.org | 0.4.0 — unblocked by the GPL-3.0-or-later relicense, but still needs a glibc-2.28 zip ([[ci]] §4) |
| Windows / macOS | 0.4.0 |

---

## New files

| File | Purpose |
|---|---|
| `hydra/compat.h` | The USD-version ledger. One documented feature macro per divergence |
| `blender/weekend_raytracer/__init__.py` | Extension entry point |
| `blender/weekend_raytracer/engine.py` | `HydraRenderEngine` subclass |
| `blender/weekend_raytracer/properties.py` | `PropertyGroup` + render-properties panels |
| `blender/weekend_raytracer/blender_manifest.toml.in` | Templated from the target row ([[ci]] §7) |
| `blender/smoke_test.py` | Runs under `bpy`-as-module; reused by CI ([[ci]] §5) |
| `blender/build_zip.sh` | Stages the tree, renders the manifest, calls `extension build` |
| `blender/blender.sh` | Launches Blender with the vanilla-USD env stripped out; `$BLENDER` points here |

Modified: `hydra/CMakeLists.txt` (flavour switch), `hydra/rendererPlugin.{h,cpp}` (the shim),
`hydra/renderDelegate.cpp` (`percentDone`), `env.sh` (Blender paths).

`blender/` is deliberately a sibling of `hydra/`, not inside it: the Python is
version-and-platform-independent and the `.so` is not — [[ci]] §1 is built on that split.

---

# Stage 0 — Get the SDK and the app

## Step 0.1 — Install `git-lfs` first

```bash
sudo apt install git-lfs && git lfs install
git lfs version   # must print, or Step 0.2 produces pointer files
```

## Step 0.2 — Harvest the 4.5 SDK

Per [[compile-against-blenders-usd-binary]] §4, with `tbb` added because it is not optional here
(see Stage A1) and `python/include` added because Blender's USD is built with
`PXR_PYTHON_SUPPORT_ENABLED` — see the 9/7 3:05pm note under Stage A1:

```bash
mkdir -p ~/opt && cd ~/opt
git clone --filter=blob:none --no-checkout \
  https://projects.blender.org/blender/lib-linux_x64.git -b blender-v4.5-release
cd lib-linux_x64
git sparse-checkout set usd tbb python/include
git checkout
```

Verify — all three, before going further:

```bash
# 1. real file, not a 133-byte LFS pointer
stat -c%s usd/lib/libusd_ms.so          # want 57342624

# 2. the namespace this build is pinned to
grep PXR_INTERNAL_NS usd/include/pxr/pxr.h   # want pxrBlender_v25_02__pxrReserved__

# 3. the soname our plugin's DT_NEEDED will have to match at runtime
readelf -d usd/lib/libusd_ms.so | grep SONAME
readelf -d tbb/lib/libtbb.so.12.13 | grep SONAME
```

Check 3 is the one nobody thinks to do. Our `.so` is `dlopen`ed into a Blender process that has
already loaded these libraries; the loader satisfies our `DT_NEEDED` entries by **soname** against
what is already mapped. If `libusd_ms.so` carries a `SONAME`, no rpath is needed and the packaged
add-on is a plain file copy. If it does not, record what `readelf` actually said and set
`-Wl,-rpath,$ORIGIN/...` pointing into the Blender install — decide it here, on evidence, not after
a `dlopen` failure in Stage B.

> 9/7 1:55pm
> check 1 and 2 are verifed
> check 3 give the below:
> nick@unick:~/opt/lib-linux_x64$ readelf -d usd/lib/libusd_ms.so | grep SONAME
> 0x000000000000000e (SONAME)             Library soname: [libusd_ms.so]
> nick@unick:~/opt/lib-linux_x64$ readelf -d tbb/lib/libtbb.so.12.13 | grep SONAME
> 0x000000000000000e (SONAME)             Library soname: [libtbb.so.12]

## Step 0.3 — Install Blender 4.5.13

Official tarball, not snap or apt. Snap confinement makes an add-on that loads a `.so` from an
arbitrary path needlessly painful, and snap GUI apps on this box have their own failure mode
(a stale `~/.cache/fontconfig` causes a silent segfault on launch). The tarball is also the only
form that pins an exact patch release, which the whole ABI story depends on.

```bash
cd ~/opt
curl -O https://download.blender.org/release/Blender4.5/blender-4.5.13-linux-x64.tar.xz
tar xf blender-4.5.13-linux-x64.tar.xz
~/opt/blender-4.5.13-linux-x64/blender --version   # want: Blender 4.5.13 LTS
```

Confirm the two things the add-on depends on at runtime:

```bash
B=~/opt/blender-4.5.13-linux-x64
$B/blender --background --python-expr 'import pxr.Plug, pxr.Tf; print("pxr OK", pxr.Plug.__file__)'
ls $B/lib/libusd_ms.so $B/lib/libtbb.so.12
```

> nick@unick:~/opt$ B=~/opt/blender-4.5.13-linux-x64
> nick@unick:~/opt$ $B/blender --background --python-expr 'import pxr.Plug, pxr.Tf; print("pxr OK", pxr.Plug.__file__)'
> Blender 4.5.13 LTS (hash daeeeca98fb0 built 2026-08-25 01:31:19)
> pxr OK /home/nick/opt/blender-4.5.13-linux-x64/4.5/python/lib/python3.11/site-packages/pxr/Plug/__init__.py
>
> Blender quit
> nick@unick:~/opt$ ls $B/lib/libusd_ms.so $B/lib/libtbb.so.12
> /home/nick/opt/blender-4.5.13-linux-x64/lib/libtbb.so.12
> /home/nick/opt/blender-4.5.13-linux-x64/lib/libusd_ms.so

## Step 0.4 — Extend `env.sh`

`env.sh` currently sets up exactly one USD. Add the Blender flavour beside it without disturbing
the vanilla vars — `usdview`/`usdrecord` work must keep working unchanged throughout this task.

```bash
# --- Blender 4.5 flavour (docs/plans/blender-addon.md) -----------------
export BLENDER_LIB_DIR=$HOME/opt/lib-linux_x64          # sparse checkout, Step 0.2
export BLENDER_BIN=$HOME/opt/blender-4.5.13-linux-x64/blender
export BLENDER=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/blender/blender.sh   # see below
export BLENDER_USD_NAMESPACE=pxrBlender_v25_02          # asserted by cmake, [[ci]] §2
export HDW_BLENDER_INSTALL=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-hydra-blender/install
```

Do **not** put the Blender USD on `LD_LIBRARY_PATH` or `PYTHONPATH`. Two USD builds with different
internal namespaces on one path is the exact confusion Blender's namespace rename exists to
prevent; the plugin finds its libraries through Blender's own process, and nothing else needs them.

**And the converse, which cost a Gate 0 failure — 9/7 2:20pm.** The rule is symmetric, and the
half that bites is the other one: `env.sh` already puts the *vanilla* USD on `LD_LIBRARY_PATH`,
and Blender finds its own libraries through `RUNPATH` (`$ORIGIN`), which the loader consults
**after** `LD_LIBRARY_PATH`. So a sourced shell hands Blender our libraries instead of its own.
They collide by soname — both ship `libMaterialXCore.so.1`, Blender's is 1.39.2 in namespace
`MaterialX_v1_39_2`, ours is 1.39.4 in `MaterialX_v1_39_4` — and Blender dies before `main()`:

```
blender: symbol lookup error: lib/libusd_ms.so: undefined symbol: _ZTIN17MaterialX_v1_39_27ElementE
```

Step 0.3's checks passed only because they were run in a shell that had not sourced `env.sh`.
Every later stage runs `$BLENDER` *from* a sourced shell (B3, Gates B/C/D/E/G), so this had to be
fixed here rather than rediscovered at Gate B. Hence `blender/blender.sh`: `$BLENDER` is that
wrapper, not the binary, and it `exec`s `$BLENDER_BIN` under
`env -u LD_LIBRARY_PATH -u PYTHONPATH -u PXR_PLUGINPATH_NAME`.

`PXR_PLUGINPATH_NAME` is stripped for the same reason one level up: it points at the vanilla
`hdWeekend` build, linked against the wrong USD namespace, and Blender would try to load it.
The Blender-flavour plugin is found via `HDW_PLUGIN_DIR` (Step B3), which passes through untouched.

## GATE 0 — the SDK is real

`libusd_ms.so` is 57,342,624 bytes, `pxr.h` says `pxrBlender_v25_02`, `blender --version` says
4.5.13, `import pxr.Plug` works inside Blender, and you have written down what `readelf -d` said
about both sonames.

Run the last two **from a shell that has sourced `env.sh`**, not a clean one — that is the
environment every later stage uses, and it is where the MaterialX soname collision above shows up.

> 9/7 2:20pm — all green, from a sourced shell, via the `blender/blender.sh` wrapper:
> size 57342624 · `pxrBlender_v25_02__pxrReserved__` · sonames `libusd_ms.so` and `libtbb.so.12` ·
> `Blender 4.5.13 LTS` · `pxr.Plug` resolves to Blender's own
> `4.5/python/lib/python3.11/site-packages/pxr/` · and `usdrecord` + the vanilla `pxr.Usd` still
> work unchanged in the same shell.

---

# Stage A — a second build config

## Step A1 — A flavour switch in `hydra/CMakeLists.txt`

Four changes, each forced by something verified above. `find_package(pxr)` cannot work — no
`pxrConfig.cmake` and no `cmake/` directory is harvested — so the monolithic lib becomes the single
link target.

```cmake
# "vanilla" -> our USD 26.05 at $USD_ROOT, installs to build-hydra/    (usdview, usdrecord)
# "blender" -> Blender 4.5's harvested USD 25.02, installs to build-hydra-blender/
set(HDW_USD_FLAVOR "vanilla" CACHE STRING "Which USD to build against: vanilla | blender")
set_property(CACHE HDW_USD_FLAVOR PROPERTY STRINGS vanilla blender)

# C++17 is correct for BOTH flavours: USD 26.05 sets it in cmake/defaults/CXXDefaults.cmake, and
# Blender 4.5 builds every dependency with -DCMAKE_CXX_STANDARD=17 (build_environment options.cmake).
set(CMAKE_CXX_STANDARD 17)

# ... add_library(hdWeekend SHARED ...) unchanged ...

if(HDW_USD_FLAVOR STREQUAL "vanilla")
    find_package(pxr REQUIRED PATHS $ENV{USD_ROOT} NO_DEFAULT_PATH)
    target_link_libraries(hdWeekend PRIVATE hd tf hdsi)
else()
    set(BLENDER_LIB_DIR "$ENV{BLENDER_LIB_DIR}" CACHE PATH "lib-linux_x64 sparse checkout")
    if(NOT EXISTS "${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h")
        message(FATAL_ERROR "BLENDER_LIB_DIR is not a lib-<platform> checkout: ${BLENDER_LIB_DIR}")
    endif()

    # Fail here, loudly, rather than as a link error or a runtime TfType miss ([[ci]] §2). The
    # expected value is a variable so CI can drive it from a targets.toml row.
    set(HDW_BLENDER_USD_NAMESPACE "$ENV{BLENDER_USD_NAMESPACE}" CACHE STRING "asserted against pxr.h")
    file(READ "${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h" _pxr_h)
    if(NOT _pxr_h MATCHES "PXR_INTERNAL_NS[ \t]+${HDW_BLENDER_USD_NAMESPACE}")
        message(FATAL_ERROR
            "namespace mismatch: expected ${HDW_BLENDER_USD_NAMESPACE} in ${BLENDER_LIB_DIR}/usd/include/pxr/pxr.h")
    endif()

    # USD's public headers reach into TBB - pxr/base/work/loops.h, which hydra/renderer.cpp
    # includes, includes <tbb/parallel_for.h> directly. Use the SDK's TBB headers and lib, never
    # the system oneTBB: at runtime this resolves to the libtbb.so.12 Blender already has loaded.
    target_include_directories(hdWeekend PRIVATE
        "${BLENDER_LIB_DIR}/usd/include"
        "${BLENDER_LIB_DIR}/tbb/include")
    target_link_libraries(hdWeekend PRIVATE
        "${BLENDER_LIB_DIR}/usd/lib/libusd_ms.so"
        "${BLENDER_LIB_DIR}/tbb/lib/libtbb.so")
endif()
```

Leave `target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)`, the
`PREFIX ""` property, and both `install()` rules alone. The install layout must stay
byte-for-byte what it is, because `plugInfo.json`'s `"LibraryPath": "../hdWeekend.so"` with
`"Root": ".."` resolves against it — the `.so` is a *sibling* of the `hdWeekend/` directory. Stage G
reproduces that layout inside the zip rather than inventing a new one.

Configure and build:

```bash
source env.sh
cmake -S hydra -B build-hydra-blender -DHDW_USD_FLAVOR=blender \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build-hydra-blender/install
cmake --build build-hydra-blender -j
```

Expect it to fail, once, in `rendererPlugin.cpp`. That is Step A2 — and it is the shim doing its
job as a triage tool ([[ci]] §6).

> **9/7 3:05pm — it failed twice, and the first one was not the shim.** Before
> `rendererPlugin.cpp` was reached, *every* TU died on:
>
> ```
> usd/include/pxr/external/boost/python/detail/wrap_python.hpp:62:11:
>   fatal error: pyconfig.h: No such file or directory
> ```
>
> Blender's `libusd_ms.so` is built with `PXR_PYTHON_SUPPORT_ENABLED`, so `pxr/pxr.h` leaves it
> defined and `pxr/base/tf/pySafePython.h` includes `<Python.h>`. That header is pulled in from
> `hd/dataSource.h`, `hd/perfLog.h` and `trace/collector.h` — i.e. from essentially every hydra
> header we touch — so this is unavoidable, not incidental to one file. Our vanilla USD is built
> without Python support, which is why the flavour switch is the first place it shows up.
>
> Three things were verified rather than assumed:
>
> - Blender ships **no** headers with its bundled interpreter — there is no
>   `blender-4.5.13-linux-x64/4.5/python/include` at all. The runtime interpreter is not a source
>   of headers.
> - `lib-linux_x64` has them, at `python/include/python3.11/` — 189 files, **1.4 MB** checked out,
>   `PY_VERSION "3.11.15"`, and `pyconfig.h` is a real 53,281-byte file rather than an LFS pointer.
>   These are the exact headers `libusd_ms.so` was compiled against, so this is the right source
>   and not merely a convenient one. Hence the change to Step 0.2's `sparse-checkout set`.
> - We add the include path only; **`libpython` is never linked.** Nothing in `hydra/` calls the C
>   API — the headers are needed purely so USD's own declarations parse — and inside Blender the
>   interpreter is already in the process. `readelf -d` at Gate A is what confirms no
>   `libpython3.11.so` `NEEDED` entry crept in.
>
> A `Python.h`-exists guard was added beside the `pxr.h` one, with the `sparse-checkout add`
> command in the error text, so a fresh checkout fails at configure time with the fix rather than
> in a wall of preprocessor errors.
>
> With the include path added, the build failed a second time — in `rendererPlugin.cpp`, exactly as
> written above: `HdRendererCreateArgs has not been declared`, and 25.02's
> `virtual bool IsSupported(bool gpuEnabled = true) const = 0` left pure. Every other TU compiled.
> That is Step A2.
>
> Also confirmed at A1, since the flavour switch moved `find_package(pxr)` inside a branch: the
> **vanilla** build still configures, compiles and links with no `-D` flags at all, `HDW_USD_FLAVOR`
> defaulting to `vanilla`. And both blender-branch guards were tested by deliberately breaking
> them — a wrong `-DHDW_BLENDER_USD_NAMESPACE` and a bogus `-DBLENDER_LIB_DIR` each stop configure
> with the intended message.

## Step A2 — `hydra/compat.h`, the one real shim

A ledger, not a grab-bag. One macro per divergence, each commented with the USD version that
introduced it, so dropping a Blender version later is "delete every branch below N" and the
carrying cost of each supported version stays visible ([[ci]] §6).

```cpp
#pragma once

#include "pxr/pxr.h"

// USD version compatibility for hdWeekend.
//
// PXR_VERSION is major*10000 + minor*100 + patch: 2502 for Blender 4.5's USD 25.02,
// 2605 for our own build. Every macro below records the version that introduced the
// change, so this header doubles as the support-window ledger.

// --- HdRendererPlugin::IsSupported ------------------------------------ USD 26.03
//
// <= 25.08: `virtual bool IsSupported(bool gpuEnabled = true) const = 0;` is the pure
//           virtual, and HdRendererCreateArgs does not exist.
//  > 26.03: IsSupported(HdRendererCreateArgs const &, std::string *) is the pure
//           virtual; the bool form survives as a deprecated, non-pure overload.
#if PXR_VERSION >= 2603
#  define HDW_HAS_RENDERER_CREATE_ARGS 1
#else
#  define HDW_HAS_RENDERER_CREATE_ARGS 0
#endif
```

`hydra/rendererPlugin.h` — include `compat.h`, then:

```cpp
#if HDW_HAS_RENDERER_CREATE_ARGS
  bool IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                   std::string *reasonWhyNot = nullptr) const override;
#else
  bool IsSupported(bool gpuEnabled = true) const override;
#endif
```

`hydra/rendererPlugin.cpp` — same guard around the definition; both bodies keep the existing
comment and `return true`. The `bool` form must **not** mention `HdRendererCreateArgs`, since the
type does not exist at 25.02. And it must **not** repeat the default argument:

```cpp
bool HdWeekendRendererPlugin::IsSupported(bool /* gpuEnabled */) const   // NOT `bool gpuEnabled = true`
```

C++ allows a default argument only on the declaration. Copying the header's signature verbatim into
the `.cpp` is the obvious move and it is an error — `-fpermissive` territory, and the diagnostic
points at the header rather than the definition. See the 9/8 note at GATE A.

## GATE A — it links, and against the right USD

```bash
cmake --build build-hydra-blender -j && cmake --install build-hydra-blender

# right namespace baked into the symbols
nm -DC build-hydra-blender/install/plugin/usd/hdWeekend.so | grep -c pxrBlender_v25_02   # > 0
nm -DC build-hydra-blender/install/plugin/usd/hdWeekend.so | grep -c pxrInternal_v0_26   # want 0

# nothing unexpected wanted at load time
readelf -d build-hydra-blender/install/plugin/usd/hdWeekend.so | grep NEEDED
readelf -d build-hydra-blender/install/plugin/usd/hdWeekend.so | grep -i python || echo "no libpython - correct"

# Resolve the way the loader will, i.e. against Blender's own libraries. A bare `ldd -r` reports
# libusd_ms.so and libtbb.so.12 as "not found" and that is CORRECT: there is no RUNPATH, because
# Step 0.2 established both carry a SONAME and are already mapped in the Blender process.
LD_LIBRARY_PATH=$HOME/opt/blender-4.5.13-linux-x64/lib \
  ldd -r build-hydra-blender/install/plugin/usd/hdWeekend.so 2>&1 | grep -i "not found" || echo "all found"

# the vanilla build still works, unchanged
cmake --build build-hydra -j && cmake --install build-hydra
```

`NEEDED` should list `libusd_ms.so`, `libtbb.so.12`, libstdc++/libm/libc, and nothing else — in
particular no second TBB and nothing from `~/opt/usd_src_build`. The last command is not optional:
both flavours must build from one source tree for the rest of the roadmap to stay pleasant.

> **9/8 10:17am — green, after three defects in the A2 edit and two corrections to this page.**
>
> The shim as first written broke in ways the plan did not anticipate, all three in the mechanical
> part rather than the interesting part:
>
> 1. The `#if/#else/#endif` block was pasted **over** `void DeleteRenderDelegate(...) override;`,
>    deleting it and orphaning its doc comment. The class stayed abstract, so
>    `HfPluginRegistry::_CreatePlugin`'s `new T` failed — and the diagnostic for that
>    (`invalid new-expression of abstract class type`) points at USD's header, not at ours.
> 2. The original unguarded `IsSupported(HdRendererCreateArgs const &, std::string *)` declaration
>    was left in place *below* the new guard, so 25.02 saw both forms.
> 3. The definition repeated the default argument. Hence the addition to Step A2 above.
>
> Results with those fixed:
>
> ```
> blender build ......... exit 0, links
> install ............... plugin/usd/hdWeekend.so + hdWeekend/resources/plugInfo.json
> pxrBlender_v25_02 ..... 612 symbols        pxrInternal_v0_26 ..... 0
> NEEDED ................ libusd_ms.so, libtbb.so.12, libstdc++, libm, libgcc_s, libc
> RUNPATH ............... none          libpython NEEDED ...... none
> vanilla ............... exit 0, installs, 717 pxrInternal_v0_26 / 0 pxrBlender
> ```
>
> The shim demonstrably picks a different overload per flavour, which is the thing actually worth
> asserting and is stronger than "it compiled":
>
> ```
> vanilla: HdWeekendRendererPlugin::IsSupported(HdRendererCreateArgs const&, std::string*) const
> blender: HdWeekendRendererPlugin::IsSupported(bool) const
> ```
>
> **Correction 1 — `ldd -r` could never have printed "all resolvable".** Standalone it reports
> `libusd_ms.so` and `libtbb.so.12` not found, which is the *correct* outcome and follows directly
> from Step 0.2: both carry a SONAME, so no RUNPATH is emitted and the loader satisfies them
> against what Blender has already mapped. The check above now resolves against Blender's `lib/`
> instead, where both are found. The original form would have been read as a failure at exactly the
> moment the build was right.
>
> **Correction 2 — `hdWeekend.so` does reference the Python C API**, five symbols:
> `_Py_NoneStruct`, `_Py_Dealloc`, `PyLong_FromLong`, `PyFloat_FromDouble`, `PyBool_FromLong`.
> Step A1's third bullet says "nothing in `hydra/` calls the C API"; that is true of our code but
> not of the header-inlined USD conversions it instantiates, and the distinction only shows up at
> the symbol level. It is benign, and for a reason worth recording rather than re-deriving:
> `libusd_ms.so` itself carries ~173 undefined `Py*` symbols by the same mechanism, and the
> `blender` executable exports all five of ours from its statically-linked interpreter (verified
> with `nm -D --defined-only`). Under `bpy`-as-module (Stage F) they resolve against
> `libpython3.11.so` instead. So the load-bearing assertion is not "no undefined Py symbols" — it
> is **no `libpython` in `NEEDED`**, which is what the amended command checks. If a
> `libpython3.11.so` `NEEDED` entry ever appears, *that* is the regression.

---

# Stage B — the add-on, from the build tree

## Step B1 — The extension skeleton

```
blender/weekend_raytracer/
  __init__.py
  engine.py
  properties.py
  blender_manifest.toml.in     # committed; Stage G and Step B3 both render from this
  blender_manifest.toml        # gitignored, rendered - Step B3 does not work without it
```

**The rendered manifest is not a Stage G concern only.** Blender 4.2+ treats a directory as an
extension *only* if it contains a `blender_manifest.toml`, and a directory without one is skipped
in silence — no console error, no entry in the add-on list, nothing to grep for. So the dev install
in Step B3 needs a real manifest in the source tree, not just the `.in` template. Rendering it is
one `sed` and it belongs to B3, not G; see the 9/8 note at GATE B.

`__init__.py`:

```python
from . import engine, properties


def register():
    properties.register()
    engine.register()


def unregister():
    engine.unregister()
    properties.unregister()
```

No `bl_info` — extensions carry their metadata in `blender_manifest.toml`.

## Step B2 — `engine.py`

Blender's in-tree `hydra_storm` add-on is the template (`scripts/addons_core/hydra_storm/` @ 4.5),
but every `bl_` value below differs from Storm's for a reason worth stating once:

```python
import os
from pathlib import Path

import bpy


def _plugin_dir() -> Path:
    """Directory holding hdWeekend/resources/plugInfo.json.

    Packaged, that is <addon>/plugin/usd - the same layout cmake installs, because
    plugInfo.json's LibraryPath ("../hdWeekend.so") resolves against it. HDW_PLUGIN_DIR
    overrides it, which is the whole dev loop: point at build-hydra-blender/install.
    """
    if override := os.environ.get("HDW_PLUGIN_DIR"):
        return Path(override)
    return Path(__file__).parent / "plugin" / "usd"


class WeekendHydraRenderEngine(bpy.types.HydraRenderEngine):
    bl_idname = "WEEKEND"
    bl_label = "Weekend"
    bl_info = "CPU path tracer, via a Hydra render delegate"

    # The TfType name registered by HdRendererPluginRegistry::Define<HdWeekendRendererPlugin>()
    # in hydra/rendererPlugin.cpp - NOT the "hdWeekend" plugin name and NOT the "Weekend"
    # display name. Blender passes this straight to registry.CreateRenderDelegate (engine.cc:63).
    bl_delegate_id = "HdWeekendRendererPlugin"

    # We are a CPU tracer. This is load-bearing in three places: no Hgi driver is created,
    # the CPU RenderTaskDelegate is used instead of GPURenderTaskDelegate, and the
    # "add color+depth anyway" fallback in final_engine.cc does not apply to us - which is
    # why get_render_settings MUST return the aovToken: keys. See the plan, "the one thing".
    bl_use_gpu_context = False

    # No materials yet, so preview thumbnails would be flat grey and would cost real time.
    # Flip to True with 0.4.0's material work.
    bl_use_preview = False
    bl_use_materialx = False

    @classmethod
    def register(cls):
        # Needed for bpy-as-module (Stage F); a harmless no-op inside the real app, where pxr
        # is already in site-packages. One code path for both.
        bpy.utils.expose_bundled_modules()

        import pxr.Plug

        plug_info = _plugin_dir() / "hdWeekend" / "resources" / "plugInfo.json"
        if not plug_info.exists():
            raise RuntimeError(f"hdWeekend plugInfo.json not found at {plug_info}")
        pxr.Plug.Registry().RegisterPlugins([str(plug_info)])


def register():
    bpy.utils.register_class(WeekendHydraRenderEngine)


def unregister():
    bpy.utils.unregister_class(WeekendHydraRenderEngine)
```

`RegisterPlugins` is given the `plugInfo.json` path explicitly rather than a directory to search.
Both work, but the explicit form turns a wrong path into the `RuntimeError` above instead of a
delegate that is simply, silently, not in the registry — which is the failure this whole file is
most likely to have.

## Step B3 — Install as a dev extension

Symlink the source tree into Blender's user extensions directory, and let `HDW_PLUGIN_DIR` point at
the build tree. Nothing is copied, so `git`-side edits are live:

```bash
# Render the dev manifest first - without it the symlink is invisible to Blender (Step B1).
sed -e "s|@BLENDER_VERSION_MIN@|4.5.0|" \
    -e "s|@BLENDER_VERSION_MAX@|5.0.0|" \
    -e "s|@PLATFORMS@|linux-x64|" \
    blender/weekend_raytracer/blender_manifest.toml.in \
    > blender/weekend_raytracer/blender_manifest.toml

EXT=~/.config/blender/4.5/extensions/user_default
mkdir -p $EXT
ln -sfn "$PWD/blender/weekend_raytracer" $EXT/weekend_raytracer
export HDW_PLUGIN_DIR="$PWD/build-hydra-blender/install/plugin/usd"
$BLENDER

# The symlink must resolve. It points at the PACKAGE directory, so the three .py files
# live in blender/weekend_raytracer/, not in blender/ beside blender.sh.
test -e $EXT/weekend_raytracer && echo "symlink ok" || echo "DANGLING"
```

Enable it in Preferences → Add-ons (search "Weekend"). Confirm the directory Blender actually uses
with `bpy.utils.user_resource('EXTENSIONS')` if the symlink does not take.

**A dev-loop fact to internalise now:** disabling the add-on does not `dlclose` `hdWeekend.so`.
Once the delegate is loaded, a rebuilt `.so` requires restarting Blender. Python-only edits are
cheap; C++ edits cost a restart. This is the strongest argument for Stage F, and the reason to do
Stage F before Stage G rather than after.

## GATE B — `Weekend` in the engine dropdown

```bash
$BLENDER --background --log "hydra.render" --log-level 3 --python-expr '
import bpy
bpy.ops.preferences.addon_enable(module="bl_ext.user_default.weekend_raytracer")

def engines(c=bpy.types.RenderEngine):
    for s in c.__subclasses__():
        if getattr(s, "bl_idname", None):
            yield s.bl_idname
        yield from engines(s)

print("REGISTERED ENGINES:", sorted(engines()))
bpy.context.scene.render.engine = "WEEKEND"
print("SELECTED:", bpy.context.scene.render.engine)
'
```

Want `['CYCLES', 'WEEKEND']` and `SELECTED: WEEKEND`. Two details in that snippet are load-bearing
and neither is obvious:

- **The walk must recurse.** `WeekendHydraRenderEngine` derives from `HydraRenderEngine`, so it is
  a *grandchild* of `RenderEngine`. A flat `RenderEngine.__subclasses__()` returns
  `[HydraRenderEngine, CyclesRender]` and misses it.
- **`addon_enable` is needed.** `--background` does not apply saved add-on preferences, and
  enabling here does not persist back to them — so the GUI still needs its own enable, once.

Then, in the GUI, Render Properties → Render Engine → **Weekend** without a console error. If the
engine is listed but selecting it raises `Cannot create render delegate: HdWeekendRendererPlugin`,
the `.so` built and the registry lookup failed — that is a `plugInfo.json` path or namespace
problem, not a Python problem, and `--log "hydra.render" --log-level 3` will say which.

> **9/8 10:38am — passes, after one plan gap and one wrong gate command.**
>
> `REGISTERED ENGINES: ['CYCLES', 'WEEKEND']` · `SELECTED: WEEKEND`, with the `.so` from
> `build-hydra-blender/install` via `HDW_PLUGIN_DIR`. No `hydra.render` warnings at level 3.
>
> **Gap — Step B3 could not have worked as written.** It symlinks `blender/weekend_raytracer/` into
> `user_default`, but nothing before Stage G produces a `blender_manifest.toml`, and Blender 4.2+
> will not see a manifest-less directory as an extension. It does not warn; the add-on simply is
> not in the list, which reads exactly like "my Python is broken". Hence the render step now in B3
> and the second row in B1's skeleton. Stage G is unaffected: `build_zip.sh` copies only `*.py` and
> renders its own manifest into the staging dir, so the gitignored dev copy never reaches the zip.
>
> **Correction — the old GATE B command could not have printed `WEEKEND`, ever.**
> `bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items` returned
> `['BLENDER_EEVEE_NEXT']` on a Blender where **Cycles was enabled and registered**
> (`CyclesRender`, `bl_idname='CYCLES'`, live in `__subclasses__()`). Cycles is the control: that
> property yields only built-in engines, on the type and on an instance alike, so the check would
> have reported failure with everything working — the same false-negative shape as GATE A's
> `ldd -r`. What actually proves registration is the recursive walk plus assigning
> `scene.render.engine` and reading it back, which is what the command above does.
>
> Worth noting for Stage C: assignment succeeding proves the *engine class* is registered. It does
> **not** exercise `CreateRenderDelegate` — that first happens at render, so the
> "Cannot create render delegate" failure mode above is still ahead, at GATE C, not retired here.

---

# Stage C — final render

## Step C1 — `get_render_settings`, where `aovToken:` is mandatory

Add to `WeekendHydraRenderEngine`:

```python
    def get_render_settings(self, engine_type: str):
        s = bpy.context.scene.hdweekend

        # Keys are the exact TfToken strings Blender forwards verbatim to
        # HdRenderDelegate::SetRenderSetting (engine.cc:126-129). The two unprefixed ones
        # are HdRenderSettingsTokens; the hdWeekend: ones are HdWeekendRenderSettingsTokens
        # from hydra/config.h.
        settings = {
            "convergedSamplesPerPixel": (s.viewport_samples if engine_type == "VIEWPORT"
                                         else s.samples),
            "threadLimit": s.thread_limit,
            "hdWeekend:maxBounces": s.max_bounces,
            "hdWeekend:randomNumberSeed": s.seed,
            "hdWeekend:tileSize": s.tile_size,
            "hdWeekend:jitterCamera": s.jitter_camera,
            "hdWeekend:enableSceneColors": s.enable_scene_colors,
        }

        if engine_type != "VIEWPORT":
            # NOT optional. FinalEngine builds its AOV map only from these keys, and the
            # RE_USE_GPU_CONTEXT fallback that saves Storm does not apply to a CPU engine.
            # Without them F12 yields an empty result and one CLOG_WARN. The key after the
            # colon must equal the RenderPass name registered in update_render_passes.
            settings |= {
                "aovToken:Combined": "color",
                "aovToken:Depth": "cameraDepth",
                "aovToken:Normal": "normal",
            }

        return settings
```

`cameraDepth`, not `depth`, for Blender's Z pass: Blender's Depth pass is camera-space distance,
while `HdAovTokens->depth` is NDC depth in [0,1]. Both are `float32` in our table and both are
implemented (`hydra/convert.h:34-36`), so if the Z pass looks wrong in the compositor this is a
one-token change — check it at GATE C rather than reasoning about it.

## Step C2 — `update_render_passes`

The pass name is the join between Blender and Hydra: `final_engine.cc:57` looks up
`aov_tokens_[rpass->name]`, and the channel count must match `HdGetComponentCount` of our
descriptor's format or `read_aov`'s memcpy writes the wrong number of floats.

```python
    def update_render_passes(self, scene, render_layer):
        if render_layer.use_pass_combined:
            self.register_pass(scene, render_layer, "Combined", 4, "RGBA", "COLOR")
        if render_layer.use_pass_z:
            self.register_pass(scene, render_layer, "Depth", 1, "Z", "VALUE")
        if render_layer.use_pass_normal:
            self.register_pass(scene, render_layer, "Normal", 3, "XYZ", "VECTOR")
```

4 / 1 / 3 against `float32_vec4` / `float32` / `float32_vec3` — consistent. Do not add passes for
`primId`, `instanceId` or `elementId`: `add_aov` refuses int32 before allocating anything, so they
would register a Blender pass that never receives data.

## Step C3 — `percentDone`

Two edits. `HdWeekendRenderer` has `SetSamplesToConvergence` but no getter
(`hydra/renderer.h:41`, `:58`), so add one beside `CompletedSamples()`:

```cpp
  [[nodiscard]] int CompletedSamples() const { return _renderer.completed_samples(); }
  [[nodiscard]] int SamplesToConvergence() const { return _renderer.samples_to_converge; }
```

Then in `hydra/renderDelegate.cpp`, `GetRenderStats()`:

```cpp
  // Blender reads this by raw string key and does UncheckedGet<double> on it
  // (source/blender/render/hydra/engine.cc:129-137). There is no percentDone in
  // HdPerfTokens, and the type is unchecked - it MUST be a double. Drives the F12
  // progress bar and the viewport's elapsed-time readout.
  const int target = std::max(1, _renderer.SamplesToConvergence());
  stats["percentDone"] = std::min(100.0, 100.0 * double(_renderer.CompletedSamples()) / double(target));
```

The `double`, the literal string key, and the clamp are all load-bearing. `usdview` shows the same
statistic, so this is not Blender-only work.

## GATE C — F12

Default cube, camera, Weekend engine, F12.

1. An image appears, and the progress bar moves — if the render completes but the bar stayed at 0%,
   Step C3 is wrong.
2. Enable the Z and Normal passes in View Layer Properties, re-render, and check both in the Image
   editor's pass dropdown.
3. Compare against the vanilla build on the same scene exported to USD:
   `usdrecord --renderer Weekend`. Same geometry, same shading — if they differ, the delegate is
   fine and the *host wiring* is not, which is a much smaller search.
4. Check orientation deliberately. Blender's `float_buffer` is bottom-up and `read_aov` is a
   straight memcpy, so nothing in the path flips anything. Our renderer writes at
   `by = height-1-y` and this is correct for `usdview`. It should therefore be correct here too,
   but "should" is doing real work in that sentence — render an obviously asymmetric scene (an
   object low in frame) and confirm, because a vertical flip is easy to overlook on a cube and
   maddening to find later.

Run once with `--log "hydra.render" --log-level 3` and read the output even if the image looks
right: `Couldn't find AOV token for render pass` and `Unsupported data format` are warnings, not
errors, and a partially-wired render otherwise looks like success.


> **9/9 — PASSES, after finding and fixing two defects.** All four items green; the diagnosis is
> kept below because both defects were silent and neither is Blender-specific.
>
> `engine_set_render_setting_func` logs all seven settings plus the three `aovToken:` keys,
> `add_aov` accepts `color`/`cameraDepth`/`normal`, level-3 `hydra.render` emits **zero** warnings,
> `percentDone` walks the bar 9% → 100% in 32 steps, Combined is 19200/19200 non-zero, and the
> image matches `usdrecord --renderer Weekend` on the exported USD to a mean |diff| of
> 0.0014/0.0008/0.0001 on RGB (Monte-Carlo noise at 32 spp), cube at row centroid 0.228 vs 0.230.
> Orientation was checked against a Cycles ground truth rather than assumed: row centroid 0.206 for
> both, row range [0,47] for both, bottom→top occupancy `[326,610,521,19]` vs `[328,611,523,20]`.
> No vertical flip.
>
> **Prerequisite gap — Stage E blocks Stage C.** `get_render_settings` dereferences
> `scene.hdweekend`, but the PropertyGroup that creates it is only written in Stage E. F12 raised
> `AttributeError` inside `bpy_types.py:1430`, which Blender prints and swallows — so *no* setting
> reached the delegate, `aovToken:` keys included, and the run then span forever reprinting
> `Done: 100%` with zero AOVs bound rather than erroring. That is the "F12 gives an empty image" row
> reached by an unexpected route; note that zero bindings **hangs** rather than fails.
> `properties.py` was written to unblock it. Step C1 and Stage E say `scene.hdweekend` while
> `engine.py` said `scene.hdWeekend` — standardised on the lowercase spelling.
>
> **Defect 1 — Combined all zero; nothing called `Resolve()`.** `color` is the only multisampled
> buffer, and for those the samples sit in `_samples` until `resolve()` divides them into
> `_resolved` — which is what `map()` returns. Nothing in `hydra/` called it. `usdview`/`usdrecord`
> work because `HdxAovInputTask::Prepare` resolves before reading (`hdx/aovInputTask.cpp:130`);
> Blender's `RenderTaskDelegate` builds only an `HdxRenderTask`, so nothing did, and it memcpy'd an
> all-zero buffer. hdEmbree has the identical structure and would fail here the same way.
> **Fixed** in `tracer::renderer::render()`, immediately after `schedule(...)` and *before* the
> cancel check: `resolve()` divides by the per-pixel `_sample_count` and skips untouched pixels, so
> a pass cancelled halfway still resolves correctly — resolving after the cancel check would discard
> it and leave the host on a stale frame, which is the common case in the viewport where tumbling
> cancels mid-render. It also lands before `_completed_samples.store(pass + 1)`, so the host never
> reads a sample count ahead of the pixels. `tracer/main.cpp:41`'s hand-rolled `resolve()` is now
> redundant.
>
> **Defect 2 — `cameraDepth` was scaled by `1/near_clip`.** `camera::get_ray` sets
> `direction = near_plane_trace` and never normalises, so `hit_info.t` was measured in near-plane
> distances. Blender's default `clip_start` of 0.1 made the Z pass exactly 10× Cycles; a sweep of
> `clip_start` 0.1/1.0/0.05/0.5 gave ratios 10.000/1.000/20.000/2.000, i.e. precisely `1/clip_start`.
> **Fixed** by scaling at the single write site (`hit_info.t * r.direction().length()`) rather than
> normalising in `get_ray`, because `t` is the closest-hit key throughout `bvh.h`/`scene.h`/`mesh.h`/
> `sphere.h` and shares units with the `t_min` epsilon. Post-fix the depth is invariant to
> `clip_start` — identical to 4 decimals across all four values.
>
> **`cameraDepth` is radial, and Blender's Z pass is planar — recorded so it is not re-investigated.**
> After the fix our depth still differs from Cycles by a mean 1.5%, rising to 3.8% at frame edges.
> That is not error: the measured per-pixel ratio (mean 1.01475) matches `sqrt(1 + tx² + ty²)`
> (mean 1.01465) to within 0.0002, the exact radial-vs-planar factor. hdEmbree normalises its camera
> ray before tracing (`plugin/hdEmbree/renderer.cpp:718-719`) and writes `ray.tfar` as
> `cameraDepth`, so USD's `cameraDepth` **is** radial distance from the eye and our post-fix value is
> spec-correct. Blender maps `cameraDepth` onto a Z pass it treats as planar (Cycles produces
> planar), so every external Hydra delegate — hdEmbree included — inherits the same edge
> discrepancy. Left alone deliberately: matching Cycles would mean deviating from the AOV's meaning.
>
> **Latent quirk, not a defect:** `t` remains in near-plane units internally, so the fixed `0.001`
> self-intersection epsilon at `tracer/renderer.h:324` is expressed in those units and scales with
> `clip_start`. Harmless at sane clip values; worth remembering if a scene ever uses an extreme one.
>
> **Residual manual checks**, both GUI-only and neither done here: the progress *widget* visibly
> advancing (the value reaching Blender is verified), and Z/Normal appearing in the Image editor's
> pass dropdown (read through compositor File Output nodes instead).
---

# Stage D — viewport

Nothing to write. `HydraRenderEngine.view_update` / `view_draw` (`bpy_types.py:1438-1457`) already
route to `ViewportEngine`, and for `bl_use_gpu_context = False` it takes the CPU branch:
`draw_texture_.create_from_buffer(render_task_delegate_->get_aov_buffer(HdAovTokens->color))`
(`viewport_engine.cc:262-266`), which maps our render buffer and uploads it each draw.

Two things were verified rather than assumed:

- **Format.** `create_from_buffer` selects `GPU_RGBA16F` only for `HdFormatFloat16Vec4` and
  `GPU_RGBA32F` + `GPU_DATA_FLOAT` for everything else (`viewport_engine.cc:169-176`). Our `color`
  is `float32_vec4`, so it takes the second branch and matches exactly.
- **Progressive.** `viewport_engine.cc:280-286` sets `bl_engine_->flag |= RE_ENGINE_DO_DRAW` while
  `!render_task_delegate_->is_converged()`, which re-enters `view_draw`. Convergence comes from
  `HdxRenderTask::IsConverged()`, hence from our render buffers. So progressive sampling reaches
  the viewport for free — and it stops when we say we are converged.
- The viewport calls `add_aov(color)` and `add_aov(depth)` unconditionally
  (`viewport_engine.cc:238-239`), so unlike final render it needs no `aovToken:` settings. This
  asymmetry is why Stage C's dict branches on `engine_type`.

## GATE D — viewport

Viewport shading → Rendered. Expect: an image that visibly refines, a status readout that counts
up, tumbling that restarts the render promptly (tile granularity is `hdWeekend:tileSize` —
[[hydra]] Step C1's cancellation path is what makes this feel responsive), and no runaway CPU once
converged. Then set `viewport_samples` low (say 16) and confirm it converges sooner than F12 does,
which proves the `VIEWPORT` branch of `get_render_settings` is actually being consulted.

---

# Stage E — settings panel

`properties.py`. Defaults are deliberately the same constants as `hydra/config.h`
(`HdWeekendDefault*`), so the panel agrees with the headless/env-var path ([[hydra-spec]] §15
mechanism 1) instead of quietly disagreeing with it.

```python
import bpy


class WeekendRenderSettings(bpy.types.PropertyGroup):
    samples: bpy.props.IntProperty(
        name="Samples", default=100, min=1, soft_max=4096,
        description="Samples per pixel before the image is considered converged")
    viewport_samples: bpy.props.IntProperty(
        name="Viewport Samples", default=32, min=1, soft_max=1024)
    max_bounces: bpy.props.IntProperty(
        name="Max Bounces", default=20, min=0, soft_max=64,
        description="Times a ray may scatter before it is terminated. Low values darken the image")
    tile_size: bpy.props.IntProperty(
        name="Tile Size", default=8, min=1, soft_max=256,
        description="Edge length of one unit of parallel work, and the granularity at which a "
                    "render can be cancelled. Large values make the viewport feel sluggish")
    seed: bpy.props.IntProperty(
        name="Random Seed", default=-1, min=-1,
        description="Any value other than -1 gives a repeatable image for a given scene and camera")
    thread_limit: bpy.props.IntProperty(
        name="Thread Limit", default=0, min=0, description="0 means all cores")
    jitter_camera: bpy.props.BoolProperty(name="Jitter Camera", default=True)
    enable_scene_colors: bpy.props.BoolProperty(name="Scene Colors", default=True)


class WeekendPanel(bpy.types.Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "render"
    COMPAT_ENGINES = {'WEEKEND'}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES


class WEEKEND_PT_sampling(WeekendPanel):
    bl_label = "Sampling"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        s = context.scene.hdweekend
        layout.prop(s, "samples")
        layout.prop(s, "viewport_samples")
        layout.prop(s, "max_bounces")
        layout.prop(s, "seed")
        layout.prop(s, "jitter_camera")
        layout.prop(s, "enable_scene_colors")


class WEEKEND_PT_performance(WeekendPanel):
    bl_label = "Performance"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        s = context.scene.hdweekend
        layout.prop(s, "thread_limit")
        layout.prop(s, "tile_size")


_classes = (WeekendRenderSettings, WEEKEND_PT_sampling, WEEKEND_PT_performance)


def register():
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.hdweekend = bpy.props.PointerProperty(type=WeekendRenderSettings)


def unregister():
    del bpy.types.Scene.hdweekend
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)
```

`properties.register()` must run before `engine.register()` (it does, in `__init__.py`), because
`get_render_settings` dereferences `scene.hdweekend`.

## GATE E — the panel changes the picture

Panels appear under Render Properties only when Weekend is the engine. Then prove each knob is
actually reaching the delegate, which the type-conversion note in Design notes explains is not
free: set `Max Bounces` to 0 and re-render — the image must go flat/dark. Set `Seed` to a fixed
value, render twice, and confirm the results are identical; set it to -1 and confirm they are not.
A knob that silently does nothing looks exactly like a knob that works, so test the two that have
unmistakable visual signatures.


> **9/9 — PASSES as written, but it surfaced a defect in a third knob.**
>
> **Gating.** All four panels poll `True` under `WEEKEND` and `False` under both `CYCLES` and
> `BLENDER_EEVEE_NEXT`. Getting here needed work the plan does not mention: Blender's built-in
> panels are engine-gated, so with only Stage E's classes registered the View Layer tab shows
> nothing but Custom Properties — the one panel with no `COMPAT_ENGINES` — and Output/Dimensions/
> Format are missing too. `properties.py` now carries its own view-layer Passes panel and the
> Cycles/`hydra_storm` whitelist idiom (`4.5/scripts/addons_core/hydra_storm/ui.py:187-199`),
> opting into 292 built-in panels at register and discarding them at unregister.
>
> **The two knobs the gate names both pass.** `Max Bounces` 20 → 0 takes mean luminance 0.7815 →
> 0.0000, i.e. fully black rather than merely flat. `Seed` 12345 rendered twice is *bit*-identical
> (max |diff| 0.0), and `-1` twice differs (max |diff| 0.118), so both the value and the negative
> sentinel survive the `int` → `Int64` → `Cast<int>` route the design note describes.
>
> **Three more, checked because "silently does nothing" is the failure mode:** `jitter_camera`
> (bool path) changes the image, `convergedSamplesPerPixel` 4 vs 64 changes it, and `thread_limit`
> 0 vs 1 costs 2.58× wall time. `tile_size` has no observable signature and was not independently
> proven; it reaches the renderer through the same setter path as the four above.
>
> **Defect, now fixed — `Scene Colors` was wired to nothing.** `hydra/mesh.cpp` read
> `HdWeekendConfig::GetInstance()`, the process-wide env-var singleton, so the value the panel
> forwarded as `hdWeekend:enableSceneColors` was accepted by the delegate and then never consulted
> by the only code that acts on it. Proven by separating the paths: with a `displayColor` authored
> onto the exported stage, `HDWEEKEND_ENABLE_SCENE_COLORS=1` renders the cube red
> `[0.785, 0.097, 0.119]` and `=0` renders it grey `[0.638, 0.709, 0.815]` under `usdrecord`, so
> the primvar handling and the singleton both worked — while toggling the panel checkbox changed
> nothing at all.
>
> The fix is in two halves, because either alone is insufficient:
>
> 1. **`mesh.cpp` polls the render settings**, with the config singleton as the fallback — the same
>    shape the render pass already uses, so an env var stays the starting value and the panel
>    overrides it. It also now caches the authored colour whenever the primvar is dirty *regardless*
>    of the flag, and applies the flag where the material is built. The flag decides what the
>    material uses, not what we remember, so toggling back on needs no second scene read. The
>    fallback grey moved out of `_displayColor`'s initialiser into a named
>    `kDefaultDisplayColor`, which that decoupling requires.
> 2. **The render pass invalidates on change.** `Sync` only re-reads `displayColor` when that
>    primvar is dirty, and moving a render setting dirties nothing, so the pass keeps the previous
>    value and marks every rprim `DirtyPrimvar` when it actually changes. `displayColor` is not one
>    of the specially-cased primvar names, so `HdChangeTracker::IsPrimvarDirty` resolves it against
>    exactly that bit (`hd/changeTracker.cpp:958-979`). Guarded on a real change, so a settings bump
>    for any other key costs nothing, and the render thread is already stopped at that point.
>
> **Verified:** the settings-map read gives red vs grey under `usdrecord` as above, and a full
> re-run of GATE C's render is unchanged — Combined 57600/57600, depth max 12.90, zero warnings at
> log level 3.
>
> **Not verified end-to-end, and cannot be on this machine:** the live-toggle invalidation. It needs
> a host that both supplies a `displayColor` *and* changes the setting while the delegate persists.
> Blender supplies none — not for an assigned material, not for a mesh colour attribute, and its USD
> file export writes none either (checked all three) — so in Blender the mesh always takes the
> `{0.8, 0.8, 0.8}` fallback and the checkbox has nothing to act on until `0.4.0`. On the USD side
> `usdrecord` renders one frame per process and `UsdAppUtils.FrameRecorder` exposes no
> `SetRendererSetting`, while `UsdImagingGL.Engine` has the setter but no Python AOV readback. A
> C++ harness would close this, and is the natural home for it in Stage F.

---

# Stage F — the `bpy` smoke test

The only automated test available here, and the fast loop for C++ changes: no app download, no
display, no GPU, and no Blender restart dance. It catches the whole class of failure that compiles
clean and dies at load — namespace mismatch, unresolved symbols at `dlopen`, a wrong `LibraryPath`,
`TF_REGISTRY_FUNCTION` not firing, `bl_delegate_id` not matching the registered `TfType`.

## Step F1 — A Python 3.11 environment

The wheel is cp311-only and this box has 3.12. `uv` fetches a managed interpreter without touching
the system Python:

```bash
uv venv --python 3.11 .venv-bpy45
uv pip install --python .venv-bpy45/bin/python bpy==4.5.13   # 373 MB
./blender/bpy.sh -c "import bpy; print(bpy.app.version_string)"   # 4.5.13 LTS
```

`bpy==4.5.13` is the same Blender build as the tarball — same `libusd_ms.so`, same
`pxrBlender_v25_02` namespace — so the `.so` under test is the shipped `.so`.

**And therefore it breaks in a sourced shell exactly the way the tarball does.** Calling
`.venv-bpy45/bin/python` directly after `source env.sh` gives

```
ImportError: .../bpy/lib/libusd_ms.so: undefined symbol: _ZTIN17MaterialX_v1_39_27ElementE
```

`LD_LIBRARY_PATH=$USD_ROOT/lib` is consulted before the wheel's RUNPATH (`$ORIGIN` =
`site-packages/bpy/lib`), so vanilla MaterialX **1.39.4** answers the `libMaterialXCore.so.1`
soname that bpy's **1.39.2** was linked against — the same collision, and the same symbol, as
Step 0.4 / GATE 0. `blender/bpy.sh` is the `blender/blender.sh` of Stage F: it scrubs
`LD_LIBRARY_PATH` / `PYTHONPATH` / `PXR_PLUGINPATH_NAME` and execs the venv interpreter.
Use it (or `$BPY_PYTHON`) everywhere below instead of `.venv-bpy45/bin/python`.

## Step F2 — `blender/smoke_test.py`

The script lives in the repo; read it there rather than here, because three of the
assumptions this plan made about `bpy` turned out to be wrong when measured on 2026-09-10,
and the file documents each one at the point it matters. All three reproduce with **stock
Cycles**, so none of them is anything to do with our delegate:

| Assumption | Reality | Consequence for the script |
|---|---|---|
| `bpy.data.images["Render Result"].pixels` returns the frame | Always `size (0, 0)`, `len(pixels) == 0` — for `WEEKEND` *and* `CYCLES` | Render to an EXR, then `images.load()` it back and read that |
| `any(p > 0.0)` catches a delegate that rendered nothing | Alpha is `1.0` across a fully-covered frame regardless, so it passes on a pure-black render | Test the colour channels only (`i % 4 != 3`) |
| A broken render fails, or at worst returns garbage | With no AOV bound it **spins at 100% CPU forever** | `faulthandler.dump_traceback_later(..., exit=True)` around the render call |

Plus one that is not about correctness but will hang CI: **`bpy` does not shut down cleanly
as a module.** The interpreter parks in a futex at teardown on both the success and the
exception path — the render itself has long since finished. The script ends in `os._exit()`
after flushing, which is the standard bpy-as-module workaround.

The watchdog deserves a note on *why* `faulthandler` and not a `threading.Timer`: the hang is
inside Blender's C++ render loop, which never returns to the interpreter, so no Python-level
timer would ever fire. `faulthandler`'s watchdog runs on its own C thread and calls `_exit(1)`,
so it works regardless of the GIL. `HDW_SMOKE_TIMEOUT` (default 120s) tunes it for slow CI
runners; a real 64×64×8spp frame takes ~0.2s, so it only ever fires on a hang.

Two assertions remain, but the first no longer means what this plan originally claimed. Reading
back from a file, `len(px)` checks the *file's* shape and would not, on its own, notice missing
AOV wiring — the render hangs before it gets there, and the watchdog is what reports that. The
colour-channel assertion is the one that catches a delegate producing nothing.

The digest is the regression check ([[ci]] §5): with a fixed seed and camera jitter off, the
image is reproducible, so a changed digest on an unchanged tracer means the *host integration*
moved. `view_transform` is pinned to `Raw` and the loaded image forced to `Non-Color` so the
number does not depend on the host's colour-management defaults.

Known gap, deliberately not closed here: `file_format = "OPEN_EXR"` writes only the Combined
pass, so `aovToken:Depth` and `aovToken:Normal` are exercised (they are bound, and removing
them changes nothing about the hang) but never *verified*. Checking their contents needs
`OPEN_EXR_MULTILAYER` and a different readback. Left for the 0.4.0 CI item.

## GATE F — green, and it fails when it should

```bash
HDW_PLUGIN_DIR=$PWD/build-hydra-blender/install/plugin/usd \
  ./blender/bpy.sh blender/smoke_test.py
```

**Run on 2026-09-10: green.** `OK 16384 pixels, digest d8e910b3f3b763d6`, exit 0, identical
across three consecutive runs — so the digest is reproducible and can become an asserted
constant whenever CI wants one.

Then break it deliberately, twice, and confirm each break is *reported* rather than silently
tolerated. Both were run:

1. **`HDW_PLUGIN_DIR` at a nonexistent directory.** Reported: `RuntimeError: hdWeekend
   plugInfo.json not found at /nonexistent/...`, raised from `engine.py` during
   `register_class`, exit 1. Behaves as designed in Step B2.
2. **`aovToken:` block removed from `get_render_settings`.** Blender logs exactly the warning
   Step F2's opening section predicted — `final_engine.cc:59 render: Couldn't find AOV token
   for render pass: Combined` — and then **hangs at 99.9% CPU indefinitely**. It does *not*
   fail an assertion, because nothing after the render call ever runs.

   Finding 2 is why the watchdog exists. Without it this break is worse than a silent pass: it
   burns a CI job to its job-level timeout with no diagnosis. With it, the run reports
   `Timeout (0:00:20)!` plus a stack at `bpy_types.py:1438 in render` and exits 1.

The `Couldn't find AOV token` warning is worth wiring into CI as a failure condition in its own
right ([[ci]] §5), since it names the broken pass and arrives ~20s before any watchdog does.

**Resolved: `bpy.ops.render.render` does *not* need a GPU context on the CPU path.** This step
was written with a fallback in case it did — driving the delegate through
`pxr.UsdImagingGL`/`usdrecord` for the pixel check and keeping the `bpy` half as a
load-and-register test. It is not needed. With `bl_use_gpu_context = False` the render
completes in ~0.2s, and re-running the gate under `env -u DISPLAY -u WAYLAND_DISPLAY -u
XDG_RUNTIME_DIR` is still green with a byte-identical digest. CI can rely on that: no display
server, no virtual framebuffer, no `xvfb-run`.


## Step G1 — `blender_manifest.toml.in`

Templated, never hand-edited, so the version window and platform list are always generated from the
target row ([[ci]] §7):

```toml
schema_version = "1.0.0"

id = "weekend_raytracer"
version = "0.3.0"
name = "Weekend Raytracer"
tagline = "CPU path tracer as a Hydra render delegate"
maintainer = "Nick Maclean <nick@imaclean.me>"
type = "add-on"
license = ["SPDX:GPL-3.0-or-later"]

blender_version_min = "@BLENDER_VERSION_MIN@"   # 4.5.0
blender_version_max = "@BLENDER_VERSION_MAX@"   # 5.0.0 - EXCLUSIVE
platforms = ["@PLATFORMS@"]                     # linux-x64

tags = ["Render"]
```

Verified against `blender_ext.py` @ 4.5, because each of these has a validator that rejects the
obvious guess:

- Required: `id`, `schema_version`, `name`, `tagline`, `version`, `type`, `maintainer`, `license`,
  `blender_version_min`. Everything else is optional.
- `type` must be `"add-on"` or `"theme"`.
- `tagline`: ≤ 64 characters and **must not end in punctuation** — the validator explicitly rejects
  a trailing full stop (`pkg_manifest_validate_terse_description_or_error`).
- `blender_version_max` is **exclusive**: the check is `filter_blender_version >= version_max`. So
  `"5.0.0"` covers every 4.5.x and correctly excludes 5.0, which is where USD moves to 25.08 and
  this binary stops being valid. That boundary is the whole point of the field here.
- **No `wheels`.** `hdWeekend.so` is not a Python module — it is a `Plug` plugin resolved through
  `LibraryPath` — so it ships as a plain file and `register()` derives its path from `__file__`.

### Licensing

The project is **GPL-3.0-or-later** (`LICENSE` at the repo root, `SPDX-License-Identifier`
headers on every source file). Three facts behind that, all verified 2026-09-07:

**GPL-2 was never available.** OpenUSD ships under the *Tomorrow Open Source Technology License
1.0* — Apache-2.0 with a modified §6 (Trademarks) — and oneTBB under Apache-2.0. Both carry
trademark and indemnification terms that GPL-2 forbids as "further restrictions". GPL-3 §7(e)
("Declining to grant rights under trademark law…") and §7(f) (indemnification) permit exactly
those, which is what makes the whole stack combinable. So this is a compatibility requirement, not
a taste. `GPL-2.0-or-later` would additionally have granted recipients a v2 option that cannot
lawfully be granted for the USD-derived parts.

**"or later" is required, not chosen.** The Extension Licenses page states: "For add-ons, the
required license is GNU General Public License v3.0 or later." `GPL-3.0-only` would be rejected.

**The manifest field is required and unvalidated.** `license` must be a non-empty list of
non-empty strings (`blender_ext.py:1995`) with no SPDX allowlist even under `--strict` — but
declare the real thing, `["SPDX:GPL-3.0-or-later"]`, since a reviewer reads the string.

**The eight hdTiny-derived files keep their upstream notices.** `mesh`, `renderDelegate`,
`renderPass` and `rendererPlugin` (`.cpp` and `.h` each) carry Pixar's original four-line header
byte-for-byte, followed by our SPDX lines and a line saying which half is which. Do not
consolidate them into a single GPL header: the combined work is GPL-3.0-or-later, and the original
material is still available upstream under TOST 1.0. Both statements need to remain true on the
face of the file.

The four test assets are redistributable with attribution and stay committed — StandardShaderBall
and OpenChessSet (CC BY 4.0), ElephantWithMonochord (CC BY-SA 4.0), Teapot (CC0); see
`assets/README.md`. Blender's platform requires assets *shipped inside* an add-on to be CC0, but
none of these ship in the zip, so that rule does not bite.

## Step G2 — `blender/build_zip.sh`

Stage the tree, render the manifest, copy the `.so` into the layout `plugInfo.json` expects, build:

```bash
#!/usr/bin/env bash
set -euo pipefail

STAGE=$(mktemp -d)/weekend_raytracer
mkdir -p "$STAGE"
cp blender/weekend_raytracer/*.py "$STAGE/"

sed -e "s|@BLENDER_VERSION_MIN@|4.5.0|" \
    -e "s|@BLENDER_VERSION_MAX@|5.0.0|" \
    -e "s|@PLATFORMS@|linux-x64|" \
    blender/weekend_raytracer/blender_manifest.toml.in > "$STAGE/blender_manifest.toml"

# Mirror cmake's install layout exactly: the .so is a SIBLING of hdWeekend/, because
# plugInfo.json says LibraryPath "../hdWeekend.so" with Root "..".
mkdir -p "$STAGE/plugin/usd"
cp -r build-hydra-blender/install/plugin/usd/. "$STAGE/plugin/usd/"

$BLENDER --command extension build --source-dir "$STAGE" --output-dir "$PWD/dist"
```

`--split-platforms` exists in 4.5 (`blender_ext.py:3174`) and is what CI will use once `platforms`
lists more than one entry; with a single platform it has nothing to split, so leave it off here.

## GATE G — a clean install renders

```bash
$BLENDER --command extension validate dist/weekend_raytracer-*.zip

# remove the dev symlink first, or you will test the wrong thing
rm ~/.config/blender/4.5/extensions/user_default/weekend_raytracer
unset HDW_PLUGIN_DIR

$BLENDER --command extension install-file --repo user_default -e dist/weekend_raytracer-*.zip
$BLENDER   # enable, select Weekend, F12
```

Deleting the symlink and unsetting `HDW_PLUGIN_DIR` is the entire point of this gate: with either
still in place, a zip whose `plugin/usd/` layout is wrong passes anyway. The zip must find its own
`.so` through `__file__`.

Finally, be honest in the commit message about what this artifact is: built against glibc 2.39, so
it runs on this box and not on Blender's stated 2.28 floor. It is a personal install, not a
release. The publishable equivalent is [[ci]] §4's manylinux container, in 0.4.0.

---

## Definition of done

- [x] `build-hydra-blender/` builds `hdWeekend.so` against Blender 4.5's USD 25.02, and
      `build-hydra/` still builds against our 26.05, from one source tree
- [x] `hydra/compat.h` exists, with exactly one documented divergence
      (`HDW_HAS_RENDERER_CREATE_ARGS`, USD 26.03)
- [x] cmake fails loudly on a namespace mismatch, driven by a variable CI can set
- [x] `Weekend` selectable as a render engine in Blender 4.5.13
- [x] F12 renders; Combined, Depth and Normal passes all populated; orientation confirmed on an
      asymmetric scene
- [x] Progress bar advances (`percentDone`, a `double`)
- [x] Rendered viewport shading works and refines progressively
- [x] Settings panels appear for the Weekend engine, and `Max Bounces` / `Seed` are shown to change
      the image
- [x] `blender/smoke_test.py` passes under `bpy==4.5.13`, and fails when the plugin path or the
      `aovToken:` settings are broken
- [x] `dist/weekend_raytracer-*.zip` validates, installs from a clean state, and renders
- [x] `docs/Roadmap.md`: `0.3.0 → blender plugin` checked
- [x] `env.sh` documents both flavours; `--log "hydra.render" --log-level 3` produces no warnings
      on a normal render

---

## Design notes — decisions recorded so they aren't re-litigated

**Why not one CMakeLists per flavour.** A flavour option keeps the source list, the target
properties and both `install()` rules in one place. Two files would drift, and the thing most
likely to drift is the install layout — which `plugInfo.json` depends on and which fails silently
when wrong.

**Why `blender/` is not under `hydra/`.** The Python is identical across every Blender version and
platform; the `.so` is specific to one of each. [[ci]] §1 is built entirely on that asymmetry
("a compile-and-fan-out problem, not an N-forks problem"), and the directory layout should make it
obvious rather than hide it.

**Why the namespace assertion is a cmake variable and not a literal.** [[ci]] §2 wants
`targets.toml` to be the single source of truth, with the namespace as an assertion against the
checked-out `pxr.h`. Making it `-D`-settable now means CI adds a row instead of patching cmake.

**Python `int` → `VtValue`, and why it happens to work.** Blender converts settings with
`PyLong_AsLong` (`python.cc:130-146`), so an `int` from the panel arrives as a C `long` — on LP64
that is `int64_t`, which `VtValue` holds as `Int64`, not `Int`. This would be a silent
fallback-to-default bug except that `GetRenderSetting<T>` is
`GetRenderSetting(key).Cast<T>().GetWithDefault(defValue)` (`renderDelegate.h:149-151`) — a
numeric `Cast`, not an `IsHolding` check — so `Int64` → `int` converts. It works, but it works for
a reason worth writing down: on a platform where `long` is 32-bit the payload type changes, and
anything that ever replaces that `Cast` with `IsHolding` breaks every integer setting at once.
`bool` and `str` map directly; there is no `float` setting yet, and one would arrive as `double`.

**Why `bl_use_preview = False` initially.** Material preview thumbnails render through
`PreviewEngine`, which derives from `FinalEngine`. With no material support every thumbnail is flat
and each one costs real CPU. Turn it on when 0.4.0 gives it something to show.

**Why the ID AOVs are not worked around.** Blender's `add_aov` refuses non-float formats before
allocating. Making our table float to satisfy it would break the `usdview` contract — hdEmbree's
table is what `primId` consumers expect — to gain passes Blender's compositor has no use for.

**Why the smoke test is the loop, not the gate.** `dlclose` never happens, so every C++ change
costs a Blender restart in the GUI. The `bpy` path is a subprocess. That inverts the usual order:
Stage F comes before packaging because it is a development tool first and a test second.

## Failure modes, and what each looks like

Assembled from the source read, because every one of these is silent or misattributed:

| Symptom | Cause | Where to look |
|---|---|---|
| `Weekend` absent from the engine list | add-on not enabled, or `register()` raised | Blender console; the `RuntimeError` in Step B2 |
| `Cannot create render delegate: HdWeekendRendererPlugin` | `RegisterPlugins` path wrong, or namespace mismatch | `nm -DC \| grep pxrBlender_v25_02`; `--log "hydra.render" --log-level 3` |
| `dlopen` fails / undefined symbol | built against the wrong USD, or a missing `DT_NEEDED` soname | `ldd -r`, `readelf -d` (GATE A) |
| F12 gives an empty image | `aovToken:` settings missing — the CPU path has no fallback | `CLOG_WARN "Couldn't find AOV token"` |
| A pass registers but stays empty | AOV format not float32/float16 | `CLOG_WARN "Unsupported data format"` |
| Progress bar stuck at 0%, viewport timer resets each draw | no `percentDone` in `GetRenderStats` | Step C3 |
| A settings knob does nothing | key string ≠ the token, or a type `Cast` failure | print `GetRenderSetting` in the render pass |
| Geometry vanishes with no error | scene-index `loadWithRenderer` display-name mismatch | `hydra/plugInfo.json` — already commented in both plugin sources |
| Image vertically flipped | orientation assumption in the memcpy path | GATE C item 4 |
| Loads here, fails on another machine | glibc 2.39 vs Blender's 2.28 floor | expected; [[ci]] §4 |

## Next up

`0.4.0` — and this task deliberately leaves it three things:

- `ci to auto-compile for multiple blender versions` inherits `compat.h`, the `-D`-settable
  namespace assertion, the manifest template and `smoke_test.py`. Adding 5.2 becomes a
  `targets.toml` row plus whatever the compiler complains about ([[ci]] §2, §6).
- `materialX or something…` turns on `bl_use_materialx` and `bl_use_preview`, and gives Blender's
  exported `UsdPreviewSurface` networks somewhere to land.
- `texture mapping` needs no add-on change at all.
