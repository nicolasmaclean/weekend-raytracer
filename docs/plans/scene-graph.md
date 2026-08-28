# scene graph with mutation — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `scene graph with mutation` — see [[Roadmap]]
**Context:** [[hydra-spec]] §6 (`HdRenderParam` + the scene-edit gateway), §7.1–§7.4 (Rprim contract,
visibility, `Finalize`), §8.3 (the `primId`/`instanceId`/`elementId` AOVs), §9 (render-pass change
detection), §10.1 (`StopRender` semantics) · [[roadmap-discussion-8-26]] §6, §5 items 3 and 5 ·
[[transform-support]] "Next up"
**Every number in this document was measured on this machine on 2026-08-27. See "Pre-verified facts".**

---

## What this task is

[[hydra-spec]] §6 gives the requirement as a seven-line code block:

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

Everything in that block is on the *delegate* side, and none of it can be written yet, because
`MyScene` does not exist. What the tracer has today is `hittable_list`: a
`std::vector<shared_ptr<hittable>>` with `add()` and `clear()`. It has no prim identity, no
removal, no version, no visibility flag, and nothing that could plausibly be handed back by an
`AcquireSceneForEdit()`.

This task builds `MyScene`. At the end of it there is one new ~170-line header, `tracer/scene.h`,
holding two classes:

- **`scene`** — a `hittable` whose prims are addressable by a stable `prim_handle`, insertable,
  replaceable, hideable and removable, carrying a monotonic `version()` and a `commit()` that
  publishes edits to the render side.
- **`scene_edit`** — an RAII gateway. Constructing one stops the render, takes the edit lock and
  bumps the version; it is the *only* type through which a mutator can be reached.

Alongside it, `hit_info` gains the three int32 ids §8.3's AOVs read, `instance` learns to stamp
`instance_id`, and `render_buffer.h`/`renderer.h` gain the `primId`/`instanceId`/`elementId` AOVs.
The viewer stops calling `start_render()` from its key handler and starts driving restarts off
`scene::version()` — which makes it a working stand-in for §9's render pass, months before the
render pass exists.

## Why it matters more than "add a `remove()` and a counter" sounds

1. **This is the item that makes the renderer safe to point at a live DCC.** Every other 0.2.0
   item so far has been about producing pixels. This one is about the fact that, in Hydra,
   `Sync()` runs on a threadpool *while a render thread is walking the same data structure*.
   [[interruptible-render-loop]] built the cancellation; this task builds the thing that must be
   cancelled *before*, and makes forgetting it a compile error rather than a race.
2. **There are two different hazards here and only one of them is `StopRender()`.** Edit-vs-render
   is solved by stopping the render. Edit-vs-edit is not: §7.2 says `Sync()` "is called in parallel
   from worker threads", so 484 prims can be inside the gateway at once. hdEmbree gets away with
   nothing but `StopRender()` because `rtcAttachGeometry`/`rtcDetachGeometry` are internally
   threadsafe; a `std::vector` is not. Removing the edit lock from the prototype produced
   **33 ThreadSanitizer data races and an `allocation-size-too-big` abort** — measured below.
3. **`primId`, `instanceId` and `elementId` are not our handles.** `HdRprim::GetPrimId()` is a
   value *Hydra* allocates (`renderIndex.cpp:1928`), and `_CompactPrimIds()` **reassigns every one
   of them** when the 24-bit id space wraps. A scene that conflates "the renderer's handle" with
   "the id the picking AOV reports" is wrong the first time a session churns enough prims. So the
   record stores both, and this is much cheaper to get right while `hit_info` is being edited
   anyway than to retrofit ([[roadmap-discussion-8-26]] §5 item 3).
4. **`commit()` is where the BVH goes.** hdEmbree does not rebuild its acceleration structure per
   edit; it marks the scene dirty and calls `rtcCommitScene(_scene)` once, from the render thread,
   at the top of `Render()` (`renderer.cpp:490`, `_PreRenderSetup`). Putting the same seam in now
   means `bvh with rebuild-on-mutation` is "fill in `scene::commit()`" rather than "work out where
   a rebuild can safely happen". A 484-prim re-Sync is **484 edits and one commit**, not 484
   rebuilds.
5. **The cost of the gateway is entirely in the first acquire.** Measured: acquire #1, against a
   render that is actually running, costs **1.34–1.55 ms**; acquires #2–484 cost **60 ns each**,
   0.030 ms for all 483. The intuition that "stopping the render 484 times per Sync will be
   ruinous" is simply false — after the first one the render thread is already idle.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| A BVH; `commit()` doing anything but rebuilding the draw list | `bvh with rebuild-on-mutation` — Appendix B is the seam |
| `element_id` ever being set to anything but `-1` | `triangle mesh` — the AOV is plumbed and gated now, the value arrives with faces |
| `HdWeekendMesh::Sync()` consuming `GetPrimId`/`GetVisible`/dirty bits; `Finalize()` | `hydra wrapper` (0.3.0) — Appendix A is the sketch, and step 12 compiles the gateway against the real `HdRenderThread` |
| `HdRenderParam` subclass, `HdRenderPass::_Execute`'s five-way change detection | `hydra wrapper` (0.3.0) — step 6 builds the viewer version of item 1 of that list |
| `HdInstancer`, `hydra:instanceTransforms`, nested flattening | 0.4.0 instancing — `instance::instance_id` is the field it will fill ([[transform-support]] Appendix B) |
| An `SdfPath` → handle map inside `scene` | nobody — argued out in "Design notes"; the Rprim holds its own handle, as `HdEmbreeMesh` holds `_rtcMeshId` |
| Render settings, `settingsVersion`, `threadLimit` | `hydra wrapper` (0.3.0), §9/§15 |
| Materials reachable through the scene graph | §13 — hdEmbree has no material support at all |
| Multiple concurrent render passes | not scheduled; hdEmbree does not support them either |
| Any change to `camera.h`, `camera_desc.h`, `mat4.h`, `material.h`, `sphere.h`, `render_control.h`, `rng.h`, `schedulers.h` | nothing — step 14 proves they are untouched |

The tracer stays **USD-free**: no `pxr/` include under `tracer/` or `viewer/`. The `StopRender()`
dependency is injected as a `std::function<void()>`, the same way `tile_scheduler` is injected in
[[interruptible-render-loop]] and for the same reason. Steps 7–13 lean on scratchpad programs —
per the standing rule, **no test code lands in the repo**.

---

## Pre-verified facts

Measured, not assumed. Every program named below exists, was built and was run; the numbers are
its output.

```
S=/tmp/claude-1000/-home-nick-git-weekend-raytracer-docs/<session>/scratchpad
```

| Claim | Measured |
|---|---|
| **Routing every scene through `scene` changes no pixel.** All five scenes, 50 spp, every prim inserted through `scene_edit` | `cmp` **byte-identical** to the goldens, 5/5 |
| `remove(h)` is pixel-equivalent to never inserting the prim | byte-identical over the whole 400×225 frame |
| `set_visible(h, false)` is pixel-equivalent to never inserting the prim | byte-identical |
| Hide then unhide restores the original frame | byte-identical |
| **Dead slots cost nothing.** 484 inserted then 384 removed, vs a scene that only held the surviving 100 | 0.0117–0.0131 vs 0.0112–0.0126 ms/px, and the same image hash |
| A freed handle is reused; slot storage does not grow | `slot_count()` stays 3 across remove+insert |
| One `edit()` scope == exactly one version bump, regardless of how many mutators run inside it | 3 inserts → `version()==1` |
| **484 prims each acquiring the gateway, against a live render** | acquire #1: **1.34–1.55 ms**. Acquires #2–484: **0.030 ms total, 60 ns each** |
| One acquire with the render already idle | **0.2–0.6 µs** |
| **8 threads × 200 parallel `edit()` calls** (what parallel `Sync()` looks like) | 1600 inserts all landed, **no handle issued twice**, `version()==1600` |
| **ThreadSanitizer on the parallel-edit + live-render test** | **0 warnings** (with `singlethread_schedule`; with `tbb_schedule` it reports 137, every one inside uninstrumented `libtbb`) |
| **Negative control — the same test with the edit lock removed** | **33 TSan data races**, several naming `scene_edit::insert` and `scene::record`, plus an `allocation-size-too-big` abort. Exit 66 |
| A miss leaves all three id AOVs at the `-1` clear value | 5603/5603 background pixels, scene 4 at 200×113 |
| Every stamped `prim_id` is the value `set_prim_id` stored, never the handle | 16997/16997 hit pixels; 63 distinct ids, all `≡ 7 (mod 10)` by construction |
| `element_id` is `-1` in every pixel | 22600/22600 — nothing writes it until triangles land |
| **Negative control — `scene::hit` without the stale-id reset** | **2006 of 2006 pixels** of a nearer, un-instanced sphere inherit the farther instance's `instance_id` |
| `commit()` cost | scene 0 (484 prims) **0.0025 ms**; scene 4 (206) 0.0011 ms; 10 000 → 0.052 ms; 100 000 → 0.577 ms; 1 000 000 → **6.52 ms**. Linear, ~6.5 ns/prim |
| `commit()` is idempotent — a second call with no intervening edit does nothing | `draw_count()` unchanged, `_dirty` already false |
| An uncommitted insert is invisible to `hit()` | `draw_count()==0` until `commit()` |
| **`scene` vs `hittable_list`, same binary, interleaved**, scene 0 (484 prims) | 0.0672 vs 0.0697 ms/px median — **+4%** |
| same, scene 4 (206 prims, 200 of them instances) | 0.0710 vs 0.0740 — **+5%** |
| **The enlarged `hit_info` costs nothing measurable** | new-headers `hittable_list` ran 0.0635–0.0744 against the committed build's 0.0735–0.0856 — i.e. *faster*, which is binary-layout luck. Read it as "below the ±10% binary-to-binary noise floor", not as a speedup |
| `sizeof` before → after | `hit_info` **72 → 88**, `instance` 416 → 424, `sphere` 56, `hittable_list` 32, `scene` **176**, `scene_edit` **24** |
| **Every USD symbol the 0.3.0 side needs exists with the assumed shape** | 16/16 assertions pass against `HdAovTokens`, `HdFormatInt32`, `HdChangeTracker`, `HdRenderThread` |
| hdEmbree's id AOV descriptor | `HdAovDescriptor(HdFormatInt32, false, VtValue(-1))` for all three (`renderDelegate.cpp:255-258`) — exactly our `{int32, false, 1, {-1}}` |
| `HdChangeTracker::DirtyPrimID` | `1 << 2`; **absent** from hdEmbree's initial dirty mask (§7.3) |

Four of those decide the shape of the task.

**The byte-identity gate is real and it is strict.** Five scenes, 484 prims in the largest, every
prim routed through a new container with a new hit loop and a 16-byte-larger `hit_info` — and not
one of 270 000 pixels moves, in any scene. So **any pixel difference in step 7 is a bug you
introduced**, not float noise.

**Both negative controls fire hard, and neither fires subtly.** Drop the edit lock and TSan
produces 33 races and an abort — not an occasional flake. Drop the stale-id reset and *every single
pixel* of the near sphere reports the wrong `instance_id` — not one in a thousand. These two
defects are the ones this design exists to prevent, and both are cheap to reproduce, so reproduce
them: a gate you have never seen fail is a gate you do not know is wired up.

**The gateway is not expensive, and the reason matters.** 1.4 ms for the first acquire, 60 ns for
each of the next 483. `StopRender()` is only slow when there is a render to stop. This is the
number that says a per-prim gateway — one acquire inside every `Sync()`, as §6 requires — is the
right design rather than a batched "stop once, edit everything, restart" that would have to be
threaded through the delegate by hand.

**`commit()` is linear and trivial today, and that is the point.** 2.5 µs for 484 prims. It is not
carrying its weight yet; it is carrying the *seam*. A million prims commit in 6.5 ms, which is the
budget the BVH will spend three orders of magnitude of — once per render start rather than once per
edit.

Build lines that work on this machine:

```bash
cd ~/git/weekend-raytracer
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# scratchpad copies of the headers, so the tracer tree stays clean.
# NOTE: quoted includes resolve against the *including file's* directory first,
# so example_scenes.h in tracer/ would pull tracer/hittable_list.h no matter
# what -I says. Copy every header into $S/inc and compile with only that.
mkdir -p $S/inc && cp tracer/*.h $S/inc/     # then edit $S/inc/*.h

g++ -std=c++17 -O3 -DNDEBUG -I$S/inc -Ibuild/_deps/tbb-src/include \
    -o $S/t_scene $S/t_scene.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb

# no scheduler needed for commit-only timing
g++ -std=c++17 -O3 -DNDEBUG -I$S/inc -o $S/t_commit $S/t_commit.cpp

# ThreadSanitizer. -fPIE -pie + setarch -R are both required here: without them
# tsan dies with "unexpected memory mapping" before main().
g++ -std=c++17 -O1 -g -fsanitize=thread -fPIE -pie -I$S/inc \
    -Ibuild/_deps/tbb-src/include -o $S/t_tsan $S/t_thread_st.cpp \
    -Lbuild/gnu_13.3_cxx11_64_release -ltbb -pthread
setarch -R $S/t_tsan

# USD-linked. -lusd_python is needed or pxr_boost::python::throw_error_already_set
# fails to resolve out of libusd_python.so.
source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/t_usd $S/t_usd.cpp -L$USD_ROOT/lib \
    -lusd_hd -lusd_tf -lusd_sdf -lusd_vt -lusd_gf -lusd_python -ltbb -lpython3.12
```

---

## The design in one page

```
tracer/scene.h           NEW      class scene : public hittable  +  class scene_edit.
                                  Handles, versioning, visibility, the draw list, the gateway.
                                  ~170 lines.

tracer/hittable.h        EDIT     hit_info gains prim_id / instance_id / element_id (int32, -1).
                                  hittable gains `virtual void commit() {}`.

tracer/hittable_list.h   EDIT     stale-id reset in hit(); commit() forwards to children.

tracer/instance.h        EDIT     public int32_t instance_id = -1, stamped in hit().
                                  commit() forwards to the prototype.

tracer/render_buffer.h   EDIT     aov::prim_id / instance_id / element_id + their descriptors
                                  ({int32, not multisampled, clear -1}).

tracer/renderer.h        EDIT     render() takes `hittable &` and calls world.commit() first.
                                  Three new AOV cases.

tracer/example_scenes.h  EDIT     scenes build through a scene_edit& instead of a hittable_list&.

tracer/main.cpp          EDIT     builds a scene.

viewer/main.cpp          EDIT     builds a scene; restarts off scene::version() instead of from
                                  the key handler; TAB/H/X edit the live scene.

tracer/camera.h          UNTOUCHED
tracer/camera_desc.h     UNTOUCHED
tracer/mat4.h            UNTOUCHED
tracer/material.h        UNTOUCHED
tracer/sphere.h          UNTOUCHED
tracer/render_control.h  UNTOUCHED   (the stop callback is injected, not abstracted here)
tracer/rng.h             UNTOUCHED
tracer/schedulers.h      UNTOUCHED
```

### Two hazards, two mechanisms, one gateway

This is the whole argument for the design, and it is the thing to re-read if any of it later looks
like ceremony.

| Hazard | When | Mechanism | Evidence it is needed |
|---|---|---|---|
| An edit mutates `_slots` while the render thread is inside `scene::hit` | any prim edit during an interactive render | **`stop_render()`**, called first, blocking | §6, §10.1; `StopRender()` is threadsafe and callable from any Hydra thread |
| Two `Sync()` calls mutate `_slots` concurrently | always — §7.2, `Sync()` runs on the threadpool | **`_edit_lock`**, held for the life of the `scene_edit` | negative control: 33 TSan races + an abort without it |
| A prim inherits the previous prim's `instance_id`/`element_id` | any frame with a mix of instanced and plain prims | **reset in the success branch of `hit()`** | negative control: 2006/2006 pixels wrong without it |
| The render pass keeps rendering a scene that changed | every edit | **`version()`**, compared by the pass | §9 item 1 |
| The draw list is stale after an edit | every edit | **`commit()`**, from the render thread | `draw_count()==0` until committed |

Both of the first two are taken by `scene_edit`'s *constructor*, and every mutator lives on
`scene_edit`, so there is no reachable path that mutates without both. That is what §6 means by
"so that stopping the render before an edit cannot be forgotten".

### The id-stamping rule

There is exactly one rule, and everything else follows from it:

> **A hittable may write `hit_info`'s ids only on a successful hit.**

`instance` already satisfies it — it stamps after `proto->hit()` returns true. `sphere` never
writes them. Given that rule, a stale id can only be created by a *successful* hit that is later
beaten by a closer one, so resetting in the success branch of the traversal loop is sufficient —
and successes are rare (a handful per ray) where prim tests are not (484 per ray, today).

Resetting before every prim test instead would also be correct, and would cost two stores per prim
per ray on the hottest loop in the renderer. Don't.

### Who owns what, after this task

| Decision | Owner |
|---|---|
| Which prims exist, and their handles | `scene`, mutated only through `scene_edit` |
| When the render is stopped for an edit | `scene_edit`'s constructor, via the injected `stop_render` |
| *How* the render is stopped | the caller — the viewer's join, `HdRenderThread::StopRender` in 0.3.0 |
| Serialising concurrent edits | `scene::_edit_lock` |
| When the draw list is rebuilt | `renderer::render`, via `world.commit()` |
| Whether to restart the render | the render loop's owner, by comparing `scene::version()` |
| The value of `prim_id` | Hydra (`HdRprim::GetPrimId()`); the scene only stores it |
| The value of `instance_id` | the `instance` wrapper; §14's instancer will set it |
| The value of `element_id` | nobody yet; the triangle intersector, once `triangle mesh` lands |

---

# Step 0 — Capture goldens before you touch anything

The gate for the existing scenes is byte equality, so the goldens must come from the current build.

```bash
cd ~/git/weekend-raytracer
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
S=/tmp/claude-1000/.../scratchpad          # your scratchpad

for i in 0 1 2 3 4; do ./build/tracer/Release/tracer_cli $i > $S/gold_$i.ppm; done
md5sum $S/gold_*.ppm | tee $S/gold.md5
```

Measured here — the first four unchanged since [[interruptible-render-loop]] step 0, the fifth
unchanged since [[transform-support]], which is itself worth knowing:

```
3292e039125ee04d7f4728ad9d89886f  gold_0.ppm
81978695472eb949e987e46fefe3e694  gold_1.ppm
418151b864772683d18aef594a1651b7  gold_2.ppm
57e57b71e5501b5f278b60a73793b64c  gold_3.ppm
297533cce4fcb5d116e82b2322a6308d  gold_4.ppm
```

Take a warm timing baseline for step 11 as well — run scene 0 **four times** and keep the last
three; the first run on this machine is consistently faster than the fourth, so absolute numbers
across sessions mean nothing and only interleaved comparisons do:

```bash
for i in 1 2 3 4; do ./build/tracer/Release/tracer_cli 0 >/dev/null; done
# here: 0.0735 / 0.0759 / 0.0756 ms/px
```

Then set up the scratchpad header tree. Do this once and edit only inside it until step 14:

```bash
mkdir -p $S/inc && cp tracer/*.h $S/inc/
```

---

# Step 1 — `hit_info` grows three ids, `hittable` grows `commit()`

`tracer/hittable.h`, in full:

```cpp
#ifndef HIITABLE_H
#define HIITABLE_H

#include <cstdint>

#include "tracer.h"
class material;

class hit_info
{
public:
  point3 p;
  vec3 normal;
  double t;
  bool front_face;
  const material *mat = nullptr;

  // Ids for the int32 AOVs (spec 8.3). -1 == "not set", which is also the
  // AOV's clear value, so a miss and an unstamped hit read the same.
  //
  // prim_id is HdRprim::GetPrimId() - Hydra's, not our handle. instance_id is
  // stamped by `instance`. element_id waits for triangles.
  int32_t prim_id = -1;
  int32_t instance_id = -1;
  int32_t element_id = -1;

  void set_face_normal(const ray &r, const vec3 &outward_normal)
  {
    // outward should be normallzed
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

class hittable
{
public:
  virtual ~hittable() = default;

  virtual bool hit(const ray &r, interval clipping_range, hit_info &info) const = 0;

  // Apply whatever a mutation invalidated. Called once, from the render
  // thread, at the top of renderer::render - the same place hdEmbree calls
  // rtcCommitScene (renderer.cpp:490, _PreRenderSetup). Must be idempotent and
  // must be cheap when nothing changed: scene::commit() forwards it to every
  // live prim, so a prototype shared by 200 instances gets called 200 times.
  virtual void commit() {}
};

#endif
```

Two things worth being deliberate about.

**`hit_info` goes from 72 to 88 bytes** (measured), and it is copied on every accepted hit in
every traversal loop. That is a real cost on paper and it did not show up in any measurement —
see step 11 and the noise-floor caveat in "Pre-verified facts". Do not try to pack the ids into
spare bits of something else.

**`commit()` is on `hittable`, not on `scene`.** The alternative — `renderer::render` taking
`scene&` — is rejected in "Design notes". The consequence to remember here is the idempotence
requirement in the comment: forwarding means redundant calls, and every implementation must make
them free.

---

# Step 2 — Write `tracer/scene.h`

The whole file:

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "hittable.h"
#include "tracer.h"

// Stable handle into a scene. An Rprim holds one for its whole life, the way
// HdEmbreeMesh holds _rtcMeshId. -1 is "not in a scene".
using prim_handle = int;
inline constexpr prim_handle null_prim = -1;

class scene_edit;

// The renderable scene: prims addressable by handle, insertable, replaceable,
// hideable and removable, carrying a monotonic version the render pass compares
// against (spec 6).
//
// Mutation is reachable only through edit(), which stops the render and takes
// the edit lock first. hit() and commit() are the render thread's side and take
// no lock: they are only ever reached while no edit is running.
class scene : public hittable
{
public:
  // Bound to HdRenderThread::StopRender by the delegate; the cli leaves it
  // empty. Blocking: it must return with the render thread idle.
  void set_stop_render(std::function<void()> stop) { _stop_render = std::move(stop); }

  // The only way to mutate. See scene_edit below.
  scene_edit edit();

  // Monotonic, bumped once per edit() scope. The render pass restarts when this
  // changes (spec 9, item 1).
  uint64_t version() const { return _version.load(std::memory_order_acquire); }

  size_t size() const { return _live; }              // live prims
  size_t slot_count() const { return _slots.size(); }  // live + freed
  size_t draw_count() const { return _draw.size(); }   // live and visible, as of the last commit

  // Rebuild the draw list if an edit invalidated it, and propagate downward.
  // Called from the render thread at the top of renderer::render.
  void commit() override
  {
    if (!_dirty.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    _draw.clear();
    for (const record &r : _slots) {
      if (r.prim == nullptr) {
        continue;                 // a freed slot
      }
      r.prim->commit();           // propagate: an instance's prototype, a mesh's bvh
      if (r.visible) {
        _draw.push_back({r.prim.get(), r.prim_id});
      }
    }
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    hit_info temp_info;
    bool did_hit = false;
    double closest = clipping_range.max;

    for (const entry &e : _draw) {
      if (e.prim->hit(r, interval(clipping_range.min, closest), temp_info)) {
        did_hit = true;
        closest = temp_info.t;
        temp_info.prim_id = e.prim_id;
        info = temp_info;

        // Clear the ids a nested hittable stamped, so the next prim cannot
        // inherit them. A hittable only writes an id on a successful hit, so
        // this is the only place a stale one can be created - and successes
        // are rare where prim tests are not.
        temp_info.instance_id = -1;
        temp_info.element_id = -1;
      }
    }

    return did_hit;
  }

private:
  friend class scene_edit;

  struct record {
    shared_ptr<hittable> prim;   // null == free slot
    int32_t prim_id = -1;        // HdRprim::GetPrimId(), NOT the handle
    bool visible = true;
  };

  // The compacted, render-facing view. Rebuilt by commit(), never read during
  // an edit. Raw pointers: the record owns the reference.
  struct entry {
    const hittable *prim;
    int32_t prim_id;
  };

  std::vector<record> _slots;
  std::vector<prim_handle> _free;
  std::vector<entry> _draw;
  size_t _live = 0;

  std::function<void()> _stop_render;
  std::mutex _edit_lock;
  std::atomic<uint64_t> _version{0};
  std::atomic<bool> _dirty{false};
};

// RAII scene-edit gateway - spec 6's AcquireSceneForEdit, with the lock and the
// version bump folded in. Construction stops the render, takes the edit lock
// and bumps the version; destruction releases the lock. Every mutator lives
// here, so none of the three can be forgotten.
class scene_edit
{
public:
  scene_edit(const scene_edit &) = delete;
  scene_edit &operator=(const scene_edit &) = delete;
  scene_edit(scene_edit &&) = default;

  prim_handle insert(shared_ptr<hittable> prim)
  {
    prim_handle h;
    if (!_s->_free.empty()) {
      h = _s->_free.back();
      _s->_free.pop_back();
    } else {
      h = prim_handle(_s->_slots.size());
      _s->_slots.emplace_back();
    }

    _s->_slots[h] = {std::move(prim), -1, true};
    _s->_live++;
    return h;
  }

  // DirtyPoints / DirtyTopology: swap the geometry, keep the handle.
  void set_prim(prim_handle h, shared_ptr<hittable> prim) { _s->_slots[h].prim = std::move(prim); }
  // DirtyPrimID: Hydra's picking id, which _CompactPrimIds() may reassign.
  void set_prim_id(prim_handle h, int32_t id) { _s->_slots[h].prim_id = id; }
  // DirtyVisibility: hdEmbree's rtcEnableGeometry / rtcDisableGeometry.
  void set_visible(prim_handle h, bool v) { _s->_slots[h].visible = v; }

  // Finalize(): drop the prim and free the slot for reuse.
  void remove(prim_handle h)
  {
    if (h == null_prim || _s->_slots[h].prim == nullptr) {
      return;
    }
    _s->_slots[h] = {};
    _s->_free.push_back(h);
    _s->_live--;
  }

private:
  friend class scene;

  explicit scene_edit(scene &s) : _s(&s)
  {
    if (s._stop_render) {
      s._stop_render();                                    // blocks until the render is idle
    }
    _lock = std::unique_lock<std::mutex>(s._edit_lock);    // serialises parallel Sync()
    s._version.fetch_add(1, std::memory_order_acq_rel);
    s._dirty.store(true, std::memory_order_release);
  }

  scene *_s;
  std::unique_lock<std::mutex> _lock;
};

inline scene_edit scene::edit() { return scene_edit(*this); }
```

Points that are load-bearing and easy to lose in transcription:

- **The stop happens before the lock, not after.** `stop_render()` may block for milliseconds;
  holding the edit lock across it would serialise every other prim's `Sync()` behind one thread's
  render stop for no reason. Stop first, then contend.
- **`_dirty` is set inside the constructor, not by the individual mutators.** An `edit()` scope
  that mutates nothing still marks the scene dirty and still bumps the version. That is
  deliberate: it matches hdEmbree (`AcquireSceneForEdit` bumps unconditionally), and the
  alternative — inferring dirtiness from which mutators ran — is exactly the kind of bookkeeping
  that eventually misses one.
- **`insert()` assigns `prim_id = -1`, not the handle.** Step 9 is the gate for that.
- **`remove()` on an already-free handle is a no-op, but the version still moved** — the bump
  happened in the constructor. Verified in step 8.
- **`_draw` holds raw pointers.** They are only valid between a `commit()` and the next edit, and
  the record holds the owning `shared_ptr` the whole time. This is what buys the ~4% rather than
  ~15%: no refcount traffic on the hot loop.
- **`version()` is `uint64_t`, not hdEmbree's `int`.** See "Design notes" — an `int` is genuinely
  reachable in a long interactive session.

---

# Step 3 — Stamp the ids

Two small edits.

**`tracer/instance.h`** — add the field, stamp it, and forward `commit()`:

```cpp
   const mat4 &object_to_world() const { return xform; }

+  // Stamped into hit_info for the instanceId AOV. -1 == "not instanced", which
+  // is the AOV's clear value. Set by the HdInstancer path in 0.4.0.
+  int32_t instance_id = -1;
+
+  void commit() override { proto->commit(); }
+
   bool hit(const ray &r, interval clipping_range, hit_info &info) const override
   {
     ...
     info.p = r.at(info.t);
     info.normal = unit_vector(inv_t.transform_dir(info.normal));
+    info.instance_id = instance_id;
     return true;
   }
```

`#include <cstdint>` at the top. Note the stamp is *after* the early `return false`, which is what
makes the "only write ids on a successful hit" rule hold — do not move it.

**`tracer/hittable_list.h`** — the same stale-id reset as `scene::hit`, plus forwarding. It matters
here because a triangle mesh will be a `hittable_list` of faces and `element_id` will have exactly
the same hazard:

```cpp
   void clear() { objects.clear(); }

+  void commit() override
+  {
+    for (const auto &obj : objects) {
+      obj->commit();
+    }
+  }
+
   bool hit(...) const override {
     ...
       if (obj->hit(r, interval(clipping_range.min, closest), temp_info)) {
         did_hit = true;
         closest = temp_info.t;
         info = temp_info;
+        temp_info.instance_id = -1;
+        temp_info.element_id = -1;
       }
```

`hittable_list` does not stamp `prim_id`: it has no identity to stamp. A `hittable_list` used as a
prim inside a `scene` gets the scene's `prim_id` on the way out, which is correct — the whole group
is one Rprim.

---

# Step 4 — The three id AOVs

**`tracer/render_buffer.h`**, at the bottom:

```cpp
-enum class aov { color, depth, camera_depth, normal, n_eye };
+enum class aov { color, depth, camera_depth, normal, n_eye, prim_id, instance_id, element_id };
```

and in `default_aov_descriptor`:

```cpp
     case aov::n_eye:        return {buffer_format::float32_vec3, false, 3, {-1, -1, -1, 0}};
+    case aov::prim_id:
+    case aov::instance_id:
+    case aov::element_id:   return {buffer_format::int32,         false, 1, {-1, 0, 0, 0}};
```

That is a transcription of `hdEmbree/renderDelegate.cpp:255-258`,
`HdAovDescriptor(HdFormatInt32, false, VtValue(-1))`, verified in step 12. `clear_value` is a
`float[4]` and the comment already there — "ids are small enough to be exact in float" — is what
makes `-1` survive the conversion; [[render-target]] anticipated this exact case.

**`tracer/renderer.h`** — three additions.

The signature, because `commit()` is not const:

```cpp
-  render_stats render(const camera &cam, const hittable &world, const aov_bindings &aovs, ...)
+  render_stats render(const camera &cam, hittable &world, const aov_bindings &aovs, ...)
```

`render_tiles` stays `const hittable &`. Nothing else in the repo passes a const world, so this is
source-compatible.

The commit, immediately after the sample counter is reset:

```cpp
     _completed_samples.store(0);
+
+    // Apply whatever the last edit invalidated, on the render thread, before
+    // any tile work. hdEmbree does the same with rtcCommitScene in
+    // _PreRenderSetup (renderer.cpp:490).
+    world.commit();
```

And three cases in the AOV switch, before `case aov::n_eye`:

```cpp
              case aov::prim_id:
                {
                  if (!did_hit) break;
                  const int32_t id = hit_info.prim_id;
                  buffer.write(x, by, 1, &id);

                  break;
                }

              case aov::instance_id:
                {
                  if (!did_hit) break;
                  const int32_t id = hit_info.instance_id;
                  buffer.write(x, by, 1, &id);

                  break;
                }

              case aov::element_id:
                {
                  if (!did_hit) break;
                  const int32_t id = hit_info.element_id;
                  buffer.write(x, by, 1, &id);

                  break;
                }
```

Three details, each of which is already handled by machinery built in earlier tasks:

- **`if (!did_hit) break;` is the whole "clear to -1" mechanism.** A miss writes nothing and the
  buffer keeps its clear value. That is exactly what hdEmbree's `_ComputeId` does by returning
  `false` (`renderer.cpp:867-880`), and it is the same shape as the existing `depth` and `normal`
  cases.
- **`buffer.write(x, by, 1, &id)` resolves to the `int32_t` overload** already present in
  `render_buffer.h`. Do not cast to float.
- **Id AOVs are not multisampled**, so `render_tiles`'s existing
  `if (!first_ever_sample && !buffer.is_multisampled()) continue;` already means they are written
  once per pixel and never averaged. Nothing new is needed; averaging ids would be nonsense.

---

# Step 5 — Scenes and the cli build a `scene`

**`tracer/example_scenes.h`** — mechanical. Change every scene's first parameter from
`hittable_list &world` to `scene_edit &world`, change every `world.add(` to `world.insert(`, and
make `load_scene` own the edit scope:

```cpp
-#include "hittable_list.h"
+#include "scene.h"

-void scene_1(hittable_list &world, camera_desc &camera)
+void scene_1(scene_edit &world, camera_desc &camera)
 {
   ...
-  world.add(make_shared<sphere>(vec3(0, -1000, 0), 1000, m_ground));
+  world.insert(make_shared<sphere>(vec3(0, -1000, 0), 1000, m_ground));
```

```cpp
-inline void load_scene(int i, hittable_list &world, camera_desc &camera)
+inline void load_scene(int i, scene &world, camera_desc &camera)
 {
+  scene_edit edit = world.edit();
   switch (i) {
-  case 0: scene_1(world, camera); break;
+  case 0: scene_1(edit, camera); break;
   ...
   }
 }
```

One whole-scene load is **one** `edit()` scope and therefore one version bump — which is right:
loading a scene is one edit, not 484.

While you are in there, `load_scene`'s `case 4:` is missing its `break` ([[transform-support]]
verification log). Add it.

**`tracer/main.cpp`** — two lines:

```cpp
-#include "hittable_list.h"
+#include "scene.h"
...
-  hittable_list world;
+  scene world;
```

Nothing else in the cli changes: `r.render(cam, world, aovs)` binds to the non-const overload, and
`render()` commits before the first tile.

---

# Step 6 — The viewer becomes a render-pass stand-in

This is the step that earns the task its keep before 0.3.0 exists. §9's `_Execute` is five change
detectors and a conditional `StartRender()`; the viewer can implement item 1 of that list today,
against the real `scene::version()`, and the shape carries straight over.

Replace the container and wire the stop:

```cpp
-#include "hittable_list.h"
+#include "scene.h"
...
-  hittable_list world;
+  scene world;
   camera_desc desc;
   load_scene(i_scene, world, desc);
```

`stop_render` and `start_render` already exist and already have the right semantics. Bind the
first one into the scene, **after** the lambda is defined and **before** any edit:

```cpp
   auto start_render = [&]() { ... };

+  // This is the wiring HdRenderParam does: the gateway stops the render before
+  // it hands back a mutable scene (spec 6).
+  world.set_stop_render(stop_render);
+
+  // The render pass's job (spec 9, item 1): notice the scene changed, restart.
+  uint64_t last_version = world.version();
```

Then, at the top of the main loop, before the resolve/blit:

```cpp
+    const uint64_t version = world.version();
+    if (version != last_version)
+    {
+      last_version = version;
+      start_render();
+    }
```

And the key handlers stop calling `start_render()` themselves. They only edit; the loop notices.

```cpp
+          case SDLK_TAB:      // cycle which prim the edit keys act on
+          {
+            selected = (selected + 1) % int(handles.size());
+            SDL_SetWindowTitle(window, ("prim " + std::to_string(selected)).c_str());
+            break;
+          }
+          case SDLK_H:        // DirtyVisibility
+          {
+            visible[selected] = !visible[selected];
+            auto edit = world.edit();
+            edit.set_visible(handles[selected], visible[selected]);
+            break;
+          }
+          case SDLK_X:        // Finalize
+          {
+            auto edit = world.edit();
+            edit.remove(handles[selected]);
+            break;
+          }
```

`handles` and `visible` are viewer-local and need no change to `load_scene`. Immediately after
loading, nothing has been removed, so the handles `load_scene` issued are exactly
`0 .. world.size()-1` — `insert` fills slots densely from zero and the free list is empty:

```cpp
  std::vector<prim_handle> handles(world.size());
  std::iota(handles.begin(), handles.end(), 0);
  std::vector<bool> visible(handles.size(), true);
  int selected = 0;
```

That identity holds *only* at load time, which is why it is computed once here and not
recomputed later — `X` frees slots, and after the first removal the two sets diverge. Returning
the handles out of `load_scene` would be more honest and would churn all six scene signatures for
a viewer convenience; if the viewer ever needs to re-enumerate, that is the point to do it.

Two things this arrangement gets right that a `start_render()` call in the key handler does not:

- **The edit and the restart are decoupled**, which is what Hydra actually does. `Sync()` edits;
  `_Execute()` restarts. Nothing in the edit path knows a renderer exists.
- **N edits in one frame cause one restart.** Hold `H` down and the version moves several times
  between two loop iterations; the viewer restarts once. hdEmbree gets the same property for free
  from the same comparison.

Note one deliberate difference from hdEmbree, recorded so it is a decision rather than an
accident: our `start_render()` **clears the buffer**. hdEmbree calls `Clear()` only in the
AOV-bindings branch of `_Execute` (`renderPass.cpp:213`); on a scene-version change it calls
`MarkAovBuffersUnconverged()` and `StartRender()` and nothing else, so samples taken before the
edit stay in the accumulation buffer. Clearing is the behaviour you want in a viewer. Whether the
delegate should match hdEmbree or match us is a 0.3.0 decision; see "Design notes".

---

# Step 7 — GATE 1: the five scenes do not move

The strict one. Write `$S/t_scene.cpp`: for each scene, load into a `hittable_list` the old way,
insert every object into a `scene` through one `edit()`, render at 50 spp with the tbb scheduler
and the same 400×225 camera the cli uses, and write a `.ppm` with the cli's exact writer.

```bash
g++ -std=c++17 -O3 -DNDEBUG -I$S/inc -Ibuild/_deps/tbb-src/include \
    -o $S/t_scene $S/t_scene.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
$S/t_scene $S/out
for i in 0 1 2 3 4; do cmp $S/gold_$i.ppm $S/out/scene_$i.ppm && echo "scene $i IDENTICAL"; done
```

Expected, exactly as measured:

```
scene 0: prims=484 live=484 draw=484 version=1
scene 1: prims=5 live=5 draw=5 version=1
scene 2: prims=5 live=5 draw=5 version=1
scene 3: prims=5 live=5 draw=5 version=1
scene 4: prims=206 live=206 draw=206 version=1
sizeof hit_info=88 scene=176 instance=424
scene 0 IDENTICAL
scene 1 IDENTICAL
scene 2 IDENTICAL
scene 3 IDENTICAL
scene 4 IDENTICAL
```

`version=1` on every line is not decoration: it is the check that a whole scene load is one edit
scope. `draw` equal to `live` is the check that `commit()` ran and dropped nothing.

If a scene differs, the two things to suspect first are the traversal loop's `closest` handling
(the `interval(clipping_range.min, closest)` argument must be rebuilt each iteration, not hoisted)
and an accidental reordering of `_draw` relative to insertion order. Insertion order does not
affect a correct closest-hit search, but if it changes the image, the search is not correct.

---

# Step 8 — GATE 2: mutation semantics

`$S/t_mutate.cpp`. Eighteen assertions in four groups; all of them passed as written.

**Handles, free list, version.** Insert three prims in one edit; assert handles `0,1,2`,
`version()==1`, `size()==3`. Remove the middle one; assert `version()==2`, `size()==2`. Insert a
fourth; assert it *reuses* handle 1 and `slot_count()` is still 3. Remove everything, commit,
assert `draw_count()==0` and that a ray into the empty scene misses. Remove an already-free handle
and assert the version still moved.

**`commit()` is load-bearing.** Insert without committing; assert `draw_count()==0` — the prim is
genuinely invisible to `hit()`. Commit; assert 1. Commit again with no edit; assert still 1 and
that nothing was rebuilt.

**Removal and visibility are pixel-equivalent to never inserting.** Build three scenes from
scene 1's five prims: a *reference* holding four of them, a *removed* holding all five then
`remove`ing the fourth, and a *hidden* holding all five with the fourth `set_visible(false)`.
Render all three at 50 spp and compare the resolved 8-bit bytes.

```
remove(h) == never inserting it, byte for byte             ok
set_visible(h,false) == never inserting it, byte for byte  ok
both draw 4 of 5 prims                                     ok
```

This is a stronger statement than "the prim disappeared". A prim that were merely skipped
*somewhere* — say, still tested but rejected — would change nothing visible and pass an eyeball
test, while still costing what the compaction was supposed to save. Byte equality against a scene
that never held it is the version of the claim that has teeth.

**Round trips.** Hide then unhide restores the original bytes; `set_prim(h, same_prim)` changes
nothing. Both matter because `DirtyVisibility` and `DirtyPoints` arrive repeatedly during a
viewport drag and must be idempotent.

Expected tail:

```
ALL PASS (0 failures)
```

---

# Step 9 — GATE 3: the id AOVs, and the stale-id negative control

`$S/t_ids.cpp`, at 200×113 and 4 spp with `color`, `prim_id`, `instance_id` and `element_id` bound.

**Scene 4.** Insert all 206 prims, then `set_prim_id(h, i*10 + 7)` — a numbering that can never be
confused with a handle — and set `instance_id` on each `instance` to its index. Measured:

```
  background=5603 hit=16997 distinct prim_id=63 distinct instance_id=63
the frame has both background and geometry                     ok
a miss leaves all three id AOVs at the -1 clear value          ok
every stamped prim_id is the value set_prim_id stored          ok
every visible prim in scene 4 is instanced (no -1)             ok
one distinct instance_id per distinct prim_id                  ok
instanced prims report distinct instance_ids                   ok
element_id is -1 everywhere - no triangles yet                 ok
```

Two of those need a note. "*every visible prim in scene 4 is instanced*" is true because the
`scale(100, 0.01, 100)` slab added in [[transform-support]] wholly occludes the ground sphere —
the only non-instanced prim in the scene. Do not "fix" a failure of this check by loosening it;
check whether the slab moved. And "*every stamped prim_id ≡ 7 (mod 10)*" is the check that a
handle never leaks into the AOV: handles here run `0..205`, so a leak would show up immediately.

**The stale-id negative control.** A two-prim scene: a large instanced backdrop at `z = -8` with
`instance_id = 42`, and a plain, un-instanced `sphere` in front of it at `z = -2`. Count the
pixels whose `prim_id` is the sphere's and whose `instance_id` is not `-1`.

```
  near prim pixels=2006 (instance_id leaked into 0)  far pixels=20594
a nearer un-instanced prim does not inherit the farther instance's id  ok
```

Now delete the two reset lines from `scene::hit` into `$S/inc_noreset/` and rebuild the same
program against it:

```
  near prim pixels=2006 (instance_id leaked into 2006)  far pixels=20594
a nearer un-instanced prim does not inherit the farther instance's id  FAIL
```

**2006 of 2006.** Every pixel, not a flake. Restore the reset and move on — but run the control
once, because this is the gate most likely to be silently absent.

---

# Step 10 — GATE 4: threading, and the lock's negative control

`$S/t_thread.cpp`. Two halves.

**Parallel edits.** Eight threads, 200 `edit()` scopes each, inserting a sphere per scope. This is
what §7.2's "`Sync()` is called in parallel from worker threads" looks like when every prim reaches
the gateway.

```
8 threads x 200 parallel edits: every insert landed            ok
no handle was issued twice                                     ok
version == number of edit() scopes                             ok
one commit publishes all of them                               ok
```

**Edits against a live render.** Load scene 0, start a real render thread with
`samples_to_converge = 100000` so it never finishes, bind `stop_render` into the scene, let it run
until `completed_samples() > 0`, then acquire the gateway 484 times — once per prim, as a full
re-Sync would.

```
the render is genuinely running before we edit                 ok
  acquire #1 (stops a running render): 1.5535 ms
  acquires #2-484 (render already idle): 0.0302 ms total, 0.00006 ms each
  one acquire with the render already idle: 0.0003 ms
one version bump per acquire, none missed                      ok
removing a prim mid-render bumps the version                   ok
the render thread stopped without a crash                      ok
and the restarted render makes progress                        ok
the restarted render committed the removal                     ok
```

The first line of that block is not optional. Without it the whole timing is a measurement of
stopping a render that was already stopped, which is the 60 ns number, and the interesting one
(1.4 ms) never gets measured at all.

That 1.4 ms is larger than the 0.05–0.16 ms [[interruptible-render-loop]] step 9 measured for
`StopRender()`, and the difference is the harness, not the design: this stop is a `std::thread`
join — full thread teardown — where `HdRenderThread` parks the thread on a condition variable and
keeps it alive. Expect the delegate to be faster than this, not slower.

**ThreadSanitizer.** Run the same program with `singlethread_schedule` substituted for
`tbb_schedule` and a smaller frame (80×45) so the liveness checks still pass at TSan's ~100×
slowdown:

```bash
g++ -std=c++17 -O1 -g -fsanitize=thread -fPIE -pie -I$S/inc \
    -Ibuild/_deps/tbb-src/include -o $S/t_tsan $S/t_thread_st.cpp \
    -Lbuild/gnu_13.3_cxx11_64_release -ltbb -pthread
setarch -R $S/t_tsan
```

```
tsan warnings: 0
ALL PASS (0 failures)
```

Use `singlethread_schedule` deliberately. With `tbb_schedule` the same run reports **137**
warnings, every one of them inside `libtbb.so` — TSan cannot see oneTBB's internal
synchronisation, so every task handoff looks like a race. Those are noise, and wading through them
to find a real one is exactly how a real one gets missed. The scheduler is injected precisely so
it can be swapped out for this.

**The negative control.** Copy the headers to `$S/inc_nolock/` and delete the one line that takes
the lock:

```
tsan warnings: 33
SUMMARY: ThreadSanitizer: data race .../inc_nolock/scene.h:131 in scene_edit::insert(...)
SUMMARY: ThreadSanitizer: data race .../inc_nolock/scene.h:86 in scene::record::operator=(...)
SUMMARY: ThreadSanitizer: allocation-size-too-big in operator new(unsigned long)
exit=66
```

The `allocation-size-too-big` is the vector's size field being torn by two concurrent
`emplace_back`s. That is the failure this lock exists to prevent, and it aborts rather than
corrupting quietly — which is luck, not design.

---

# Step 11 — GATE 5: performance

Three questions, three measurements. All of them must be **interleaved** — alternate old and new
within a single loop — because absolute ms/px on this machine drifts ~10% between runs.

**1. What does `scene` cost over `hittable_list`?** Build one binary that can render either, so
the comparison does not cross a binary boundary:

```bash
g++ -std=c++17 -O3 -DNDEBUG -DWITH_SCENE -I$S/inc -Ibuild/_deps/tbb-src/include \
    -o $S/t_perf $S/t_perf.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
for r in 1 2 3 4 5 6; do echo "B=$($S/t_perf 0 0) C=$($S/t_perf 1 0)"; done
```

```
scene 0 (484 prims):  list 0.0672   scene 0.0697   +4%
scene 4 (206 prims):  list 0.0710   scene 0.0740   +5%
```

Four to five percent, and it buys handles, removal, visibility, versioning and the `prim_id`
stamp. Accept it. It is also the wrong number to optimise: `hittable_list::hit` and `scene::hit`
are both linear scans, and `bvh with rebuild-on-mutation` deletes the scan entirely.

**2. Does the enlarged `hit_info` cost anything?** This one *does* cross a binary boundary — the
old `hit_info` only exists in the old headers — so it can only ever produce a bound, not a figure.
The bound is comfortable:

```
committed build, hittable_list, old 72-byte hit_info :  0.0735 - 0.0856 ms/px
new headers,     hittable_list, new 88-byte hit_info :  0.0635 - 0.0744 ms/px
```

The new one is *faster*, which it has no business being. That is code layout, not a speedup; the
honest reading is "the 16 bytes are below the ±10% binary-to-binary noise floor". Do not record it
as a win, and do not let a future 5% regression hide behind it.

**3. Do dead slots cost anything?** `$S/t_holes.cpp` renders scene 0's first 100 prims two ways: a
scene that only ever held those 100, and a scene that held all 484 and then removed 384.

```
  pristine: 0.01237 slots=100 live=100 draw=100 hash=10660871210577927969
  churned : 0.01203 slots=484 live=100 draw=100 hash=10660871210577927969
  pristine: 0.01219 slots=100 live=100 draw=100 hash=10660871210577927969
  churned : 0.01224 slots=484 live=100 draw=100 hash=10660871210577927969
```

Identical hash, indistinguishable timing. That is the compaction doing its job: `_slots` carries
the holes, `_draw` does not, and the ray never sees them. A design that skipped nulls in the
traversal loop instead would have shown 384 wasted branches per ray here.

**4. What does `commit()` cost?** `$S/t_commit.cpp`, best-of-200 with an empty `edit()` scope in
front of each to force the rebuild:

```
commit scene 0 (484 prims): 0.0025 ms
commit scene 4 (206 prims): 0.0011 ms
commit   10000 prims: 0.0522 ms
commit  100000 prims: 0.5765 ms
commit 1000000 prims: 6.5208 ms
```

Linear, ~6.5 ns/prim, and it happens once per render start. Scene 4's number is the one to keep:
206 prims of which 200 share a single prototype, so `commit()` forwards into that prototype 200
times and it still costs a microsecond — the idempotence requirement from step 1 is holding.

---

# Step 12 — GATE 6: assert against USD itself

The tracer is USD-free, so nothing above has actually checked the assumptions this task makes
*about Hydra*. `$S/t_usd.cpp` does, in sixteen assertions, and it also compiles the 0.3.0 gateway
against the real `HdRenderThread`:

```cpp
class WeekendRenderParam final : public HdRenderParam
{
public:
  WeekendRenderParam(scene *s, HdRenderThread *t) : _scene(s), _thread(t) {}
  scene *AcquireSceneForEdit() {
    _thread->StopRender();
    return _scene;              // the tracer bumps the version inside edit()
  }
private:
  scene *_scene;
  HdRenderThread *_thread;
};
```

Expected output, exactly as measured:

```
HdAovTokens->primId exists                               ok
HdAovTokens->instanceId exists                           ok
HdAovTokens->elementId exists                            ok
id AOVs are HdFormatInt32                                ok
id AOVs are NOT multisampled                             ok
id AOVs clear to -1                                      ok
HdFormatInt32 is 4 bytes, like ours                      ok
HdChangeTracker::DirtyPrimID == 1<<2                     ok
HdChangeTracker::DirtyVisibility exists                  ok
HdChangeTracker::DirtyInstancer exists                   ok
AllDirty includes DirtyPrimID                            ok
IsVisibilityDirty(AllDirty) is true                      ok
hdEmbree's initial mask omits DirtyPrimID                ok
hdEmbree's initial mask includes DirtyVisibility         ok
AcquireSceneForEdit returns the scene after StopRender   ok
an atomic version counter is lock-free sized             ok
```

The last two rows of the dirty-bit block are the interesting ones and they look like a
contradiction. hdEmbree's initial mask (§7.3) does **not** ask for `DirtyPrimID`, and
`HdRenderIndex::_AllocatePrimId` deliberately does not mark it dirty either ("*not marking
DirtyPrimID here to avoid undesirable variability tracking*", `renderIndex.cpp:1929`). hdEmbree
gets away with that because it never copies the id — it stores an `HdRprim*` in its prototype
context and calls `rprim->GetPrimId()` at trace time (`renderer.cpp:869`). We cannot: our scene is
USD-free and holds no `HdRprim*`. The resolution is in Appendix A, and it is simpler than either
alternative.

Two link-line traps this program walks into, recorded because both cost time:
`pxr/imaging/hd/renderParam.h` does not exist as a standalone header (`HdRenderParam` comes in via
`renderDelegate.h`), and omitting `-lusd_python` leaves
`pxr_boost::python::throw_error_already_set` unresolved out of `libusd_python.so`.

---

# Step 13 — GATE 7: the viewer, by hand

The one gate no program replaces. Build and run:

```bash
cmake --build build --config Release
./build/viewer/Release/viewer 0
```

- It converges as before. Nothing about a static scene changed, and no edit ever happens, so the
  render is **never interrupted** — that is §10.1's design intent stated as an observation.
- `TAB` cycles the selected prim. `H` hides it: the image restarts from zero and the prim is gone.
  `H` again: it comes back. `X` removes it: same, permanently.
- Hold `H` down. The title's sample count restarts repeatedly but the window stays responsive —
  the render is being stopped inside the key handler's `edit()` and restarted by the loop, once
  per frame at most, not once per key repeat.
- Hide a prim, then let it converge fully, then unhide. The restart is immediate; there is no
  frame in which the old prim is still visible in a converged image.
- `SPACE` still pauses and `R` still restarts.

If the viewport ever hangs for more than a fraction of a second on an edit, the cause is the stop
being taken *after* the edit lock rather than before — see step 2.

---

# Step 14 — Commit

```bash
cd ~/git/weekend-raytracer
git diff --stat
```

Expect exactly nine files: `tracer/scene.h` (new), `tracer/hittable.h`,
`tracer/hittable_list.h`, `tracer/instance.h`, `tracer/render_buffer.h`, `tracer/renderer.h`,
`tracer/example_scenes.h`, `tracer/main.cpp`, `viewer/main.cpp`. Anything else is scope creep or a
stray scratchpad file.

```bash
grep -rn '#include.*pxr' tracer/ viewer/     # must be empty
ls tracer/*test* viewer/*test* 2>/dev/null   # must be empty
git status --porcelain                        # no .ppm, no scratchpad leakage
```

Then tick `scene graph with mutation` in [[Roadmap]] and commit with a message in the existing
style — a short lowercase summary line, no body.

---

## Definition of done

- [ ] `cmp` says all five scenes are **byte-identical** to the step 0 goldens with every prim
      routed through `scene`
- [ ] `$S/t_scene` reports `live == draw == prim count` and `version == 1` for every scene
- [ ] `$S/t_mutate` passes all 18 assertions, including all three byte-equality claims
      (`remove` == never inserted, `set_visible(false)` == never inserted, hide/unhide round trip)
- [ ] `$S/t_ids` passes, with `element_id` `-1` in every pixel and every `prim_id` traceable to
      `set_prim_id` rather than to a handle
- [ ] The **stale-id negative control** was actually run and actually failed: 2006/2006 pixels
      wrong with the reset removed
- [ ] `$S/t_thread` passes, and reports acquire #1 in the millisecond range with acquires #2–484
      two orders of magnitude cheaper
- [ ] ThreadSanitizer reports **0 warnings** on the parallel-edit + live-render test, run with
      `singlethread_schedule`
- [ ] The **edit-lock negative control** was actually run and actually failed: TSan races naming
      `scene_edit::insert`
- [ ] `scene` is within ~5% of `hittable_list` on scenes 0 and 4, measured interleaved in one
      binary
- [ ] 384 dead slots cost nothing: same image hash, indistinguishable ms/px
- [ ] `commit()` is linear and under 3 µs for 484 prims, and a second call with no edit is free
- [ ] `$S/t_usd` reports 16/16, including that the gateway compiles and links against the real
      `HdRenderThread`
- [ ] The viewer's step 13 checklist passes by hand, including the held-`H` responsiveness check
- [ ] `git diff --stat` is empty for `camera.h`, `camera_desc.h`, `mat4.h`, `material.h`,
      `sphere.h`, `render_control.h`, `rng.h`, `schedulers.h`, `vec3.h`, `color.h`, `interval.h`,
      `ray.h`, `tracer.h`
- [ ] `grep -rn '#include.*pxr' tracer/ viewer/` is empty — the tracer is still USD-free.
      (Grep for the *include*, not for `pxr/`: `render_buffer.h:313` cites
      `pxr/imaging/hd/tokens.h` in a prose comment, so the looser grep never comes back empty)
- [ ] `build-hydra` still builds `hdWeekend` and `testHdWeekend` (still vacuous — `hydra/`
      includes no tracer header until 0.3.0)
- [ ] No test file and no test CMake target added to the repo

---

## Design notes — decisions made, recorded so they aren't re-litigated

**The gateway is a type, not a method.** §6 and hdEmbree both show `AcquireSceneForEdit()`
returning a raw `MyScene*`, leaving the caller free to hold it, pass it around, or mutate through
it later — after the render has been restarted. `scene_edit` closes that: the mutators are its
members, `scene`'s are private, and the lock is released by its destructor. The cost is that a
0.3.0 `HdRenderParam::AcquireSceneForEdit()` returns a `scene*` and the *prim* calls `edit()` on
it, which is one more line at each call site than hdEmbree's. Appendix A shows the shape.

**Stop first, then lock.** `stop_render()` can block for milliseconds. Taking the edit lock first
would make one thread's render stop serialise every other prim's `Sync()`. Measured: with the
current order, acquires #2–484 cost 60 ns each because they find both the render idle and the
lock free.

**`std::mutex`, not a lock-free structure.** Edit-vs-edit contention only exists during `Sync()`,
which is bounded by the prim count and happens between frames. 60 ns per uncontended acquire is
not worth a single line of lock-free code, and a lock-free slot table would have to solve
ABA on the free list for no measured benefit.

**The version lives on `scene`, not on the delegate.** §6 puts `std::atomic<int> sceneVersion` on
the render delegate and passes a pointer to both the render param and the render pass. Ours lives
next to the thing it describes, so it cannot be bumped without an edit or edited without a bump —
`scene_edit`'s constructor does both. The delegate just forwards `scene->version()`.

**`uint64_t`, not hdEmbree's `int`.** hdEmbree's counter overflows after 2³¹ edits. That sounds
unreachable until you multiply out an interactive session: 10 000 prims re-syncing at 60 Hz
reaches 2.1×10⁹ in about an hour. Overflow is only *harmful* if the wrapped value lands exactly
on the pass's last-seen value, so this is a very small risk — but it costs nothing to remove, and
`sizeof(std::atomic<uint64_t>) == 8` is lock-free on this target (verified in step 12).

**`commit()` at render start, not at edit end.** This is the load-bearing choice for the next task.
hdEmbree calls `rtcCommitScene(_scene)` from `_PreRenderSetup` (`renderer.cpp:490`), on the render
thread, once. Doing the work in `scene_edit`'s destructor instead would be correct today — the draw
list rebuild is 2.5 µs — and catastrophic the moment `commit()` builds a BVH, because a 484-prim
re-Sync is 484 edit scopes. Measured, the rebuild is linear at ~6.5 ns/prim; the BVH will be three
orders of magnitude more per prim, and it will still be once per render.

**`commit()` is virtual on `hittable`.** The alternative — `renderer::render(const camera&,
scene&, ...)` — was considered and rejected: the renderer's only dependency on the world today is
`hittable::hit`, and coupling it to `scene` would mean a scratchpad program cannot point the
renderer at a bare `sphere`. The virtual costs a vtable slot that already exists and is called
once per render. It also has to be virtual eventually anyway: a mesh's BVH will live below a
`scene` slot, behind an `instance`, and the commit has to reach it.

**Forwarding `commit()` accepts redundant calls.** Scene 4's prototype is shared by 200 instances,
so it is committed 200 times per render start. Measured cost of all 206: 1.1 µs. The alternative —
a visited set, or a global "commit epoch" — is bookkeeping in exchange for microseconds. The
requirement it imposes instead is written on `hittable::commit()`: **idempotent and cheap when
clean**. `scene::commit()`'s `_dirty.exchange(false)` is that contract honoured; every future
implementation must do the same.

**A compacted draw list, rather than skipping nulls in the traversal loop.** `_slots` is stable so
handles are stable; `_draw` is dense so the ray never pays for a hole. Measured: 384 dead slots
cost nothing and produce a bit-identical image. Skipping nulls inline would have cost a branch per
dead prim per ray — 384 × every ray in the churned case.

**`_draw` holds raw pointers, not `shared_ptr`.** The owning reference is in `_slots` and outlives
every use; the entries are only valid between a `commit()` and the next edit, which is exactly the
window in which `hit()` runs. Copying a `shared_ptr` per prim per ray would put atomic refcount
traffic on the hottest loop in the renderer.

**`scene` stores `prim_id` separately from the handle.** They are different things and they are
allocated by different owners. `HdRprim::GetPrimId()` is assigned by
`HdRenderIndex::_AllocatePrimId` as an index into `_rprimPrimIdMap`, capped at 2²⁴−1, and
`_CompactPrimIds()` **reassigns every prim's id** when that space wraps (`renderIndex.cpp:1905`).
Our handle is an index into `_slots` and is stable for the prim's whole life. Conflating them
would mean the `primId` AOV reports whatever our free list happened to hand out — wrong for
picking, and wrong in a way that only shows up after a long session.

**No `SdfPath` → handle map inside `scene`.** [[roadmap-discussion-8-26]] §6 says "prims
addressable by stable id (`SdfPath`)", and the resolution is that the *addressing* is by handle
while the *path* stays where Hydra already keeps it. `HdRenderIndex` owns `_rprimMap`
(`SdfPath` → `HdRprim*`); `HdEmbreeMesh` holds `_rtcMeshId`; `HdEmbreeMesh::Finalize` removes by
that handle, not by path. Ours does the same. That deletes an `unordered_map<SdfPath, int>`, its
lock, and the question of what to do when a path is re-inserted — none of which any caller needs.

**A prim only writes `hit_info`'s ids on a successful hit.** Stated in "The id-stamping rule",
enforced by the reset in the success branch, and gated by a negative control that fails on
2006/2006 pixels. The cheaper-looking alternative — reset before every prim test — is two stores
per prim per ray on the traversal loop, against a handful per ray here.

**`hit_info` grew rather than gaining a side channel.** 72 → 88 bytes, copied on every accepted
hit. A separate "id block" filled only for the primary ray was considered: it saves nothing
(the same stores, the same copy) and it means two structures have to agree about which hit they
describe. Measured cost of the growth: below the noise floor.

**Visibility is a flag on the record, not a remove-and-reinsert.** `DirtyVisibility` arrives
constantly — a viewport visibility toggle, a purpose switch, a variant change — and `set_visible`
must not invalidate the handle the Rprim is holding. hdEmbree makes the same call with
`rtcEnableGeometry`/`rtcDisableGeometry` (`mesh.cpp:828-833`) rather than detaching. Verified
pixel-equivalent to never inserting the prim.

**A freed handle is reused, with no generation counter.** Stale handles are unreachable by
construction: Hydra calls `Finalize(renderParam)` and then immediately `DestroyRprim`
(`renderIndex.cpp:539-541`), and the only holder of the handle is the object being deleted. A
generation tag was considered and dropped — it would double the handle's size to catch a bug the
lifecycle prevents.

**No `scene::clear()`.** `HdRenderIndex::Clear()` calls `Finalize` on each Rprim in turn
(`renderIndex.cpp:667`), so per-prim removal already covers it. Adding a bulk clear would add a
second path that has to keep `_free`, `_live` and `_dirty` consistent.

**The viewer clears on restart; hdEmbree does not.** `_Execute` calls `_renderer->Clear()` only in
the AOV-bindings branch (`renderPass.cpp:213`); a `sceneVersion` change gets
`MarkAovBuffersUnconverged()` + `StartRender()` and nothing else, so pre-edit samples stay in the
accumulation buffer. Ours clears, which is what a viewer wants. Flagged rather than settled: the
delegate does not own its render buffers, the host does, and whether to clear them on a scene edit
is a 0.3.0 question about what the host expects — not a question about this task.

**`renderer::render` takes `hittable&`.** The only reason is `commit()`. `render_tiles` stays
`const hittable&` because it genuinely only reads. Nothing in the repo passed a const world, so
this is source-compatible; if something ever needs to, the answer is to commit before calling, not
to make `commit()` const.

---

## Appendix A — the delegate side, for 0.3.0

Not committed in this task. `hydra/mesh.cpp` is still the hdTiny stub. This is what "does
something" looks like, and it is the payoff for the shape chosen above.

```cpp
// hydra/renderParam.h
class HdWeekendRenderParam final : public HdRenderParam
{
public:
    HdWeekendRenderParam(scene *s, HdRenderThread *t) : _scene(s), _thread(t) {}
    scene *GetScene() { return _scene; }
private:
    scene *_scene;
    HdRenderThread *_thread;
};
```

There is no `AcquireSceneForEdit()`, because there is nothing left for it to do: the delegate
binds `HdRenderThread::StopRender` into the scene once, at construction, and `scene::edit()` calls
it.

```cpp
// hydra/renderDelegate.cpp, in _Initialize()
_scene.set_stop_render([this]() { _renderThread.StopRender(); });
_renderThread.SetRenderCallback(...);
_renderThread.StartThread();
```

```cpp
// hydra/mesh.cpp
void HdWeekendMesh::Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam,
                         HdDirtyBits *dirtyBits, TfToken const &reprToken)
{
    const SdfPath &id = GetId();
    scene *world = static_cast<HdWeekendRenderParam *>(renderParam)->GetScene();

    // One edit scope for the whole prim: one StopRender, one version bump, one
    // lock acquisition - not one per dirty bit.
    scene_edit edit = world->edit();

    if (_handle == null_prim) {
        _handle = edit.insert(nullptr);
    }

    // GetPrimId() is OUR member, not a scene-delegate pull, so 7.2's
    // "only pull data whose dirty bit is set" does not apply and no dirty bit
    // is needed. Which is just as well: _AllocatePrimId deliberately does not
    // set DirtyPrimID, and _CompactPrimIds does. Copying it unconditionally is
    // one int per Sync and is correct under both.
    edit.set_prim_id(_handle, GetPrimId());

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _UpdateVisibility(sceneDelegate, dirtyBits);   // fills _sharedData.visible
        edit.set_visible(_handle, IsVisible());
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _instance->set_transform(to_mat4(sceneDelegate->GetTransform(id)));
    }

    // ... points / topology / triangulation -> edit.set_prim(_handle, ...)

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;   // clear what was consumed
}

void HdWeekendMesh::Finalize(HdRenderParam *renderParam)
{
    scene *world = static_cast<HdWeekendRenderParam *>(renderParam)->GetScene();
    scene_edit edit = world->edit();
    edit.remove(_handle);
    _handle = null_prim;
}
```

Four facts that make this shorter than it could be:

- **`Finalize` is where removal goes, not `DestroyRprim`.** `DestroyRprim(HdRprim*)` has no
  `HdRenderParam` and therefore cannot stop the render; `Finalize(renderParam)` does, and Hydra
  calls it immediately before (`renderIndex.cpp:539-541`). hdEmbree detaches its geometry in
  `Finalize` for exactly this reason (`mesh.cpp:38`).
- **One `scene_edit` per `Sync()`, not one per dirty bit.** The whole prim's update is one stop,
  one bump and one lock. This is the shape that makes the 60 ns figure the relevant one.
- **`GetPrimId()` needs no dirty bit.** Argued in the comment above and confirmed in step 12:
  hdEmbree's initial mask omits `DirtyPrimID` and `_AllocatePrimId` does not set it, because
  hdEmbree reads the id live through an `HdRprim*`. We copy instead, unconditionally, for the
  price of one `int`.
- **`_UpdateVisibility` is `HdRprim`'s, not ours.** It writes `_sharedData.visible`, read back
  through `IsVisible()`. hdEmbree does the same and then translates it into
  `rtcEnableGeometry`/`rtcDisableGeometry` (`mesh.cpp:603`, `mesh.cpp:830`).

And the render pass, which is §9 item 1 and nothing else at this stage:

```cpp
// hydra/renderPass.cpp
const uint64_t sceneVersion = _scene->version();
if (_lastSceneVersion != sceneVersion) {
    _lastSceneVersion = sceneVersion;
    needStartRender = true;
}
```

That is the same three lines the viewer runs in step 6, which is the point of writing them there
first.

## Appendix B — what `bvh with rebuild-on-mutation` adds

Recorded now because this task exists partly to make that one small.

The seam is already cut. `scene::commit()` is called once per render start, from the render
thread, with no edit in flight and `_dirty` telling it whether anything changed. The BVH task is:

```cpp
  void commit() override
  {
    if (!_dirty.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    _draw.clear();
    for (const record &r : _slots) {
      if (r.prim == nullptr) continue;
      r.prim->commit();
      if (r.visible) _draw.push_back({r.prim.get(), r.prim_id});
    }

+   _accel.build(_draw);         // <- the whole task
  }

  bool hit(...) const override
  {
-   for (const entry &e : _draw) { ... }
+   return _accel.hit(r, clipping_range, info);   // stamps e.prim_id on the winner
  }
```

What it needs from elsewhere, none of which exists yet:

- **World-space bounds per prim.** `hittable` gains a `virtual aabb bounds() const`. `sphere` is
  trivial; `instance` transforms the prototype's object-space box's eight corners and takes the
  extent of the result — `object_to_world()` was kept in [[transform-support]] for precisely this.
- **The `prim_id` stamp has to survive the tree.** In the linear scan the traversal loop owns both
  the `prim_id` and the "did this beat the closest" decision. In a BVH the leaf owns them. Keep
  `entry` as the leaf payload and the stamp stays a one-liner; store bare `hittable*` and it
  becomes a parallel lookup.
- **The stale-id reset moves with the loop.** Whatever code decides "this hit is the new closest"
  is the code that must clear `instance_id`/`element_id` for the next candidate. The negative
  control in step 9 should be re-run against the BVH unchanged — it is written against the
  renderer, not against `scene::hit`, so it will still fire.
- **A rebuild budget.** 6.5 ns/prim today; a BVH build is more like 1–10 µs/prim. At 484 prims
  that is a few milliseconds per render start, which is invisible next to the 1.4 ms `StopRender`
  already measured. At a million triangles it is seconds, and that is when
  "refit vs rebuild" stops being premature.

Two things that will *not* need revisiting: the version counter (a rebuild does not change what
"the scene changed" means), and the gateway (a rebuild happens after the render is already
stopped, by definition).

---

## Next up

`triangle mesh` — `HdMeshUtil::ComputeTriangleIndices`, flat indexed triangle storage, and the
first real value for `element_id`. This task hands it three things: the AOV is already plumbed and
already gated at `-1`, so a triangle that stamps a face index is immediately visible in a test;
`hittable_list`'s stale-id reset is already in place for the face loop; and `scene::set_prim` gives
`DirtyPoints`/`DirtyTopology` somewhere to land without disturbing the prim's handle, its
`prim_id`, or its visibility. [[roadmap-discussion-8-26]] §1 is the argument for why the BVH ships
*with* meshes rather than after them — `scene::hit`'s linear scan over 484 prims is already the
+4% measured in step 11, and it becomes unusable at the first thousand-triangle mesh.
