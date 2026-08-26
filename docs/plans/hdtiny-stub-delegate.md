# hdTiny stub delegate — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `hdTiny stub delegate` (see [[Roadmap]])
**Context:** [[hydra-spec]] §3, §4, §5, §17, §18, §19, §20 · [[roadmap-discussion-8-26]] §1
**Environment facts below were verified against the real tree on 2026-08-26.**

---

## What this task is

Spec §20 calls this **T0 — Loads**:

> `plugInfo.json` + `HdRendererPlugin` + an `hdTiny`-shaped delegate that stubs all 16 pure
> virtuals and prints callbacks. Verify it appears in `GetRendererPlugins()` and can be
> selected in `usdview`.

At the end of this task you will have a shared object that USD discovers, loads, and drives —
and which prints a line every time Hydra calls into it. **It renders nothing.** No pixels, no
rays, no camera, and it does not touch a single line of `tracer/`.

## Why it's first (and not last)

This moved from 0.3.0 to the head of 0.2.0 during the 8-26 review. The reasoning ([[roadmap-discussion-8-26]] §1):

The highest-risk items in the whole Hydra effort are **environmental, not algorithmic** —
ABI mangling, plugin search-path assembly, static-initializer stripping, `plugInfo.json`
substitution. None of them depend on the tracer refactor, all of them are days-long when they
bite, and every one of them fails *silently*. A stub that loads proves the environment works
before you have any real code to blame.

The secondary payoff: the camera / render-target / tile-loop refactors that follow get designed
against an API you have actually compiled and linked against, instead of against a guess.

## What is explicitly NOT in this task

Deferred to later 0.2.0 / 0.3.0 items — do not let them creep in:

| Not now | Comes with |
|---|---|
| Reading points/topology, triangulating | `triangle mesh` |
| `HdRenderBuffer` / AOVs / any pixels | `render target refactor` (spec T1) |
| Camera matrices, data window | `camera api refactor` (spec T1) |
| `HdRenderThread`, cancellation | `interruptible tile-driven render loop` (spec T3) |
| `HdRenderParam`, `sceneVersion`, `AcquireSceneForEdit` | `scene graph with mutation` (spec T2) |
| Implicit-surface scene index plugin | later — see "Design notes" below |
| Materials, lights, instancing | 0.4.0+ |

---

## Environment — verified facts

These were checked on this machine on 2026-08-26. They are the constraints the build has to satisfy.

| Fact | Value | Why it matters |
|---|---|---|
| USD source | `~/opt/OpenUSD`, v26.05 (`0.26.5`) | Reference implementations live here |
| USD install | `~/opt/usd_src_build` | `pxrConfig.cmake` + `cmake/pxrTargets.cmake` here |
| Internal namespace | `pxrInternal_v0_26_5__pxrReserved__` | Version-mangled; ABI mismatch = link/`dlopen` failure (§18.1) |
| USD C++ standard | **C++17** (`cmake/defaults/CXXDefaults.cmake:12`) | Plugin must match exactly |
| USD compiler | GCC 13.3 — matches system `g++ 13.3.0` | Match exactly (§18.1) |
| Python for USD tools | `~/opt/usd-build-venv/bin/python` | Only this one has PySide6/PyOpenGL; system `python3` can `import pxr` but **cannot** run `usdview` |
| USD's TBB | **2020.3**, `libtbb.so.2`, `TBB_INTERFACE_VERSION 11103` | *Not* oneTBB — see the CMake warning in step 5 |
| Tracer's TBB | oneTBB **2023.1.0** via FetchContent, `libtbb.so.12` | Different soname, different CMake target, same target *name* |
| Baseline renderer list | `renderer {Storm,Embree,GL}` | This is the string step 7 has to change |

**Control experiment (already run, it works):** pointing `PXR_PLUGINPATH_NAME` at the installed
stock hdTiny turns the list into `renderer {Storm,Embree,Tiny,GL}`. The discovery mechanism on
this machine is known-good. If your plugin doesn't show up, the fault is yours, not the environment's.

---

## Naming

| Thing | Value |
|---|---|
| Directory | `hydra/` (repo root) |
| Library | `hdWeekend.so` |
| Plugin class | `HdWeekendRendererPlugin` |
| Delegate class | `HdWeekendRenderDelegate` |
| `displayName` | `Weekend` |
| `Name` in plugInfo | `hdWeekend` |

`displayName` is load-bearing three ways (§3.2): it is the string `usdview --renderer` /
`usdrecord --renderer` accept, it is the key scene-index plugins match on via `loadWithRenderer`,
and it is what `GetRendererDisplayName()` returns. The C++ class name string in `plugInfo.json`
must match the registered `TfType` name **exactly**.

---

# Step 0 — Run the control experiment yourself

**Why:** before writing anything, see a plugin get discovered. Then you know what success looks
like, and you have a working reference to diff against when yours doesn't.

```bash
export USD_ROOT=$HOME/opt/usd_src_build
export USD_PY=$HOME/opt/usd-build-venv/bin/python
export PYTHONPATH=$USD_ROOT/lib/python
export LD_LIBRARY_PATH=$USD_ROOT/lib

# baseline
$USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -o 'renderer {[^}]*}'
# renderer {Storm,Embree,GL}

# stock hdTiny on the path
PXR_PLUGINPATH_NAME=$USD_ROOT/share/usd/examples/plugin/hdTiny/resources \
  $USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -o 'renderer {[^}]*}'
# renderer {Storm,Embree,Tiny,GL}
```

Now look at the layout that made that work — **memorise this shape**, step 6 has to reproduce it:

```
share/usd/examples/plugin/
├── hdTiny.so                        ← the library is a SIBLING of the directory
└── hdTiny/
    └── resources/
        └── plugInfo.json            ← PXR_PLUGINPATH_NAME points HERE
```

and the substituted `plugInfo.json` in it:

```json
"LibraryPath": "../hdTiny.so",  "ResourcePath": "resources",  "Root": ".."
```

**How Plug resolves those three** (this is the part that is easy to get wrong by one directory):

1. `Root` is relative to the directory containing `plugInfo.json` → `.../hdTiny/resources/..` = `.../hdTiny`
2. `LibraryPath` is relative to **`Root`**, not to the resources dir → `.../hdTiny/../hdTiny.so` = `.../hdTiny.so`
3. `ResourcePath` is relative to `Root` → `.../hdTiny/resources`

So the `.so` sits *beside* the plugin's directory. Get this wrong and `plugInfo.json` is found,
the type is registered, and the library silently fails to load.

---

# Step 1 — Write `env.sh` at the repo root

**Why:** every command in this plan needs four env vars set correctly. Retyping them is how you
end up debugging a plugin that was never on the path. Commit it — this is a one-machine project;
make it `env.sh.in` if that ever stops being true.

```bash
# weekend-raytracer/env.sh — source before any usd tool
export USD_SRC=$HOME/opt/OpenUSD
export USD_ROOT=$HOME/opt/usd_src_build
export USD_PY=$HOME/opt/usd-build-venv/bin/python

export PYTHONPATH=$USD_ROOT/lib/python${PYTHONPATH:+:$PYTHONPATH}
export LD_LIBRARY_PATH=$USD_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PATH=$USD_ROOT/bin:$PATH

# where step 6 installs to, and where Plug is told to look
export HDW_INSTALL=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-hydra/install
export PXR_PLUGINPATH_NAME=$HDW_INSTALL/plugin/usd/hdWeekend/resources
```

Note `PXR_PLUGINPATH_NAME` is the *default* env var name — USD lets a build rename it via
`PXR_OVERRIDE_PLUGINPATH_NAME` (§3.3), but step 0 proved this build didn't.

Also add to `.gitignore`:

```
/build-hydra
```

---

# Step 2 — Create `hydra/` and copy the skeleton

**Why a separate top-level directory:** `hydra/` is the *only* place in the repo that ever
includes a USD header. `tracer/` stays USD-free forever — that boundary is what keeps
`tracer_cli` and the SDL viewer buildable on a machine with no USD installed, and it's what makes
the tracer reusable. Enforce it from day one; it is miserable to reintroduce later.

```bash
cd ~/git/weekend-raytracer
mkdir -p hydra
cp $USD_SRC/extras/imaging/examples/hdTiny/{rendererPlugin,renderDelegate,renderPass,mesh}.{h,cpp} hydra/
```

That is 629 lines of Pixar-authored skeleton. **Keep the copyright headers** — the files carry a
`Copyright 2020 Pixar` / `LICENSE.txt` block and this is a derivative work.

**Why copy `mesh.{h,cpp}` too** (the earlier 8-25 draft said not to): hdEmbree's entire Rprim set
is `mesh`, real USD assets are mesh-based, and the 8-26 review moved `triangle mesh` *into* 0.2.0
for exactly that reason ([[roadmap-discussion-8-26]] §1). Making `mesh` the stub's one Rprim means
the callback trace in step 8 exercises the prim type we'll actually implement, and `AddCube` in the
test just works. The analytic-sphere shortcut is discussed under "Design notes" — it is not needed here.

Read these two before editing anything:

- `hydra/renderDelegate.cpp` (189 lines) — the shape everything hangs off
- `$USD_SRC/pxr/imaging/plugin/hdEmbree/renderParam.h` (63 lines) — the shape you're heading toward

---

# Step 3 — Rename, mechanically

```bash
cd hydra
sed -i 's/HdTiny/HdWeekend/g; s/EXTRAS_IMAGING_EXAMPLES_HD_TINY/HD_WEEKEND/g; s/Tiny /Weekend /g' *.h *.cpp
git diff --no-index /dev/null . | less   # or just read each file
```

That gets ~95% of it. Then check by hand:

- **Include guards** are now `HD_WEEKEND_RENDER_DELEGATE_H` etc. — no collisions.
- **`std::cout` strings** now say `Weekend`. **Keep every one of them.** They are not debug
  cruft; they are the entire deliverable of this task and the verification in step 8.
- **Self-includes** are already local (`#include "renderDelegate.h"`) — hdTiny is written that
  way. Nothing to change.

### The one thing that will silently destroy you

**Do not touch `PXR_NAMESPACE_OPEN_SCOPE` / `PXR_NAMESPACE_CLOSE_SCOPE`.** Every declaration must
stay inside them. Because `PXR_NS` is a namespace *alias* onto the mangled
`pxrInternal_v0_26_5__pxrReserved__`, code that omits the macros **compiles, links, and loads
fine** — and then never registers, because the `TfType` it registers is a different type from the
one the registry looks up. §18.1. This is the single most expensive mistake available in this task.

### Confirm the `IsSupported` signature

The 26.05 pure virtual is:

```cpp
bool IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                 std::string *reasonWhyNot = nullptr) const override;
```

Return `true` unconditionally, as hdTiny and hdEmbree both do ("we assume if the plugin loads
correctly it is supported"). This is not laziness — a CPU renderer returning `true` is what makes
it the automatically-correct choice under `usdrecord --disableGpu`, where GPU-requiring plugins are
filtered out (§4, §17.3).

If you ever see a `bool gpuEnabled` overload instead, you copied from an older tree and your class
is still abstract.

---

# Step 4 — Write `hydra/plugInfo.json`

**Why you write it by hand:** in-tree, the `pxr_plugin()` CMake macro runs `_plugInfo_subst()`
(`cmake/macros/Private.cmake:159`) to fill in the `@PLUG_INFO_*@` tokens. Out of tree that macro
isn't available, so you write the *substituted* result directly (§18.3).

```json
{
    "Plugins": [
        {
            "Info": {
                "Types": {
                    "HdWeekendRendererPlugin": {
                        "bases": [
                            "HdRendererPlugin"
                        ],
                        "displayName": "Weekend",
                        "priority": 99
                    }
                }
            },
            "LibraryPath": "../hdWeekend.so",
            "Name": "hdWeekend",
            "ResourcePath": "resources",
            "Root": "..",
            "Type": "library"
        }
    ]
}
```

- `bases` **must** be `["HdRendererPlugin"]` — that is literally how the registry finds it (§3.2).
- `priority: 99` matches Storm and Embree.
- No scene-index plugin entry yet. Adding an entry for a class that doesn't exist gives a load
  error rather than a clean gate.

---

# Step 5 — Write `hydra/CMakeLists.txt` as a **separate CMake project**

**Do not `add_subdirectory(hydra)` from the root `CMakeLists.txt`.** This is the load-bearing
build decision of the task, and here is the verified reason:

- `vendor/tbb/CMakeLists.txt` does `FetchContent_MakeAvailable(TBB)` on oneTBB v2023.1.0, which
  creates the target **`TBB::tbb`**.
- `~/opt/usd_src_build/pxrConfig.cmake:58` does `add_library(TBB::tbb SHARED IMPORTED)` pointing at
  `$USD_ROOT/lib/libtbb.so` — **TBB 2020.3**.
- `pxrTargets.cmake:525` puts `TBB::tbb` in `hd`'s `INTERFACE_LINK_LIBRARIES`, so you cannot avoid
  it by not asking for it.

Two `add_library(TBB::tbb ...)` calls in one CMake project is a hard configure error. And even if
it weren't, pulling both `libtbb.so.2` (2020.3) and `libtbb.so.12` (oneTBB 2023.1) into one process
gives you two independent thread pools — precisely the oversubscription that §17.6 warns about and
that the OMP→TBB migration was meant to remove.

The stub itself needs no TBB at all. Split the projects **now** anyway: the clash arrives the
moment `hydra/` links anything from `tracer/`, and restructuring the build later is pure churn.

Because `tracer` is a header-only `INTERFACE` target that links vendored `TBB::tbb`, the separate
project must include tracer's *headers* directly and never link the target. (Not needed at T0 —
the include line below is there so the boundary is already correct when T1 arrives.)

```cmake
cmake_minimum_required(VERSION 3.15)
project(hdWeekend CXX)

# Must match the USD build exactly (§18.1): USD 26.05 is C++17, GCC 13.3.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pxr REQUIRED
             PATHS $ENV{USD_ROOT}
             NO_DEFAULT_PATH)

add_library(hdWeekend SHARED
    rendererPlugin.cpp
    renderDelegate.cpp
    renderPass.cpp
    mesh.cpp)

# hdTiny links only hd + tf; hd transitively brings plug, tf, vt, work, hf, TBB::tbb.
target_link_libraries(hdWeekend PRIVATE hd tf)

# Header-only tracer, for T1. Never link the `tracer` target - it would drag in oneTBB.
target_include_directories(hdWeekend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tracer)

# hdWeekend.so, not libhdWeekend.so - LibraryPath in plugInfo.json says so.
set_target_properties(hdWeekend PROPERTIES PREFIX "")

# Install layout per §3.3 - note the .so is a SIBLING of the plugin directory.
install(TARGETS hdWeekend        DESTINATION plugin/usd)
install(FILES   plugInfo.json    DESTINATION plugin/usd/hdWeekend/resources)
```

Two details worth not getting wrong:

1. **`PREFIX ""`.** Without it CMake produces `libhdWeekend.so` and `LibraryPath: "../hdWeekend.so"`
   resolves to nothing.
2. **The install destinations.** `TARGETS` → `plugin/usd/`, `plugInfo.json` →
   `plugin/usd/hdWeekend/resources/`. Re-derive it from step 0's resolution rules if you're unsure;
   the sibling relationship is genuinely counter-intuitive and an earlier draft of this plan got it
   wrong by one level.

**No `pch.h`.** `pxr_plugin` expects one unless you pass `DISABLE_PRECOMPILED_HEADERS` (hdTiny
does); out of tree the question doesn't arise.

**No LTO, no `--gc-sections`.** `TF_REGISTRY_FUNCTION` relies on static initializers in the shared
object; those configurations drop them silently (§18.4). Don't add them "for size" later either.

---

# Step 6 — Build and install

```bash
source env.sh
cmake -S hydra -B build-hydra -DCMAKE_INSTALL_PREFIX=$HDW_INSTALL
cmake --build build-hydra --target install -j
```

Verify the layout matches step 0's shape exactly:

```bash
find $HDW_INSTALL -type f
# .../install/plugin/usd/hdWeekend.so
# .../install/plugin/usd/hdWeekend/resources/plugInfo.json
```

If those two paths aren't exactly that, fix step 5 before going further. Everything downstream
assumes them.

---

# Step 7 — GATE 1: the plugin is discovered

This is the gate the whole task exists for. Do not proceed past it.

```bash
source env.sh

$USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -o 'renderer {[^}]*}'
# want: renderer {Storm,Embree,Weekend,GL}

$USD_PY -c "from pxr import UsdImagingGL; print(UsdImagingGL.Engine.GetRendererPlugins())"
# want: Weekend in the list
```

### If `Weekend` does not appear

Work this list in order — it's ranked by how cheap the check is, and each one eliminates a distinct
failure mode from §3.3 / §18:

**1. Was the registration function stripped?**
```bash
nm -D $HDW_INSTALL/plugin/usd/hdWeekend.so | grep -i registry
```
Empty ⇒ `TF_REGISTRY_FUNCTION(TfType)` didn't survive. Confirm `rendererPlugin.cpp` is genuinely in
the target's sources; check for LTO / `--gc-sections` / symbol-visibility flags leaking in from a
toolchain file. (§18.4)

**2. Is the namespace mangled correctly?**
```bash
nm -DC $HDW_INSTALL/plugin/usd/hdWeekend.so | grep RendererPlugin
```
Symbols must read `pxrInternal_v0_26_5__pxrReserved__::HdWeekendRendererPlugin`. If they read
`pxr::` or bare `HdWeekendRendererPlugin`, a `PXR_NAMESPACE_OPEN_SCOPE` is missing — see step 3.

**3. Was `plugInfo.json` even found, and did `LibraryPath` resolve?**
```bash
TF_DEBUG=PLUG_INFO_SEARCH $USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -i weekend
TF_DEBUG=PLUG_REGISTRATION $USD_PY $USD_ROOT/bin/usdrecord --help 2>&1 | grep -i weekend
```
Nothing from `PLUG_INFO_SEARCH` ⇒ `PXR_PLUGINPATH_NAME` is wrong; it must point at the directory
*containing* `plugInfo.json`, i.e. `.../hdWeekend/resources`. Remember the search path is assembled
**once** at library-load time by `Plug_InitConfig`, and **first path containing a plugin wins**
(§3.3) — so exporting the var after the process starts does nothing.

**4. Does the library load at all?**
```bash
ldd $HDW_INSTALL/plugin/usd/hdWeekend.so | grep 'not found'
```
Anything listed ⇒ `LD_LIBRARY_PATH` from `env.sh` isn't in effect.

**5. Class-name mismatch.** The key in `plugInfo.json` (`HdWeekendRendererPlugin`) must match the
C++ type passed to `HdRendererPluginRegistry::Define<>()` character for character.

---

# Step 8 — GATE 2: Hydra actually drives it (headless)

Discovery is not the same as being usable. This gate proves the delegate gets constructed, prims
get created, `Sync` runs, and the render pass executes.

**Why headless first:** it needs no display and no GL, it's the thing you can put in CI, and it
isolates delegate faults from `usdview`'s much larger surface area (§19).

```bash
cp $USD_SRC/extras/imaging/examples/hdTiny/testenv/testHdTiny.cpp hydra/tests/testHdWeekend.cpp
```
(101 lines; `mkdir -p hydra/tests` first.) Change exactly one string — the token *value* on
line 25; leave the variable name alone:

```cpp
const TfToken tinyRendererPluginId("HdWeekendRendererPlugin");
```

The test does exactly what §19's "minimal headless render" row describes: fetch the plugin from
`HdRendererPluginRegistry`, `CreateRenderDelegate()`, `HdRenderIndex::New(delegate, {})`, populate
via `HdUnitTestDelegate::AddCube`, drive an `HdxRenderTask`, `HdEngine::Execute()`, then assert
`TfErrorMark::IsClean()`.

Add to `hydra/CMakeLists.txt`:

```cmake
add_executable(testHdWeekend tests/testHdWeekend.cpp)
target_link_libraries(testHdWeekend PRIVATE hd hdx tf gf)
```

`hdx` is needed **only by the test harness**, not by the delegate (§17.5) — the delegate itself
stays GPU-free. The file's own comments sketch a hand-written `MyDrawTask` if you ever want to drop
the `hdx` dependency entirely.

Run it:

```bash
cmake --build build-hydra -j && ./build-hydra/testHdWeekend
```

**Expected output** — this trace *is* the deliverable:

```
Creating Weekend RenderDelegate
Create Weekend Rprim type=mesh id=/MyCube1
Create RenderPass with Collection=geometry
* (multithreaded) Sync Weekend Mesh id=/MyCube1
=> CommitResources RenderDelegate
=> Execute RenderPass
Destroy Weekend Rprim id=/MyCube1
Destroying Weekend RenderDelegate
Destroying renderPass
OK
```

The trailing `OK` (and exit status 0) comes from the `TfErrorMark::IsClean()` check in `main` — a
`FAILED` there means Hydra logged an error even though the trace looked right.

Reading it against §2's three phases: the `Sync` line is phase 1 (and note it says *multithreaded*
— `SyncAll()` walks prims in parallel, which is why §6's edit gateway will be needed later);
`CommitResources` is phase 2, the one serial hook; `Execute RenderPass` is phase 3.

If `Create ... type=mesh` never appears but the delegate is created, the fault is
`SUPPORTED_RPRIM_TYPES` in `renderDelegate.cpp`.

---

# Step 9 — GATE 3: selectable in `usdview`

```bash
source env.sh
$USD_PY $USD_ROOT/bin/usdview --renderer Weekend <any-scene>.usda   # -r also works
```

Any `.usda` will do — grab one from `$USD_SRC/extras/usd/examples/` or write three cubes by hand.

**Expected:** the window opens, the viewport is empty/black (correct — the render pass draws
nothing), the renderer menu shows *Weekend* as selected, and the terminal fills with the same
callback trace, re-firing as you tumble.

Also worth running once:

```bash
$USD_PY $USD_ROOT/bin/usdview --renderer Weekend --quitAfterStartup <scene>.usda
```

A pure load-and-exit smoke test, which is the cheap version of this gate for future regressions.

`usdrecord --renderer Weekend --disableGpu <scene>.usda /tmp/out.png` will also *run* at this point,
but the image will be empty — writing pixels is T1 (`render target refactor`). Don't treat a blank
PNG as a failure here.

---

# Step 10 — Commit

```bash
git add hydra/ env.sh .gitignore docs/plans/hdtiny-stub-delegate.md
git commit -m "hdWeekend: hdTiny-shaped stub render delegate that loads in usdview"
```

Then tick `hdTiny stub delegate` in [[Roadmap]].

---

## Definition of done

- [ ] `usdrecord --help` lists `Weekend`
- [ ] `UsdImagingGL.Engine.GetRendererPlugins()` includes `Weekend`
- [ ] `testHdWeekend` prints the full callback trace and exits clean
- [ ] `usdview --renderer Weekend` opens and stays open
- [ ] `tracer_cli` and `viewer` still build **with USD entirely off the path** — the `hydra/` ↔
      `tracer/` boundary is intact
- [ ] `env.sh` committed; `build-hydra/` ignored

---

## Design notes — decisions deferred, recorded so they aren't re-litigated

**Analytic spheres vs. the implicit-surface scene index.** Hydra hands you `sphere` / `cube` /
`cone` / `cylinder` / `capsule` / `plane` as native prim types **only if you decline to register**
`hdsi/implicitSurfaceSceneIndex`; register it and they arrive pre-tessellated as meshes (§16).
Since the tracer already has an analytic `sphere::hit`, there is a real temptation to keep
`sphere` native and let everything else become mesh — hdEmbree's plugin does the reverse, converting
everything. `HdsiImplicitSurfaceSceneIndex` stores one mode token per type, so a type omitted from
`inputArgs` passes through untouched, which makes the split easy to express. **Not now**: the
scene-index plugin's `_pluginDisplayName` must match `displayName` exactly or geometry silently
vanishes, and that is a debugging session you do not want tangled up with first-load problems.
Revisit under `triangle mesh`.

**No `HdRenderParam` yet.** `GetRenderParam()` returning `nullptr` (hdTiny's behaviour) is correct
while there is no scene to guard. When it lands it must be the *only* route to the scene —
`AcquireSceneForEdit()` calls `StopRender()`, bumps `sceneVersion`, and hands back the pointer;
the raw member stays private with no other accessor so the stop can't be forgotten (§6).

**No `SetDrivers` / Hgi.** Leave it unimplemented. A CPU delegate gets viewport presentation,
colour correction, and picking for free: `HdxAovInputTask` sees an empty `VtValue` from
`GetResource()`, falls back to `Map()`, and uploads the CPU pixels into an `HgiTexture` itself
(§17.2). The cost is one full-frame CPU→GPU upload per presented frame.

**Two TBBs is a live issue, just not yet.** USD here is 2020.3 (`libtbb.so.2`), the tracer is
oneTBB 2023.1 (`libtbb.so.12`). Different sonames, so they *can* coexist in one process — but as
two independent thread pools. The fix, per §17.6, is that the tracer must stop owning its parallel
loop: expose `render_tile(tile, sample)` and let the caller drive it, so the SDL viewer keeps
`tbb::parallel_for` while the delegate uses `WorkParallelForN` inside USD's arena. That is a
constraint on `interruptible tile-driven render loop`, not a task of its own.

---

## Standing verification loop, for everything after this

```bash
source env.sh
cmake --build build-hydra --target install -j && ./build-hydra/testHdWeekend
```

Debug env vars, in rough order of usefulness:

| Var | Shows |
|---|---|
| `TF_DEBUG=PLUG_INFO_SEARCH` | which `plugInfo.json` files were found, and where |
| `TF_DEBUG=PLUG_REGISTRATION` | which types actually registered |
| `TF_DEBUG=HD_*` | sync / dirty-bit traffic |
| `usdview --quitAfterStartup` | load-only smoke test |

Consider registering your own `TF_DEBUG_CODES` (see `hdEmbree/debugCodes.h`) once there is
anything worth tracing — §19 lists it as the diagnostics conformance level.

---

## Next up

`camera api refactor` and `render target refactor` — together they are spec **T1, "First pixels"**.
Do them knowing what `_Execute` actually receives: only `GetWorldToViewMatrix()` and
`GetProjectionMatrix()` (§11), plus the data window from
`renderPassState->GetFraming().dataWindow` with a `GetViewport()` fallback (§9). The data window is
**y-down** while image line order is bottom-to-top; budget an afternoon for that flip, and debug it
on a scene that is asymmetric in *both* axes — a symmetric one hides it.
