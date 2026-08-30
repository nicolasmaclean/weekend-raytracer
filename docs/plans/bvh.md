# bvh with rebuild-on-mutation — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `bvh with rebuild-on-mutation` — see [[Roadmap]]. Last
item before `0.3.0 - hydra delegate`.
**Context:** [[hydra-spec]] §6 (`StopRender()` → mutate → bump `sceneVersion`, and "the render
thread may be reading your BVH"), §14 (instancing), §17.7 (progressive rendering) ·
[[roadmap-discussion-8-26]] §1 (why the BVH shipped adjacent to meshes), §5 item 5 ·
[[triangle-mesh]] Appendix B · [[scene-graph]] Appendix B · [[transform-support]] Appendix B
**Every number in this document was measured on this machine on 2026-08-29, at commit `7636be9`
("mesh (and tinyobjloader for cli/viewer)"). The implementation described here was written and run
before the document was; "Pre-verified facts" is its output, and two of the bugs listed there were
found by running it, not by reasoning about it.**

---

## What this task is

Every ray in the tracer currently tests every primitive. `scene::hit` is a `for` loop over
`_draw`; `mesh::hit` is a `for` loop over `geom`. [[triangle-mesh]] step 12 measured what that
costs the moment a real asset arrives: one 1280-triangle icosphere renders **3.0× slower than all
484 spheres of scene 0 put together**, and 167× slower than the analytic sphere it approximates.
Scene 6 takes **0.149 ms/px**. Nothing in 0.3.0 is usable at that number.

At the end of this task there are two new headers — `tracer/aabb.h` and `tracer/bvh.h` — one
binned-SAH BVH implementation used at **two levels**, and a `commit()` that refits when it can and
rebuilds when it must:

- **BLAS.** `mesh` owns a BVH over its own triangles, in object space.
- **TLAS.** `scene` owns a BVH over `_draw`, in world space.
- **`hittable` grows `bounds()`**, and `instance::bounds()` is where the two levels meet: it
  transforms the prototype's object-space box into the world box the TLAS indexes.

Measured, best of five, 400×225 at 50 spp:

| scene | before | after | |
|---|---|---|---|
| 0 — 484 spheres | 0.06879 | 0.00633 ms/px | **10.9×** |
| 4 — 206 instances | 0.08034 | 0.00750 ms/px | **10.7×** |
| 6 — 1280-triangle mesh | 0.14927 | 0.00392 ms/px | **38.1×** |
| 1, 2, 3 — five spheres each | 0.00333 | 0.00350 ms/px | 0.95× |
| 5 — three prims | 0.00100 | 0.00112 ms/px | 0.90× |

**All seven golden images stay byte-identical.** The last two rows are the honest part: below
`linear_threshold` the tree is bypassed entirely and what remains is a ~10% overhead in `commit()`
and one extra call frame. Step 9 measures the threshold that buys those rows back and shows why 8
is where it lands.

---

## Why it matters more than "put the prims in a tree" sounds

**1. The roadmap item is `rebuild-on-mutation`, not `bvh`.** [[hydra-spec]] §6 requires
`StopRender()` → mutate → bump `sceneVersion` → restart on *every* scene edit, and §17.7 says the
viewport is expected to be interactive. A build-once BVH would be correct and useless: the first
time usdview drags a transform, the tree describes where the prim used to be. What the delegate
actually needs is a `commit()` that is cheap when only positions moved. Step 8 is that, and it is
the reason `bvh` exposes `refit()` and a cost model rather than just `build()` and `hit()`.

**2. `scene::commit()` is already the correct seam, and [[scene-graph]] cut it on purpose.** It
runs once per render start, from the render thread, with no edit in flight, with `_dirty` telling
it whether anything changed. Nothing about the gateway, the version counter, or the lock needs
revisiting. This task is almost entirely *inside* one function that already exists.

**3. Two levels is not an optimisation, it is the data model.** Hydra hands over one Rprim per
mesh with its own transform, and §14 instancing hands over N transforms sharing one prototype. A
single flat BVH over triangles would have to be rebuilt whenever any transform moved, and would
lose instancing entirely. A TLAS over prims plus a BLAS per mesh means a transform edit rebuilds
*only* a tree whose leaf count is the number of prims — measured at **0.030 ms for 400
instances**, against 0.297 ms for a full rebuild and against the 1.4 ms `StopRender` already
costs.

**4. It is the last thing between the tracer and `usdview`.** After this, 0.3.0 is wrapper code.

---

## What is explicitly NOT in this task

- **A parallel build.** `bvh::build` is single-threaded. It is 0.24 ms on scene 0 and 10.6 ms on a
  20 480-triangle mesh, against a 353 ms render; the case where it matters is a million triangles,
  and the mechanism it would use — an injected scheduler, exactly like `tile_scheduler` — is
  sketched in Appendix B. **`bvh.h` must not include TBB**, for the same reason `renderer.h` does
  not: [[interruptible-render-loop]] step 6b gates the renderer compiling with no TBB anywhere.
- **A wide (BVH4/BVH8) tree, or SIMD.** Appendix B. The binary tree it would be collapsed from is
  the thing being built here, so nothing is thrown away by deferring it.
- **Spatial splits (SBVH), treelet restructuring, reinsertion.** All improve the tree by 10–30% on
  scenes we do not have, at 2–10× the build cost. Not before profiling exists (a `0.4.0` wishlist
  item).
- **Float or quantised node bounds.** `sizeof(bvh::node)` is 64 bytes with doubles. Halving it is
  worth real cache money and is a self-contained change; it also needs conservative outward
  rounding to stay correct, and it is not free to get right. Design notes.
- **De-duplicating `commit()` on a shared prototype.** Measured: 20 instances of one 20 480-triangle
  mesh cost **9.7 ms per commit** where one would do, because `instance::commit()` calls
  `proto->commit()` and nothing remembers that it already ran. This is pre-existing behaviour that
  this task makes more expensive, not behaviour it introduces. The fix is a commit epoch on
  `hittable`; it is a separate change with its own gate. **Recorded here so it is not discovered as
  a mystery later.**
- **Anything in `hydra/`.** Appendix A.

---

## Pre-verified facts

Measured, not assumed. Every program named below was written and run; the numbers are its output.

```
S=$CLAUDE_JOB_DIR/tmp/scratchpad          # any scratch dir; the headers were built in $S/ship
```

### Correctness

| Claim | Measured |
|---|---|
| **All seven scenes are byte-identical with the BVH in.** 400×225, 50 spp, `cmp` against the pre-task goldens | **7 / 7 identical** |
| **The tree agrees with a linear scan ray for ray**, comparing `t`, `prim_id`, `instance_id`, `element_id`, `mat` and `normal` | 7 scenes × 200 000 rays, **0 mismatches, worst \|Δt\| = 0** (bit-exact: only the visiting order differs) |
| **`tmax <= tmin` in the slab test is a real bug, not a style choice.** An axis-aligned triangle's box has *exactly* zero extent on one axis, so `t0 == t1` | with `<=`: **20 000 / 20 000 rays rejected** by a box they pass through. On scene 5 that is **31 273 / 90 000 pixels wrong**, max Δ = 175/255 — and scenes 0–4 and 6 stay *identical*, so only the flat-triangle scene catches it |
| **The NaN path is real.** Ray parallel to a slab with its origin exactly on the plane: `0 * inf` | `t0` **is NaN**; written as `if (t0 > tmin) tmin = t0;` the NaN leaves the interval alone and the box passes — conservative. Reversing the comparison drops real hits |
| **An empty `aabb` is never hit and never pollutes a parent** | 20 000 random rays, **0 hits**; `expand(empty)` changes nothing; `centroid()` of an empty box is **not NaN** (it would be `0.5 * (inf + -inf)`, and a singular `instance` really does report one) |
| `instance::bounds()` contains all 8 transformed corners and is **idempotent under re-commit** | pass — it transforms the prototype's corners, never the previous world box's |
| **A degenerate axis overruns the traversal stack.** 100 000 coplanar boxes strung along +x, SAH costs all identically zero | **depth 98** against a 64-entry stack. With the median fallback: **depth 14** |
| Max depth on real geometry | **25** at 1 000 000 primitives, **16** at 20 480, **13** at 1280 — against `max_depth = 60` |
| Pathological inputs all build and stay shallow | 100k identical boxes **14** · 100k collinear **14** · 100k empty **14** · 0 boxes, 1 box **fine** |

### Tree quality — the same scenes, three builders, `leaf_size = 1`

Render time, ms/px, best of four. The builder is the *only* thing that changes.

| builder | scene 0 (484 spheres) | scene 4 (206 instances) | scene 6 (1280 tris) |
|---|---|---|---|
| **binned SAH** | **0.00493** | **0.00590** | **0.00345** |
| object median | 0.00889 (1.80×) | 0.00803 (1.36×) | 0.00437 (1.27×) |
| spatial middle | 0.00662 (1.34×) | 0.00833 (1.41×) | 0.00441 (1.28×) |

Median split is 100 lines and 2.5× cheaper to build. It is also **1.8× slower to trace on the
scene this project renders most**. That gap is the whole argument for the extra 150 lines.

### Leaf size — render time, ms/px, best of four, binned SAH

| `leaf_size` | scene 0 | scene 4 | scene 6 (1280) | scene 6 (20 480) |
|---|---|---|---|---|
| **1** | **0.00506** | **0.00621** | **0.00380** | **0.00473** |
| 2 | 0.00561 | 0.00669 | 0.00401 | 0.00487 |
| 4 | 0.00590 | 0.00865 | 0.00427 | 0.00516 |
| 8 | 0.00624 | 0.00991 | 0.00431 | 0.00527 |

**`leaf_size = 1` does not mean one primitive per leaf.** The SAH's own leaf-versus-split test
still terminates early: measured **mean leaf 1.05** on scene 0 and **1.73** on the icosphere. It
means "do not stop before asking". It costs 2× the nodes and ~1.9× the build.

### Bin count — 1280 triangles, `leaf_size = 1`

| bins | SAH cost | build |
|---|---|---|
| 8 | 28.34 | 0.44 ms |
| **12** | **27.71** | **0.45 ms** |
| 16 | 27.86 | 0.50 ms |
| 32 | 27.59 | 0.76 ms |

Past 12 bins the tree stops improving and the build keeps paying. 12 it is.

### Build and refit cost

| Claim | Measured |
|---|---|
| **Three `std::vector`s per `build_node()` call is two thirds of the build.** They are per *node*, not per build | 487 prims: **1062 → 337 ns/prim** when the bins and the sweep arrays become stack arrays (**3.2×**) |
| Build throughput, warm, best of many | **337 ns/prim** at 487 · **421** at 20 480 · **676** at 1 000 000 (cache) |
| Build cost of one cold `commit()` | scene 0 TLAS (484) **0.237 ms** · scene 4 (206) **0.169 ms** · scene 6 BLAS (1280) **0.552 ms** · 20 480-triangle BLAS **10.6 ms** |
| ...against the render it precedes | scene 6 at 50 spp is **353 ms**. The build is **0.16%** of it, and 8% of a single-sample viewer pass |
| **Refit is 52× cheaper than a rebuild** | 483 prims: **0.0032 ms** vs **0.164 ms** |
| **A refitted tree degrades smoothly, and the SAH ratio sees it before the stopwatch does** | see step 7's table: SAH ratio 1.00 → 2.47 over 14 steps, with measured primitive tests per ray tracking it 0.39 → 0.98 |
| **The 1.3 trigger fires about once every 13 frames** on 400 drifting instances | commit **0.030 ms** (refit) vs **0.297 ms** (rebuild); mean **0.051 ms**, a **4.9×** saving, and every frame still agrees with the linear scan |
| Memory | `sizeof(aabb)` **48**, `sizeof(bvh::node)` **64**, **2.00 nodes/prim** at `leaf_size = 1` → **128 B/prim** resident. Peak RSS during a 1 M-primitive build: **325 MB** ≈ 325 B/prim |
| **A shared prototype is committed once per instance** | 20 instances of one 20 480-triangle mesh: **9.7 ms per commit**, 20× what one would cost. Pre-existing; see "explicitly NOT" |

Build lines that work on this machine:

```bash
cd ~/git/weekend-raytracer
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# the cli, against scratchpad headers, with the tbb scheduler and the obj loader
g++ -std=c++17 -O3 -DNDEBUG -DTINYOBJLOADER_USE_DOUBLE \
    -DTRACER_ASSET_DIR="\"$PWD/assets\"" \
    -I$S/ship -Ibuild/_deps/tbb-src/include -Ibuild/_deps/tinyobjloader-src \
    -o $S/cli $S/cli.cpp \
    build/_deps/tinyobjloader-build/Release/libtinyobjloader_double.a \
    -Lbuild/gnu_13.3_cxx11_64_release -ltbb

# the gates: no scheduler needed, and -O2 so an assert-shaped test is readable in gdb
g++ -std=c++17 -O2 -DTINYOBJLOADER_USE_DOUBLE -DTRACER_ASSET_DIR="\"$PWD/assets\"" \
    -I$S/ship -Ibuild/_deps/tinyobjloader-src -o $S/gates $S/gates.cpp \
    build/_deps/tinyobjloader-build/Release/libtinyobjloader_double.a

# the builder stress test: no tracer deps at all beyond the headers
g++ -std=c++17 -O3 -DNDEBUG -I$S/ship -o $S/stress $S/stress.cpp
```

**`aabb.h` must include `tracer.h` before `interval.h`.** `interval.h` → `tracer.h` → `color.h` →
`interval.h`, and the `#pragma once` on the second visit hands `color.h` a file that has not
defined `interval` yet. Every existing header gets this right by accident, by including
`tracer.h` (or `hittable.h`) first. Getting it wrong costs ten confusing minutes:

```
color.h:20:18: error: 'interval' does not name a type
```

---

## The design in one page

```
tracer/aabb.h        NEW      struct aabb + slab_ray + slab_hit(). ~125 lines.
                              No BVH knowledge; `instance` and `mesh` would want it anyway.

tracer/bvh.h         NEW      class bvh. Binned SAH build, refit, ordered traversal with a
                              caller-supplied leaf callback. ~455 lines, over half comments.

tracer/hittable.h    EDIT     + `virtual aabb bounds() const = 0;`  (5 lines)
tracer/sphere.h      EDIT     + bounds()   (6 lines)
tracer/hittable_list.h EDIT   + bounds()   (10 lines)
tracer/instance.h    EDIT     + bounds(): the eight-corner transform  (21 lines)

tracer/mesh.h        EDIT     owns a `bvh accel` + a `tri_index` permutation. commit() builds
                              or refits; hit() traverses. The Moller-Trumbore loop moves into
                              a private intersect() and is otherwise UNCHANGED.

tracer/scene.h       EDIT     owns a `bvh _tlas` + `_visible` (insertion order) alongside
                              `_draw` (BVH order). commit() builds or refits; hit() traverses;
                              the old loop survives verbatim as linear_hit().

tracer/renderer.h    UNTOUCHED
tracer/camera*.h     UNTOUCHED
tracer/main.cpp      UNTOUCHED
viewer/main.cpp      UNTOUCHED   (it only ever sees `scene`)
tracer/CMakeLists.txt UNTOUCHED  (header-only INTERFACE target; new headers need no line)
```

Nothing outside `tracer/` changes, and no public signature changes except the one new pure
virtual.

### The seven invariants

Everything below follows from wanting these to hold, so that `renderer::raycast`, every
`material`, and the whole of `hydra/` stay unaware that a tree exists.

| Invariant | How | Verified |
|---|---|---|
| **A BVH changes the ORDER prims are tested in, never the answer** | traversal only ever tightens `closest`, and a leaf writes `hit_info` exactly once, on success | step 10: 1.4 M rays, 0 mismatches, worst \|Δt\| = **0** |
| **`t` stays in the caller's units** | the slab test is arithmetic on the same unnormalised direction; `bvh` never touches the ray | the 0.001 acne epsilon and `instance`'s contract are untouched; goldens identical |
| **A box test errs toward FALSE POSITIVES, never false negatives** | strict `tmax < tmin`; NaN comparisons written so a NaN leaves the interval unchanged | step 3's two negative controls |
| **`bounds()` is valid only after `commit()`** | `scene::commit()` calls `prim->commit()` on every prim *before* reading any `bounds()` | ordering is explicit in `commit()`; `mesh::bounds()` returns the tree's root, so it cannot be stale relative to the tree |
| **The `prim_id` / stale-id stamp survives the tree** | the leaf callback *is* the old loop body, `entry` is still the leaf payload | [[scene-graph]] step 9's negative control is written against the renderer and still fires |
| **`element_id` is still the authored face** | `geom` is permuted, `tri_index` carries the original triangle index, and `face` is indexed through it | step 10 compares `element_id` on every ray |
| **Refit is always CORRECT, only ever stale in shape** | `tri_index` stays a permutation of every triangle no matter what the indices now say; a refitted box still contains its primitive | step 12 re-checks agreement after every mutated frame |

### Who owns what, after this task

| Decision | Owner |
|---|---|
| The world-space box of a prim | the prim — `hittable::bounds()` |
| The object→world box of a mesh | `instance::bounds()`, from `instance::object_to_world()` — kept by [[transform-support]] for exactly this |
| Which triangles are in which leaf | `mesh::commit()`, via `bvh::build` |
| Which prims are in which leaf | `scene::commit()`, via `bvh::build` |
| Whether this commit refits or rebuilds | `bvh::degraded()`, i.e. the SAH ratio — **not** the caller's guess about what changed |
| When it is safe to touch any of it | unchanged: `scene::edit()`, the `StopRender` gateway from [[scene-graph]] |
| Splitting heuristic, bin count, leaf size | `bvh`, and nothing else knows them |

### Where the two levels meet

```
    ray (world)
        |
   scene::hit  ------ TLAS ------> leaf: entry{ const hittable*, prim_id }
        |                                        |
        |                                  instance::hit
        |                                        |   ray -> object space (inv)
        |                                        v
        |                                   mesh::hit  --- BLAS ---> leaf: triangles
        |
   world box of an instance = instance::bounds()
                            = box of the 8 transformed corners of mesh::bounds()
                            = box of the 8 transformed corners of the BLAS ROOT
```

The BLAS root box is the mesh's object-space bounds. That is why `mesh::bounds()` is
`accel.bounds()` and not a separately maintained box: one thing to keep current, and it is
current by construction.

---

# Step 0 — Capture goldens before you touch anything

Same discipline as every task since [[camera-refactor]]. All seven scenes must come back
byte-identical at the end; the whole point of this change is that it is invisible.

```bash
cd ~/git/weekend-raytracer
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
cmake --build build --config Release

mkdir -p $S/golden
for i in 0 1 2 3 4 5 6; do
  ./build/tracer/Release/tracer_cli $i > $S/golden/scene$i.ppm
done
md5sum $S/golden/*.ppm
```

Measured here, at `7636be9`:

```
3292e039125ee04d7f4728ad9d89886f  scene0.ppm      0.06879 ms/px
81978695472eb949e987e46fefe3e694  scene1.ppm      0.00333
418151b864772683d18aef594a1651b7  scene2.ppm      0.00371
57e57b71e5501b5f278b60a73793b64c  scene3.ppm      0.00362
297533cce4fcb5d116e82b2322a6308d  scene4.ppm      0.08034
eeaafa6f38d9b298b0aa1cf6f0e1807a  scene5.ppm      0.00100
d78712aac95e214d9cc5c8488b50acf3  scene6.ppm      0.14927
```

`scene4.ppm`'s hash is the same one [[transform-support]] step 6 recorded. That is a useful signal
that the machine and the build are in the state those measurements were taken in.

Take the timings as best-of-five and interleaved with the new binary when you compare — this
laptop's clock scales, and a naive before-then-after run reads 15% high on whichever went second.

---

# Step 1 — Write `tracer/aabb.h`

A box, a precomputed-reciprocal ray, and the slab test. No BVH in it: `instance` and `mesh` want a
box regardless, and keeping it separate is what lets step 3 gate the box before any tree exists.

Three things in this file are load-bearing and none of them look it.

**`empty()` is `lo = +inf, hi = -inf`, deliberately inverted.** That makes `expand()` an
unconditional min/max with no "is this the first point" branch, makes `expand(empty)` a no-op, and
makes an empty box fail the slab test on the first axis. A prim with nothing in it — a singular
`instance`, a mesh with no triangles — flows through the whole build and traversal without a
single special case.

**`centroid()` guards the empty box.** `0.5 * (inf + -inf)` is NaN, that NaN reaches the SAH
binner, and `int(NaN)` is undefined. Scene 4 has a deliberately singular instance
(`scale(vec3(1, 0, 1))`, "must vanish"), so this is a live case, not a defensive one.

**The slab test's comparisons are written for NaN, and its final test is strictly `<`.** Both are
verified by negative control in step 3. Do not tidy either of them.

```cpp
#pragma once

#include <algorithm>
#include <cmath>

#include "tracer.h"
#include "interval.h"
#include "ray.h"
#include "vec3.h"


// Axis-aligned bounding box, half-open in neither direction: `lo` and `hi` are
// both inclusive. An `empty()` box has lo > hi on every axis, which makes
// `expand` a plain min/max with no "is this the first point" branch.
struct aabb
{
  point3 lo { infinity,  infinity,  infinity};
  point3 hi {-infinity, -infinity, -infinity};

  static aabb empty() { return aabb{}; }

  static aabb infinite()
  {
    return aabb{point3(-infinity, -infinity, -infinity),
                point3( infinity,  infinity,  infinity)};
  }

  bool is_empty() const
  {
    return hi[0] < lo[0] || hi[1] < lo[1] || hi[2] < lo[2];
  }

  void expand(const point3 &p)
  {
    for (int a = 0; a < 3; a++)
    {
      lo[a] = std::min(lo[a], p[a]);
      hi[a] = std::max(hi[a], p[a]);
    }
  }

  void expand(const aabb &b)
  {
    for (int a = 0; a < 3; a++)
    {
      lo[a] = std::min(lo[a], b.lo[a]);
      hi[a] = std::max(hi[a], b.hi[a]);
    }
  }

  vec3 extent() const
  {
    if (is_empty()) return vec3(0, 0, 0);
    return hi - lo;
  }

  // 2*(wh + wd + hd). The SAH only ever uses ratios of these, so the factor of
  // two is kept only to make the number the actual surface area when printed.
  double surface_area() const
  {
    if (is_empty()) return 0;
    const vec3 d = hi - lo;
    return 2 * (d[0] * d[1] + d[0] * d[2] + d[1] * d[2]);
  }

  // An empty box has no centroid; 0.5 * (inf + -inf) is NaN, and a NaN
  // centroid reaches the SAH binner. Prims legitimately report empty - a
  // singular `instance` is one - so this is a real case, not a defensive one.
  point3 centroid() const
  {
    return is_empty() ? point3(0, 0, 0) : 0.5 * (lo + hi);
  }
};

inline aabb merge(const aabb &a, const aabb &b)
{
  aabb r = a;
  r.expand(b);
  return r;
}

// A ray with its reciprocal direction precomputed. Built once per BVH
// traversal, not once per node: the divide is the expensive part of the slab
// test, and a 1280-triangle mesh does ~30 node tests per ray.
struct slab_ray
{
  point3 o;
  vec3 inv;
  int neg[3];

  explicit slab_ray(const ray &r) : o(r.origin())
  {
    const vec3 &d = r.direction();
    for (int a = 0; a < 3; a++)
    {
      inv[a] = 1.0 / d[a];      // deliberately allowed to be +/-inf
      neg[a] = inv[a] < 0;
    }
  }
};

// Slab test. Every comparison is written so that a NaN operand leaves the
// running interval UNCHANGED rather than rejecting the box: a ray whose origin
// lies exactly on a slab plane and whose direction is parallel to it produces
// 0 * inf == NaN, and a BVH that rejects on NaN drops real hits. Erring the
// other way costs one wasted leaf visit.
inline bool slab_hit(const aabb &b, const slab_ray &s, double tmin, double tmax)
{
  for (int a = 0; a < 3; a++)
  {
    double t0 = (b.lo[a] - s.o[a]) * s.inv[a];
    double t1 = (b.hi[a] - s.o[a]) * s.inv[a];
    if (s.neg[a]) std::swap(t0, t1);

    if (t0 > tmin) tmin = t0;
    if (t1 < tmax) tmax = t1;

    // STRICTLY less-than. A box with zero extent on one axis - which every
    // axis-aligned triangle has, and every node above one that is alone in its
    // subtree - produces t0 == t1 exactly, and `<=` rejects every ray that
    // hits it. See the plan's step 5 negative control.
    if (tmax < tmin) return false;
  }

  return true;
}
```

---

# Step 2 — `hittable` grows `bounds()`, and its four subclasses implement it

```cpp
// tracer/hittable.h
 #include <cstdint>

 #include "tracer.h"
+#include "aabb.h"
...
   virtual bool hit(const ray &r, interval clipping_range, hit_info &info) const = 0;

+  // World-space bounds, valid after commit(). Pure, not defaulted: a prim that
+  // silently reported an infinite box would poison every node above it and the
+  // only symptom would be a slow render.
+  virtual aabb bounds() const = 0;
+
   virtual void commit() {}
```

**Pure virtual, not a default.** A defaulted `return aabb::infinite();` would compile everywhere
and silently make every ancestor node's box infinite, which is a BVH that visits everything and a
render that is merely slow. The compiler should refuse to build a `hittable` that has not thought
about its bounds. There are exactly five subclasses in the tree (`sphere`, `mesh`, `instance`,
`hittable_list`, `scene`); `grep -rn "public hittable" tracer/ viewer/ hydra/` finds them all.

```cpp
// tracer/sphere.h  — after the constructor
  aabb bounds() const override
  {
    const vec3 rad(radius, radius, radius);
    return aabb{center - rad, center + rad};
  }
```

```cpp
// tracer/hittable_list.h  — before hit()
  aabb bounds() const override
  {
    aabb b = aabb::empty();
    for (const auto &obj : objects)
    {
      b.expand(obj->bounds());
    }
    return b;
  }
```

`hittable_list` is not on the render path any more — `load_scene` builds a `scene` — but it is
still a `hittable` and it still has to answer. It stays a linear scan on purpose: it is the
"a handful of things, no ceremony" container, and `scene` is the one with a tree.

```cpp
// tracer/instance.h  — after commit()

  // The eight corners of the prototype's object-space box, transformed, and the
  // box of the result. Not the transformed box's own corners - that would be a
  // box of a box and would grow on every re-commit under rotation.
  aabb bounds() const override
  {
    if (!valid) return aabb::empty();

    const aabb local = proto->bounds();
    if (local.is_empty()) return aabb::empty();

    aabb world = aabb::empty();
    for (int i = 0; i < 8; i++)
    {
      const point3 corner(i & 1 ? local.hi[0] : local.lo[0],
                          i & 2 ? local.hi[1] : local.lo[1],
                          i & 4 ? local.hi[2] : local.lo[2]);
      world.expand(xform.transform(corner));
    }
    return world;
  }
```

Three details worth the words:

- **`xform`, not `inv`.** The box goes object→world; the *ray* goes world→object. Getting this
  backwards produces a box that is right for identity and axis-aligned scales and wrong the moment
  anything rotates — which is scene 4, and which the goldens catch.
- **Eight corners, from the prototype's box, every time.** Transforming the previous *world* box's
  corners instead would grow the box on every re-commit under rotation, without bound. Step 3
  gates idempotence for exactly this reason.
- **`!valid` returns empty**, matching `hit()`'s `if (!valid) return false;`. A singular transform
  vanishes at both ends, consistently.

`mesh` and `scene` get theirs in steps 6 and 7, where their trees appear.

---

# Step 3 — GATE 1: the box is a box, and the two guards that are not hygiene

Nothing has a tree yet, so this gate is pure geometry and it should pass first time. Two of its
five checks are **negative controls** — they assert that the obvious simplification is wrong.

```cpp
// $S/gates.cpp — the aabb half
static void gate_flat_box()
{
  // The z-extent is exactly zero, as it is for every axis-aligned triangle.
  const aabb flat{point3(-1, -1, -1), point3(1, 1, -1)};

  rng gen(12345);
  int hits = 0, rejected_by_le = 0;
  for (int i = 0; i < 20000; i++)
  {
    const point3 o(gen.uniform(-0.9, 0.9), gen.uniform(-0.9, 0.9), 3);
    const slab_ray s(ray(o, vec3(0, 0, -1)));
    if (slab_hit(flat, s, 0.001, infinity)) hits++;

    // what `tmax <= tmin` would have done
    double tmin = 0.001, tmax = infinity;
    bool rejected = false;
    for (int a = 0; a < 3 && !rejected; a++)
    {
      double t0 = (flat.lo[a] - s.o[a]) * s.inv[a];
      double t1 = (flat.hi[a] - s.o[a]) * s.inv[a];
      if (s.neg[a]) std::swap(t0, t1);
      if (t0 > tmin) tmin = t0;
      if (t1 < tmax) tmax = t1;
      if (tmax <= tmin) rejected = true;
    }
    if (rejected) rejected_by_le++;
  }

  check(hits == 20000,           "flat box is hit by every ray that crosses it");
  check(rejected_by_le == 20000, "  ...and `tmax <= tmin` would reject all of them");
}
```

Also assert, in the same file: the NaN case really is NaN and passes anyway; an empty box is hit
by no ray, changes nothing when expanded into another, and has a finite centroid; every point of a
sphere is inside `sphere::bounds()`; all eight corners of a rotated, non-uniformly-scaled mesh are
inside `instance::bounds()`; `instance::bounds()` is unchanged by a second call; a singular
instance reports empty.

Expected output:

```
flat box is hit by every ray that crosses it               PASS (strict: 20000/20000 hit; with <=: 20000/20000 rejected)
  ...and `tmax <= tmin` would reject all of them           PASS
the parallel-and-on-the-plane case really is NaN           PASS (t0 = -nan)
  ...and the slab test passes it anyway (conservative)     PASS
an empty aabb is hit by no ray                             PASS
expanding by an empty box changes nothing                  PASS
an empty box's centroid is not NaN                         PASS
every point of a sphere is inside sphere::bounds()         PASS
every corner of a transformed mesh is inside instance::bounds() PASS
instance::bounds() is idempotent under re-commit           PASS
a singular instance reports an empty box                   PASS
```

### Why the flat-box case deserves its own gate

This was found by running the implementation, not by reading it. With `tmax <= tmin`, scenes 0–4
and 6 all rendered **byte-identical** and scene 5 came back with **31 273 of 90 000 pixels wrong**
— the triangle and the ground behind it replaced by sky. Every mesh in scenes 0–4 and 6 is a
sphere approximation, so no triangle is exactly axis-aligned and no box has exactly zero extent.
Scene 5's single triangle lies in the plane `z = -1`.

A ground plane is two triangles of exactly that shape. So is every wall of a Cornell box. The
first asset that hits this in anger would look like a material bug or a winding bug, and the six
scenes that pass would argue convincingly that the BVH was fine.

---

# Step 4 — Write `tracer/bvh.h`

One class, two entry points that matter (`build`, `hit`) and one that makes the roadmap item real
(`refit`). Write it in one go; the pieces do not test independently, and step 10 tests all of it
at once against a linear scan.

The five decisions inside it, before the listing:

**1. It stores a tree and a permutation, and knows nothing about primitives.** `build()` takes
`std::vector<aabb>` and fills `order()`; the caller permutes its own payload. `hit()` takes a leaf
callback. That is the entire reason one implementation serves both `mesh` (triangles) and `scene`
(prims) — and the reason a third user (an instancer, a curve set) costs nothing later.

**2. Two array orders exist, and they are the one real hazard.** `build(boxes)` takes the
*caller's* order; `refit(boxes)` takes *BVH* order. Passing the wrong one to `refit` produces a
tree whose boxes do not contain their primitives — a silent wrong-answer bug, not a crash. Both
call sites in this task are three lines apart from the `order()` they use; keep them that way.

**3. Depth-first layout, right child at `left + 1`.** Children always live at a higher index than
their parent, which is what makes `refit`'s single reverse sweep a correct post-order and what
makes the near-child-first traversal one `std::swap`.

**4. `max_depth` is what makes the fixed traversal stack safe.** One stack entry per level, and
`max_depth (60) < max_stack (64)`. Without the cap this is a buffer overrun on real-shaped input —
step 11 has the 100 000-coplanar-box case that reached depth 98.

**5. The SAH's leaf test, not `leaf_size`, is what actually terminates.** `leaf_size = 1` means
"never stop before asking"; the measured mean leaf is 1.05–1.73. `max_leaf` is a separate cap that
keeps a degenerate node from becoming a linear scan.

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "tracer.h"
#include "aabb.h"
#include "hittable.h"
#include "interval.h"


// A binary BVH built with a binned surface-area heuristic.
//
// The class owns the tree and nothing else: it is handed an array of boxes and
// hands back a permutation, and its traversal calls a leaf callback the owner
// supplies. That is what lets one implementation serve both levels - `mesh`
// puts triangles in it, `scene` puts prims in it - without either of them
// knowing about the other.
//
// Two array orders exist, and confusing them is the one real hazard here:
//
//   build(boxes)  takes boxes in the CALLER's order and fills order() so that
//                 order()[i] is the caller index of the i'th primitive in BVH
//                 order. The caller permutes its payload to match.
//   refit(boxes)  takes boxes already in BVH order - that is, in the order the
//                 caller permuted its payload into. Topology is untouched.
class bvh
{
public:
  // Traversal cost against primitive-intersection cost, in the same units. The
  // SAH only reads their ratio; every measurement in the plan used 1:1.
  static constexpr double cost_traverse = 1.0;
  static constexpr double cost_intersect = 1.0;

  // Refit until the tree costs this much more than it did when it was built,
  // then rebuild. Measured: the traversal cost of a refitted tree tracks this
  // ratio closely, and 1.3 is where it starts to run away.
  static constexpr double rebuild_ratio = 1.3;

  // Stop subdividing at or below `leaf_size` without even trying a split, and
  // never emit a leaf larger than `max_leaf` even when the SAH asks for one.
  // leaf_size = 1 does NOT mean one primitive per leaf: the SAH's own
  // leaf-vs-split test still terminates early, and the measured mean leaf is
  // 1.05 prims on a sphere scene and 1.73 on a mesh.
  int leaf_size = 1;
  int max_leaf = 16;
  int bin_count = 12;

  struct node
  {
    aabb box;
    int32_t left_first = 0;  // interior: index of the left child; leaf: first prim
    int32_t count = 0;       // 0 == interior node
    int32_t axis = 0;        // split axis, for front-to-back child ordering
  };

  void build(const std::vector<aabb> &boxes)
  {
    _nodes.clear();
    _order.clear();
    _depth = 0;
    _built_cost = _cost = 0;

    const size_t n = boxes.size();
    if (n == 0)
    {
      return;
    }

    _refs.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      _refs[i] = {boxes[i], boxes[i].centroid(), int32_t(i)};
    }

    _nodes.reserve(2 * n);
    _nodes.emplace_back();
    build_node(0, 0, int32_t(n), 1);

    _order.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      _order[i] = _refs[i].index;
    }

    _refs.clear();
    _refs.shrink_to_fit();

    _built_cost = _cost = sah_cost();
  }

  // Bottom-up box refresh, topology untouched. `boxes` must be in BVH order.
  // Children always live at a higher index than their parent, so one reverse
  // sweep is a correct post-order.
  //
  // Returns the refitted tree's SAH cost, accumulated in the same sweep: the
  // caller needs it on every refit to decide whether to rebuild, and a
  // separate sah_cost() pass would cost more than the refit did.
  double refit(const std::vector<aabb> &boxes)
  {
    double sum = 0;

    for (int32_t i = int32_t(_nodes.size()) - 1; i >= 0; i--)
    {
      node &nd = _nodes[i];
      if (nd.count > 0)
      {
        nd.box = aabb::empty();
        for (int32_t k = 0; k < nd.count; k++)
        {
          nd.box.expand(boxes[nd.left_first + k]);
        }
        sum += cost_intersect * nd.box.surface_area() * nd.count;
      }
      else
      {
        nd.box = merge(_nodes[nd.left_first].box, _nodes[nd.left_first + 1].box);
        sum += cost_traverse * nd.box.surface_area();
      }
    }

    const double root = _nodes.empty() ? 0 : _nodes[0].box.surface_area();
    _cost = root > 0 ? sum / root : 0;
    return _cost;
  }

  bool empty() const { return _nodes.empty(); }
  size_t node_count() const { return _nodes.size(); }
  int depth() const { return _depth; }
  const std::vector<int32_t> &order() const { return _order; }
  const std::vector<node> &nodes() const { return _nodes; }

  aabb bounds() const
  {
    return _nodes.empty() ? aabb::empty() : _nodes[0].box;
  }

  double built_cost() const { return _built_cost; }
  double cost() const { return _cost; }
  bool degraded() const { return _built_cost > 0 && _cost > rebuild_ratio * _built_cost; }

  // What the SAH is minimising: the expected cost of a ray that hits the root
  // box, in units of one primitive intersection. A linear scan over n prims
  // costs n; this is the honest way to compare two builders, and the only one
  // that is not a stopwatch.
  double sah_cost() const
  {
    if (_nodes.empty()) return 0;

    const double root = _nodes[0].box.surface_area();
    if (root <= 0) return 0;

    double c = 0;
    for (const node &nd : _nodes)
    {
      const double a = nd.box.surface_area() / root;
      c += nd.count > 0 ? cost_intersect * a * nd.count : cost_traverse * a;
    }

    return c;
  }

  // leaf(first, count, clip, info) -> true if it wrote a hit nearer than
  // clip.max. `info.t` is read back to tighten the traversal, so a leaf that
  // returns true must have set it.
  template <class LeafFn>
  bool hit(const ray &r, interval clip, hit_info &info, LeafFn leaf) const
  {
    if (_nodes.empty()) return false;

    const slab_ray s(r);
    double closest = clip.max;
    bool did_hit = false;

    // One entry per level, and build_node() caps the depth below max_stack, so
    // this cannot overflow. See max_depth.
    int32_t stack[max_stack];
    double stack_t[max_stack];
    int sp = 0;
    int32_t cur = 0;

    if (!slab_hit(_nodes[0].box, s, clip.min, closest))
    {
      return false;
    }

    for (;;)
    {
      const node &nd = _nodes[cur];

      if (nd.count > 0)
      {
        if (leaf(nd.left_first, nd.count, interval(clip.min, closest), info))
        {
          did_hit = true;
          closest = info.t;
        }
      }
      else
      {
        // Near child first. The far one goes on the stack with the distance at
        // which it would be entered, so that a hit found in the near subtree
        // can discard it without a second box test.
        int32_t a = nd.left_first;
        int32_t b = nd.left_first + 1;
        if (s.neg[nd.axis]) std::swap(a, b);

        double ta = 0, tb = 0;
        const bool ha = slab_enter(_nodes[a].box, s, clip.min, closest, ta);
        const bool hb = slab_enter(_nodes[b].box, s, clip.min, closest, tb);

        if (ha)
        {
          if (hb)
          {
            stack_t[sp] = tb;
            stack[sp++] = b;
          }
          cur = a;
          continue;
        }
        if (hb)
        {
          cur = b;
          continue;
        }
      }

      for (;;)
      {
        if (sp == 0) return did_hit;
        sp--;
        if (stack_t[sp] < closest)
        {
          cur = stack[sp];
          break;
        }
      }
    }
  }

private:
  static constexpr int max_stack = 64;
  static constexpr int max_bins = 64;

  // Strictly less than max_stack, which is what makes the traversal stack
  // provably safe. Only degenerate input ever reaches it; real geometry
  // measured 25 at a million primitives.
  static constexpr int max_depth = 60;

  struct prim_ref
  {
    aabb box;
    point3 centroid;
    int32_t index;
  };

  struct bin
  {
    aabb box = aabb::empty();
    int32_t count = 0;
  };

  std::vector<node> _nodes;
  std::vector<int32_t> _order;
  std::vector<prim_ref> _refs;
  int _depth = 0;
  double _built_cost = 0;
  double _cost = 0;

  // As slab_hit, but also reports the entry distance, which the traversal uses
  // to order and cull the stack.
  static bool slab_enter(const aabb &b, const slab_ray &s, double tmin, double tmax, double &enter)
  {
    for (int a = 0; a < 3; a++)
    {
      double t0 = (b.lo[a] - s.o[a]) * s.inv[a];
      double t1 = (b.hi[a] - s.o[a]) * s.inv[a];
      if (s.neg[a]) std::swap(t0, t1);

      if (t0 > tmin) tmin = t0;
      if (t1 < tmax) tmax = t1;
      if (tmax < tmin) return false;   // strictly less-than: see slab_hit
    }

    enter = tmin;
    return true;
  }

  void make_leaf(int32_t index, int32_t first, int32_t count)
  {
    _nodes[index].left_first = first;
    _nodes[index].count = count;
  }

  // Split the range at its centroid median on `axis`. The fallback whenever the
  // SAH has nothing to say: coincident centroids, zero-area geometry, or a
  // partition that came back empty on one side.
  void median_split(int32_t index, int32_t first, int32_t count, int axis, int depth)
  {
    const auto begin = _refs.begin() + first;
    const int32_t left_n = count / 2;
    std::nth_element(begin, begin + left_n, begin + count,
                     [&](const prim_ref &a, const prim_ref &b) {
                       return a.centroid[axis] < b.centroid[axis];
                     });
    split_children(index, first, count, left_n, axis, depth);
  }

  void split_children(int32_t index, int32_t first, int32_t count, int32_t left_n, int axis, int depth)
  {
    const int32_t left = int32_t(_nodes.size());
    _nodes.emplace_back();
    _nodes.emplace_back();

    _nodes[index].left_first = left;
    _nodes[index].count = 0;
    _nodes[index].axis = axis;

    // By index, never by reference: the emplace_back above can reallocate.
    build_node(left, first, left_n, depth + 1);
    build_node(left + 1, first + left_n, count - left_n, depth + 1);
  }

  void build_node(int32_t index, int32_t first, int32_t count, int depth)
  {
    if (depth > _depth) _depth = depth;

    aabb box = aabb::empty();
    aabb centroids = aabb::empty();
    for (int32_t i = first; i < first + count; i++)
    {
      box.expand(_refs[i].box);
      centroids.expand(_refs[i].centroid);
    }
    _nodes[index].box = box;

    if (count <= leaf_size || depth >= max_depth)
    {
      make_leaf(index, first, count);
      return;
    }

    // Widest axis of the CENTROID bounds, not of the box: the box can be wide
    // on an axis every centroid shares, and binning that axis puts every
    // primitive in one bin.
    const vec3 ext = centroids.extent();
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;

    // Zero surface area makes every SAH candidate cost zero, so the sweep picks
    // the first one it sees and the tree degenerates into a list - measured at
    // depth 98 over 100k coplanar boxes, which overruns the traversal stack.
    // Flat geometry is not exotic: a ground plane is two triangles like this.
    if (!(box.surface_area() > 0) || !(ext[axis] > 0))
    {
      if (count <= max_leaf)
      {
        make_leaf(index, first, count);
        return;
      }
      median_split(index, first, count, axis, depth);
      return;
    }

    // --- bin ---------------------------------------------------------------
    // Stack arrays, not vectors. build_node() runs once per node, so three heap
    // allocations here are three per node: measured at 487 prims, that alone
    // was two thirds of the build.
    bin bins[max_bins];
    for (int i = 0; i < bin_count; i++) bins[i] = bin();

    const double lo = centroids.lo[axis];
    const double scale = bin_count / ext[axis];

    for (int32_t i = first; i < first + count; i++)
    {
      int b = int((_refs[i].centroid[axis] - lo) * scale);
      b = std::min(std::max(b, 0), bin_count - 1);
      bins[b].box.expand(_refs[i].box);
      bins[b].count++;
    }

    // --- sweep -------------------------------------------------------------
    // left_area[i] describes bins [0, i]; the candidate between bin i and i+1
    // is evaluated against the suffix accumulated on the way back down. Two
    // linear passes, not O(bins^2).
    double left_area[max_bins];
    int32_t left_count[max_bins];
    {
      aabb acc = aabb::empty();
      int32_t n = 0;
      for (int i = 0; i < bin_count; i++)
      {
        acc.expand(bins[i].box);
        n += bins[i].count;
        left_area[i] = acc.surface_area();
        left_count[i] = n;
      }
    }

    double best_cost = infinity;
    int best_bin = -1;
    {
      aabb acc = aabb::empty();
      int32_t n = 0;
      for (int i = bin_count - 1; i > 0; i--)
      {
        acc.expand(bins[i].box);
        n += bins[i].count;
        if (left_count[i - 1] == 0 || n == 0) continue;

        const double c = left_area[i - 1] * left_count[i - 1] + acc.surface_area() * n;
        if (c < best_cost)
        {
          best_cost = c;
          best_bin = i - 1;     // the left side is bins [0, best_bin]
        }
      }
    }

    const double parent_area = box.surface_area();
    const double leaf_cost = cost_intersect * parent_area * count;
    const double split_cost = cost_traverse * parent_area + cost_intersect * best_cost;

    if (best_bin < 0 || (split_cost >= leaf_cost && count <= max_leaf))
    {
      make_leaf(index, first, count);
      return;
    }

    // --- partition ---------------------------------------------------------
    const auto begin = _refs.begin() + first;
    const auto end = begin + count;
    const auto mid = std::partition(begin, end, [&](const prim_ref &p) {
      int b = int((p.centroid[axis] - lo) * scale);
      b = std::min(std::max(b, 0), bin_count - 1);
      return b <= best_bin;
    });

    const int32_t left_n = int32_t(mid - begin);
    if (left_n == 0 || left_n == count)
    {
      // The bin the SAH chose and the bin std::partition computed disagreed,
      // which rounding can do at a bin edge. Falling through would recurse on
      // an empty child - infinite recursion, not a slow tree.
      median_split(index, first, count, axis, depth);
      return;
    }

    split_children(index, first, count, left_n, axis, depth);
  }
};
```

### Reading the build, once

`build_node(index, first, count, depth)` owns the range `_refs[first, first+count)` and the
already-allocated node at `index`. In order:

1. **Bounds.** One pass for the primitive box (stored on the node) and the *centroid* box (used
   for the split). They are different boxes and the difference matters: a node can be wide on an
   axis every centroid shares, and binning that axis puts every primitive in one bin.
2. **Terminate?** `count <= leaf_size`, or `depth >= max_depth`.
3. **Degenerate?** Zero surface area, or zero centroid extent on the widest axis → median split
   (or a leaf, if it is small enough). This is the branch that turns the pathological cases from
   depth 98 into depth 14.
4. **Bin.** 12 bins on the widest centroid axis, accumulating a box and a count each. Stack
   arrays: this runs once per *node*.
5. **Sweep.** A forward pass accumulating left area and count, then a backward pass accumulating
   the right and evaluating each of the 11 candidates. Two linear passes, not O(bins²).
6. **Decide.** `Ct·A(P) + Ci·(A(L)·N(L) + A(R)·N(R))` against `Ci·A(P)·N`. Make a leaf if
   splitting is not worth it *and* the leaf would not exceed `max_leaf`.
7. **Partition and recurse.** `std::partition` on the same bin predicate. If it comes back empty
   on one side — which rounding at a bin edge can do — fall back to a median split rather than
   recursing on an empty child, which is infinite recursion, not a slow tree.

`split_children` allocates both children **before** recursing, and passes indices rather than
references, because `_nodes.emplace_back()` can reallocate the vector out from under a reference.
`reserve(2n)` makes that unlikely, not impossible; `leaf_size = 1` produces up to `2n - 1` nodes.

### Reading the traversal

A leaf callback with the signature `bool(int32_t first, int32_t count, interval clip, hit_info&)`,
returning true when it wrote something nearer than `clip.max`. The traversal reads `info.t` back
to tighten `closest`, which is why a leaf that returns true must have set it.

Two things earn their complexity:

- **Near child first**, chosen by `s.neg[nd.axis]`. Without it the far subtree is often entered
  before the near one has produced a hit to cull it with.
- **The stack carries the entry distance.** On pop, an entry whose entry distance is already
  behind `closest` is discarded without a box test. That is the payoff for ordering the children:
  the culled entries are the ones the near subtree just made irrelevant.

`slab_enter` is `slab_hit` plus the entry distance. They are duplicated rather than shared because
`slab_hit` is also the interior-node reject in a hot loop and the extra out-parameter measurably
costs there; the comment in `slab_enter` points back at `slab_hit` for the two guards.

---

# Step 5 — `mesh` gets a BLAS

`mesh::hit`'s Möller–Trumbore loop is **not modified**. It moves, verbatim, into a private
`intersect(r, first, count, clip, out)` that scans a *range* of `geom` instead of all of it, and
becomes the leaf callback. Everything [[triangle-mesh]] gated about it — the `|det|` guard, the
geometric-vs-shading normal split, the write-once rule — is unchanged and stays gated.

Three additions:

**`bounds()` is the tree's root.** `accel.bounds()`, not a separately maintained box. One thing to
keep current, current by construction.

**`geom` is reordered, and `tri_index` remembers where each triangle came from.** The build hands
back a permutation; `geom` is permuted to match so that a leaf is a contiguous run. But `face` and
the vertex normals are still indexed by the *original* triangle number, so:

```cpp
info.element_id = face.empty() ? f : face[f];     // f == tri_index[slot]
const int32_t i0 = tris[3 * f + 0];               // ...and the normals likewise
```

One extra indirection, paid only on the winning triangle. [[triangle-mesh]] chose a flat
contiguous `geom` array over indices into `verts` partly for this: reordering 72-byte records is a
permutation of one array.

**A linear bypass below `linear_threshold` triangles.** The traversal pays three divides building
the slab ray before it tests anything; a one-triangle mesh never earns that back. Step 9 measures
where the crossover is.

```cpp
#pragma once

#include <memory>
#include <vector>

#include "bvh.h"
#include "hittable.h"
#include "vert.h"


class mesh : public hittable
{
public:
  std::vector<vec3> verts;
  std::vector<vec3> normals;
  std::vector<int32_t> tris;
  std::vector<int32_t> face;
  shared_ptr<material> mat;

  size_t triangle_count() const
  {
    return tris.size() / 3;
  }

  void add_triangle(const point3 &a, const point3 &b, const point3 &c)
  {
    const int32_t base = int32_t(verts.size());
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
    tris.push_back(base);
    tris.push_back(base+1);
    tris.push_back(base+2);
  }

  void add_triangle(const vertex &a, const vertex &b, const vertex &c)
  {
    add_triangle(a.p, b.p, c.p);
    normals.push_back(a.n);
    normals.push_back(b.n);
    normals.push_back(c.n);
  }

  aabb bounds() const override { return accel.bounds(); }

  void commit() override
  {
    const size_t count = triangle_count();

    geom.clear();
    geom.reserve(count);

    std::vector<aabb> boxes;
    boxes.reserve(count);

    // Refit first when the triangle count has not moved. `tri_index` stays a
    // permutation of every triangle whatever the indices now say, so this is
    // always CORRECT - only the tree's shape can go stale, and the SAH ratio
    // below is what notices.
    if (!accel.empty() && tri_index.size() == count)
    {
      for (int32_t f : tri_index)
      {
        const vec3 &p0 = verts[tris[3*f + 0]];
        const vec3 &p1 = verts[tris[3*f + 1]];
        const vec3 &p2 = verts[tris[3*f + 2]];
        geom.push_back({p0, p1-p0, p2-p0});

        aabb b = aabb::empty();
        b.expand(p0);
        b.expand(p1);
        b.expand(p2);
        boxes.push_back(b);
      }

      accel.refit(boxes);
      if (!accel.degraded())
      {
        return;
      }

      // Degraded, so fall through to the rebuild below. Both arrays are full
      // at this point, and in the OLD BVH order: `build()` has to be handed
      // exactly `count` boxes, and filling them a second time would hand it
      // 2*count - `order()` would come back the wrong length, `tri_index` with
      // it, and `sorted` would be indexed against triangles that are not
      // there. No crash, just wrong answers. The rebuild reuses the capacity
      // the refit just paid for.
      geom.clear();
      boxes.clear();
    }

    for (size_t i = 0; i+2 < tris.size(); i += 3)
    {
      const vec3 &p0 = verts[tris[i]];
      const vec3 &p1 = verts[tris[i+1]];
      const vec3 &p2 = verts[tris[i+2]];
      geom.push_back({p0, p1-p0, p2-p0});

      aabb b = aabb::empty();
      b.expand(p0);
      b.expand(p1);
      b.expand(p2);
      boxes.push_back(b);
    }

    accel.build(boxes);

    // Reorder `geom` into BVH order so a leaf is a contiguous run, and keep the
    // permutation: `tri_index[slot]` is the index this triangle had in `tris`,
    // which is what `face` and the vertex normals are still indexed by.
    const std::vector<int32_t> &order = accel.order();
    std::vector<tri_geom> sorted;
    sorted.reserve(order.size());
    tri_index.assign(order.begin(), order.end());
    for (int32_t src : order)
    {
      sorted.push_back(geom[src]);
    }
    geom.swap(sorted);
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    if (geom.size() <= linear_threshold)
    {
      return intersect(r, 0, int32_t(geom.size()), clipping_range, info);
    }

    return accel.hit(r, clipping_range, info,
      [&](int32_t first, int32_t count, interval clip, hit_info &out)
      {
        return intersect(r, first, count, clip, out);
      });
  }

  // Below this many triangles the tree costs more than it saves: the traversal
  // pays three divides to build the slab ray before it tests anything.
  // Measured in step 9.
  static constexpr size_t linear_threshold = 4;

private:
  bool intersect(const ray &r, int32_t first, int32_t count, interval clip, hit_info &out) const
  {
    int32_t best = -1;
    double closest = clip.max;
    double bu = 0, bv = 0;

    for (int32_t f = first; f < first + count; f++)
    {
      const tri_geom &g = geom[f];

      // Moller-Trumbore. The direction is NOT normalized, so `t` comes back in
      // the caller's units - the invariant `instance` and the 0.001 acne
      // epsilon both depend on.
      const vec3 pv = cross(r.direction(), g.e2);
      const double det = dot(g.e1, pv);

      // case 1: ray does not intersect with the plane containing this tri, does not hit...
      if (std::fabs(det) < 1e-12) continue;

      const double inv_det = 1.0 / det;
      const vec3 tv = r.origin() - g.p0;

      // case 2: check intersection is in the triangle
      // u coordinate is out of bounds, early exit
      const double u = dot(tv, pv) * inv_det;
      if (u < 0 || u > 1) continue;

      const vec3 qv = cross(tv, g.e1);
      const double v = dot(r.direction(), qv) * inv_det;
      if (v < 0 || u + v > 1) continue;

      const double t = dot(g.e2, qv) * inv_det;
      if (t <= clip.min || t >= closest) continue;

      best = f;
      closest = t;
      bu = u;
      bv = v;
    }

    if (best < 0) return false;

    fill(r, best, closest, bu, bv, out);
    return true;
  }
```

...and the rest of the file: the payload type, the members, and `fill()` — the tail of the
old `hit()`, unchanged, factored out so that "a hit is written exactly once, on success"
stays a property you can see in one place.

```cpp
  struct tri_geom
  {
    vec3 p0, e1, e2;
  };

  std::vector<tri_geom> geom;
  std::vector<int32_t> tri_index;
  bvh accel;

  // Everything written to hit_info, in one place, so that a hit is written
  // exactly once and only on success - the invariant scene::hit's stale-id
  // reset depends on.
  void fill(const ray &r, int32_t slot, double t, double bu, double bv, hit_info &info) const
  {
    const tri_geom &g = geom[slot];
    const vec3 ng = cross(g.e1, g.e2);   // geometric normal, not unit
    const int32_t f = tri_index[slot];

    // calculate normal
    vec3 ns = ng;
    if (!normals.empty())
    {
      const int32_t i0 = tris[3 * f + 0];
      const int32_t i1 = tris[3 * f + 1];
      const int32_t i2 = tris[3 * f + 2];
      ns = (1 - bu - bv) * normals[i0] + bu * normals[i1] + bv * normals[i2];

      // Authored normals that cancel out, or that disagree with the winding.
      // Both are bad data; neither should produce a black pixel or a scatter
      // into the surface.
      if (ns.near_zero())       ns = ng;
      else if (dot(ns, ng) < 0) ns = -ns;
    }

    info.t = t;
    info.p = r.at(t);
    info.mat = mat.get();
    info.element_id = face.empty() ? f : face[f];
    info.set_face_normal(r, ng, unit_vector(ns));
  }
};
```

Note the members: `geom` (BVH order), `tri_index` (BVH slot → original triangle), `accel`.
`verts`, `normals`, `tris` and `face` are untouched, still in authored order, still what Hydra's
`Sync` assigns straight into.

---

# Step 6 — `scene` gets a TLAS

The same shape one level up, and the old loop survives verbatim in two places at once.

**`_draw` splits into two vectors.** `_visible` is the draw list in insertion order — the key that
tells a later `commit()` whether the tree still describes this set of prims. `_draw` is the same
list permuted into BVH order — the leaf payload. Both hold the same `entry { const hittable*,
int32_t prim_id }` that [[scene-graph]] introduced, so the leaf callback *is* the old loop body,
`prim_id` stamping and stale-id reset included.

**`linear_hit()` is the old `hit()`, renamed and kept.** It is not dead code: `hit()` calls it
below `linear_threshold`, and step 10's gate asserts the tree against it on every ray of every
scene. Keeping the reference implementation next to the optimised one, in the same file, is the
cheapest possible insurance for the invariant that matters most.

```cpp
  void commit() override
  {
    if (!_dirty.exchange(false, std::memory_order_acq_rel))
    {
      return;
    }

    // `_visible` is the draw list in INSERTION order; `_draw` is the same
    // list permuted into BVH order. Both are kept: the first is what tells a
    // later commit whether the tree still describes this set of prims.
    std::vector<entry> visible;
    for (const record &r : _slots)
    {
      if (r.prim == nullptr) continue;

      r.prim->commit();
      if (r.visible)
      {
        visible.push_back({r.prim.get(), r.prim_id});
      }
    }

    // Every prim has already committed, so bounds() is current.
    const bool same_set = !_tlas.empty() && visible.size() == _visible.size()
        && std::equal(visible.begin(), visible.end(), _visible.begin(),
                      [](const entry &a, const entry &b) {
                        return a.prim == b.prim && a.prim_id == b.prim_id;
                      });

    _visible.swap(visible);

    std::vector<aabb> boxes;
    boxes.reserve(_visible.size());

    if (same_set)
    {
      // Only the prims' own bounds can have moved. Refit in BVH order and keep
      // the topology - a transform edit, which is the common Hydra case, gets
      // out of here in ~2% of a rebuild.
      for (int32_t src : _tlas.order())
      {
        boxes.push_back(_visible[src].prim->bounds());
      }

      _tlas.refit(boxes);
      if (!_tlas.degraded())
      {
        return;
      }

      boxes.clear();
    }

    for (const entry &e : _visible)
    {
      boxes.push_back(e.prim->bounds());
    }

    _tlas.build(boxes);

    _draw.clear();
    _draw.reserve(_visible.size());
    for (int32_t src : _tlas.order())
    {
      _draw.push_back(_visible[src]);
    }
  }

  aabb bounds() const override { return _tlas.bounds(); }

  // The pre-BVH linear scan, kept as the reference implementation the gates
  // assert the tree against. Nothing in the render path calls it; it is here
  // rather than in a test file because `_draw` is private and an accessor for
  // it would be a worse thing to leave behind.
  bool linear_hit(const ray &r, interval clipping_range, hit_info &info) const
  {
    hit_info temp_info;
    bool did_hit = false;
    double closest = clipping_range.max;

    for (const entry &e : _draw)
    {
      if (!e.prim->hit(r, interval(clipping_range.min, closest), temp_info))
      {
        continue;
      }

      did_hit = true;
      closest = temp_info.t;
      temp_info.prim_id = e.prim_id;
      info = temp_info;

      temp_info.instance_id = -1;
      temp_info.element_id = -1;
    }

    return did_hit;
  }

  // Below this many prims a linear scan beats the tree: the traversal pays
  // three divides to build the slab ray before it tests anything, and a scene
  // of five spheres never earns that back. Measured in step 9.
  static constexpr size_t linear_threshold = 8;

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    if (_draw.size() <= linear_threshold)
    {
      return linear_hit(r, clipping_range, info);
    }

    return _tlas.hit(r, clipping_range, info,
      [&](int32_t first, int32_t count, interval clip, hit_info &out)
      {
        hit_info temp_info;
        bool did_hit = false;
        double closest = clip.max;

        for (int32_t i = first; i < first + count; i++)
        {
          const entry &e = _draw[i];
          if (!e.prim->hit(r, interval(clip.min, closest), temp_info))
          {
            continue;
          }

          did_hit = true;
          closest = temp_info.t;
          temp_info.prim_id = e.prim_id;
          out = temp_info;

          // The stale-id reset. A prim that does not write instance_id or
          // element_id must not inherit the last one that did - see
          // [[scene-graph]] step 9.
          temp_info.instance_id = -1;
          temp_info.element_id = -1;
        }

        return did_hit;
      });
  }
```

...and the members:

```cpp
  std::vector<record> _slots;
  std::vector<prim_handle> _free;
  std::vector<entry> _visible;   // insertion order
  std::vector<entry> _draw;      // BVH order
  bvh _tlas;
  size_t _live = 0;
```

`scene.h` also gains `#include <algorithm>` (for `std::equal`) and `#include "bvh.h"`.

**`commit()`'s ordering is a contract, not an accident.** `r.prim->commit()` runs for every prim
*before* any `bounds()` is read. A mesh's BLAS is therefore built before the instance wrapping it
is asked for a world box, which is what makes `instance::bounds()` → `mesh::bounds()` →
`accel.bounds()` correct rather than one frame stale. If you ever reorder this loop, that is the
thing that breaks, and it breaks silently.

---

# Step 7 — Refit, and the trigger that decides

This is the `-on-mutation` half of the roadmap item, and it is worth being precise about what it
buys and what it risks.

**Refit** recomputes every node's box bottom-up and leaves the topology alone: O(n), one reverse
sweep, **52× cheaper than a rebuild** at 483 prims (0.0032 ms vs 0.164 ms). It is always
*correct* — a box that contains its primitives is a valid BVH node whatever shape the tree is —
and progressively *worse*, because the topology was chosen for where the primitives used to be.

**The trigger is the SAH cost ratio**, not a frame counter, not a distance heuristic, and not the
caller's guess about what changed. `refit()` accumulates the new cost in the same sweep it is
already doing, so asking is free; a separate `sah_cost()` pass would cost more than the refit did.

Measured degradation — 483 spheres given random velocities, stepped, refitting only:

| step | SAH (refit) | SAH (rebuilt) | ratio | node tests/ray | prim tests/ray |
|---|---|---|---|---|---|
| 0 | 9.464 | 9.464 | 1.00× | 0.37 / 0.37 | 0.39 / 0.39 |
| 2 | 10.250 | 9.467 | 1.08× | 0.36 / 0.34 | 0.40 / 0.40 |
| **4** | **11.742** | **9.207** | **1.28×** | 0.38 / 0.33 | 0.44 / 0.39 |
| 6 | 13.692 | 9.242 | 1.48× | 0.42 / 0.32 | 0.52 / 0.36 |
| 9 | 17.068 | 9.275 | 1.84× | 0.48 / 0.31 | 0.66 / 0.34 |
| 12 | 20.731 | 9.268 | 2.24× | 0.57 / 0.30 | 0.84 / 0.33 |
| 14 | 23.270 | 9.427 | 2.47× | 0.63 / 0.30 | 0.98 / 0.31 |

The last two columns are *measured* traversal work over 40 000 rays, not model output. They track
the SAH ratio closely, which is the whole justification for using the ratio as the trigger.

**`rebuild_ratio = 1.3`** lands between steps 3 and 4: still within ~10% of a fresh tree's
measured traversal cost, and about a dozen refits per rebuild on a scene that is actually moving.
It is one `static constexpr` and there is no scene in the repo whose behaviour is delicate around
it.

The two call sites, both already in the listings above:

- **`scene::commit()`** refits when `_visible` is the same list of prims in the same order.
  Prims that only moved — the common Hydra case, `DirtyTransform` without `DirtyTopology` — take
  that path. Anything inserted, removed, or made visible falls through to a rebuild, because
  `same_set` is false and the permutation would not line up.
- **`mesh::commit()`** refits when the triangle count has not changed, rebuilding `geom` *through*
  `tri_index` so the payload stays in BVH order. `DirtyPoints` without `DirtyTopology` — a
  deforming character — takes that path.

**`mesh`'s guard is a count, and that is deliberately weak.** If the topology changed while the
count stayed the same, `tri_index` is still a permutation of every triangle, so every triangle is
still present exactly once with a box that contains it: the tree is **correct**, only badly
shaped, and the SAH ratio notices and rebuilds. Correctness never rests on the guess.

---

# Step 8 — GATE 2: the seven scenes do not move

The headline gate. A BVH changes the order primitives are tested in and nothing else, so every
image must come back **byte-identical** — not "within a channel", identical.

```bash
cd ~/git/weekend-raytracer
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

for i in 0 1 2 3 4 5 6; do
  ./build/tracer/Release/tracer_cli $i > $S/out/scene$i.ppm 2>/dev/null
  cmp -s $S/out/scene$i.ppm $S/golden/scene$i.ppm \
    && echo "scene $i IDENTICAL" || echo "scene $i *** DIFFERS ***"
done
```

Measured: **7 / 7 identical.**

Why identical and not merely close: `renderer::render` seeds its rng from `(pixel, sample, frame)`
and never from anything order-dependent, and `mesh::intersect` uses `t >= closest` (strict, so an
exact tie keeps the first-found triangle) — but a tie at exactly equal `t` between two *different*
primitives would still be resolved by visiting order, and would show up here. It does not, on
these scenes.

**If scene 5 alone differs, go back to step 3.** That is the flat-box signature, and it is worth
recognising on sight.

---

# Step 9 — GATE 3: the crossover, and the two thresholds

The tree is not free. Building the slab ray costs three divides before a single box is tested, and
there is a scene size below which the old loop simply wins. Measure it rather than guessing —
these are the numbers `linear_threshold` is set from.

Sweep the TLAS threshold (temporarily reading it from an env var; delete the knob afterwards —
**a `getenv` on the per-ray path costs more than the BVH saves**, measured at 3× on scene 5, which
is a confusing five minutes if you leave it in while timing):

| `TLAS_LINEAR` | scene 1 | scene 2 | scene 3 | scene 5 | scene 0 | scene 4 |
|---|---|---|---|---|---|---|
| 0 (always the tree) | 0.00371 | 0.00416 | 0.00376 | 0.00138 | 0.00587 | 0.00671 |
| 4 | 0.00396 | 0.00433 | 0.00395 | 0.00129 | 0.00615 | 0.00706 |
| **8** | **0.00316** | **0.00356** | **0.00346** | **0.00129** | 0.00631 | 0.00713 |
| 16 | 0.00332 | 0.00446 | 0.00415 | 0.00161 | 0.00811 | 0.00849 |
| 32 | 0.00400 | 0.00510 | 0.00395 | 0.00149 | 0.00658 | 0.00759 |

Scenes 1–3 have five prims and scene 5 has three, so only the first four columns respond; the last
two are noise on a laptop whose clock scales (±7% between repeats of an identical binary — treat
anything smaller than that as nothing). At 8, scenes 1, 2, 3 and 5 are back at their pre-BVH
timings.

**`scene::linear_threshold = 8`, `mesh::linear_threshold = 4.** The mesh number is lower because
a mesh below it is a mesh the TLAS is already culling as one prim; the case it exists for is
`make_triangle`'s one-triangle mesh, which is scene 5.

What survives after the thresholds are set:

| scene | before | after | |
|---|---|---|---|
| 1, 2, 3 | 0.00333 / 0.00371 / 0.00362 | 0.00350 / 0.00380 / 0.00373 | 0.95–0.97× |
| 5 | 0.00100 | 0.00112 | 0.90× |

That residue is `commit()` building two trees nothing traverses, plus one extra call frame in
`hit()`. On scene 5 it is 0.1 ms on a 100 ms render. **Do not chase it.** The alternative —
skipping the build when the prim count is below the threshold — adds a second state to `commit()`
that step 12 would have to gate, to save a tenth of a millisecond on the smallest scene in the
repo.

---

# Step 10 — GATE 4: the tree agrees with a linear scan, ray for ray

The goldens are a strong gate over the rays a camera happens to fire. This one covers rays a
camera never fires: grazing, backfacing, starting inside geometry, exactly axis-aligned.

`scene::linear_hit()` is the reference, in the same object, over the same prims and the same
materials — so `mat` pointers, `prim_id`s and `element_id`s are directly comparable, and any
mismatch is the tree's fault and nothing else's.

```cpp
// $S/gates.cpp
static void gate_agreement(int which, const char *name)
{
  scene s;
  camera_desc cam;
  load_scene(which, s, cam);
  s.commit();

  rng gen(0xBEEF + which);
  int mismatch = 0;
  double worst_t = 0;

  // rays aimed through the scene's own bounds, from a sphere around it
  const aabb box = s.bounds();
  const point3 c = box.centroid();
  const double rad = box.is_empty() ? 1 : 0.5 * box.extent().length() + 1;

  for (int i = 0; i < 200000; i++)
  {
    const point3 o = c + rad * unit_vector(vec3(gen.uniform(-1,1), gen.uniform(-1,1), gen.uniform(-1,1)));
    const point3 target = c + 0.6 * rad * vec3(gen.uniform(-1,1), gen.uniform(-1,1), gen.uniform(-1,1));
    const ray r(o, target - o);

    hit_info a, b;
    const bool ha = s.hit(r, interval(0.001, infinity), a);
    const bool hb = s.linear_hit(r, interval(0.001, infinity), b);

    if (ha != hb) { mismatch++; continue; }
    if (!ha) continue;

    if (std::fabs(a.t - b.t) > 1e-9 * std::max(1.0, std::fabs(b.t))
        || a.prim_id != b.prim_id || a.element_id != b.element_id
        || a.instance_id != b.instance_id || a.mat != b.mat
        || (a.normal - b.normal).length() > 1e-9)
    {
      mismatch++;
    }
    worst_t = std::max(worst_t, std::fabs(a.t - b.t));
  }
  ...
}
```

Measured, all seven scenes:

```
scene 0: BVH == linear scan on every ray                   PASS (200000 rays, worst |dt| = 0)
scene 1: ...                                               PASS (200000 rays, worst |dt| = 0)
   ... 2, 3, 4, 5 ...
scene 6: BVH == linear scan on every ray                   PASS (200000 rays, worst |dt| = 0)
```

**`worst |dt| = 0` is the point.** Not "within 1e-9" — bit-exact. The same primitive kernels run
on the same doubles; only the order and the value of `closest` differ, and `closest` only ever
rejects candidates that a later comparison would have rejected anyway. Anything other than zero
here means the traversal is culling with a tolerance it should not have.

**Do not build a second scene to compare against.** The obvious version of this test loads
`load_scene` twice and compares the two; it fails on every ray, because the two scenes have
different `material` objects and `a.mat != b.mat` always. That is a test bug that looks exactly
like a catastrophic renderer bug.

---

# Step 11 — GATE 5: pathological input does not overrun the stack

The traversal stack is a fixed 64 entries. That is safe only because `build_node` caps depth at
60, and the cap is only *never reached* because of the degenerate-geometry fallback. This gate
asserts all three at once.

```cpp
// $S/stress.cpp
static void run(const char *name, std::vector<aabb> boxes)
{
  bvh t;
  t.build(boxes);
  ... print n, depth, nodes, nodes/prim, max leaf, build ms, SAH ...
  if (t.depth() >= 64) printf("   *** DEPTH EXCEEDS THE 64-ENTRY TRAVERSAL STACK ***\n");
}
```

Measured:

```
sizeof(aabb) = 48, sizeof(bvh::node) = 64

100k identical boxes                 n= 100000  depth= 14  nodes=  16383 (0.16/prim)  18.9 ms
100k collinear degenerate boxes      n= 100000  depth= 14  nodes=  16383 (0.16/prim)  10.6 ms
100k flat boxes strung along +x      n= 100000  depth= 14  nodes=  16383 (0.16/prim)  10.9 ms
100k empty boxes                     n= 100000  depth= 14  nodes=  16383 (0.16/prim)  18.9 ms
100k uniform + one stadium-sized box n= 100001  depth= 21  nodes= 199787 (2.00/prim)  59.6 ms
1M uniform boxes                     n=1000000  depth= 25  nodes=1999645 (2.00/prim) 676.2 ms
0 boxes                              n=      0  depth=  0  nodes=      0              0.0 ms
1 box                                n=      1  depth=  1  nodes=      1              0.0 ms
```

**Without the zero-area fallback, rows 2 and 3 build to depth 98** and the traversal writes 34
entries past the end of a stack array. Not a hypothetical: those two rows are what a tessellated
ground plane and a flat wall look like to the builder. Every SAH candidate costs exactly zero, the
sweep takes the first one it sees, and the "split" peels off one bin's worth at a time.

The teapot-in-a-stadium row (100k tiny boxes plus one enormous one) is the case median split is
famously bad at; the SAH builder handles it at depth 21 with a **SAH cost of 9.0** against a
linear scan's 100 001.

---

# Step 12 — GATE 6: mutation, which is the actual roadmap item

Drive 400 instances through `scene::edit()` frame by frame, and after every `commit()` assert the
tree still agrees with the linear scan. This is the gate that would catch a refit fed boxes in the
wrong order — the one silent-wrong-answer failure mode in the design.

```
 frame  commit_ms   builds   refits   sah/built    rays_agree
     0     0.2490        1        0  14.402/14.402  4000/4000
     1     0.0285        0        1  14.245/14.402  4000/4000
     2     0.0295        0        1  14.202/14.402  4000/4000
   ...
    11     0.0312        0        1  17.582/14.402  4000/4000
    12     0.0297        0        1  18.192/14.402  4000/4000
    13     0.2970        1        1  13.129/13.129  4000/4000     <- 18.72 = 1.3 x 14.402, rebuild
    14     0.0292        0        1  13.219/13.129  4000/4000
   ...
    24     0.0306        0        1  17.053/13.129  4000/4000
```

Read it as: **12 refits at 0.030 ms, then one rebuild at 0.297 ms** — a mean commit of 0.051 ms
against 0.249 ms for always rebuilding, a **4.9×** saving, with the tree never more than 30% off a
fresh one. And `4000/4000` on every single frame, including the two frames either side of a
rebuild.

Three more things to assert in the same program, cheaply:

- **`insert` and `remove` force a rebuild.** `same_set` goes false, `_visible` and `_draw` are
  rebuilt together, and the permutation cannot be stale.
- **`set_visible(h, false)` forces a rebuild**, for the same reason, and the prim stops being hit.
- **A prim moved after `edit()` but before `commit()` is not visible to a render**, which is
  [[scene-graph]]'s gateway property, unchanged by this task.

---

# Step 13 — GATE 7: performance, and what the numbers should be

Interleave the two binaries; do not run all the "before" timings and then all the "after" ones.

```bash
for i in 0 1 2 3 4 5 6; do
  for r in 1 2 3 4 5; do
    ./build/tracer/Release/tracer_cli $i 2>&1 >/dev/null | grep -o '[0-9.e-]*ms/px'   # before
    $S/cli $i                          2>&1 >/dev/null | grep -o '[0-9.e-]*ms/px'   # after
  done
done
```

| scene | before | after | | what it is |
|---|---|---|---|---|
| 0 | 0.06879 | **0.00633** | **10.9×** | 484 spheres, TLAS only |
| 4 | 0.08034 | **0.00750** | **10.7×** | 206 instances, TLAS + 8-corner bounds |
| 6 | 0.14927 | **0.00392** | **38.1×** | one 1280-triangle mesh, TLAS + BLAS |
| 1, 2, 3 | 0.00333 / 0.00371 / 0.00362 | 0.00350 / 0.00380 / 0.00373 | 0.95× | five prims, below threshold |
| 5 | 0.00100 | 0.00112 | 0.90× | three prims, below threshold |

Two sanity checks on the shape of those numbers:

- **Scene 0's TLAS has a SAH cost of 2.000 under every builder tried**, including median split.
  That is not a bug in the metric: the r = 1000 ground sphere's box has ~10⁷ times the surface
  area of everything else, so a ray that enters the root box will essentially always be tested
  against the ground and nothing more. The 10.9× is entirely "stop testing 484 spheres", and the
  builder's *quality* is nearly irrelevant there. It is very relevant on scene 4 (SAH 3.68) and on
  meshes (27.7 for the icosphere against a linear scan's 1280).
- **Sixteen times the triangles costs 24% more time.** Scene 6 with the 1280-triangle icosphere is
  0.00380 ms/px; swapped for a 20 480-triangle one it is 0.00473. That log curve is the deliverable.

And the commit-side cost, which is new and which nothing measured before:

| what | build | as a share of the render it precedes |
|---|---|---|
| scene 0 TLAS, 484 prims | 0.237 ms | 0.04% of 570 ms |
| scene 4 TLAS, 206 prims | 0.169 ms | 0.03% |
| scene 6 BLAS, 1280 tris | 0.552 ms | 0.16% of 353 ms — and 8% of a single-sample viewer pass |
| a 20 480-triangle BLAS | 10.6 ms | 2.5% of that scene's 50-spp render |

The 8% figure is the one to watch. It is the reason step 7 exists, and the reason Appendix B's
parallel build is the next lever rather than a wide tree.

---

# Step 14 — Commit

```bash
cd ~/git/weekend-raytracer
git add tracer/aabb.h tracer/bvh.h tracer/hittable.h tracer/sphere.h \
        tracer/hittable_list.h tracer/instance.h tracer/mesh.h tracer/scene.h
git add docs/plans/bvh.md docs/Roadmap.md
git commit
```

Message:

```
bvh with rebuild-on-mutation

Two-level binned-SAH BVH: a BLAS per mesh over its triangles, a TLAS in
scene over the draw list. One implementation serves both - it takes an
array of boxes, hands back a permutation, and calls a leaf callback the
owner supplies.

commit() refits when the primitive set is unchanged and rebuilds when the
refitted tree's SAH cost exceeds 1.3x what it cost when built. Measured on
400 drifting instances: 12 refits at 0.030 ms per rebuild at 0.297 ms.

scene 0 10.9x, scene 4 10.7x, scene 6 38.1x. All seven goldens byte-
identical. Scenes below the linear threshold (8 prims, 4 triangles) bypass
the tree and are within 10% of where they were.

Two guards found by measurement, both with negative controls in the gates:
the slab test's final compare must be strictly `<` or every axis-aligned
triangle is invisible (31k of 90k pixels wrong on scene 5), and zero-area
geometry must fall back to a median split or the tree reaches depth 98
against a 64-entry traversal stack.
```

Then tick the roadmap:

```diff
-- [ ] bvh with rebuild-on-mutation
+- [x] bvh with rebuild-on-mutation
```

---

## Definition of done

- [ ] `tracer/aabb.h` and `tracer/bvh.h` exist; neither includes TBB, and `bvh.h` includes nothing
      from `hydra/`.
- [ ] `hittable::bounds()` is pure virtual and all five subclasses implement it.
- [ ] `mesh` owns a BLAS; `geom` is in BVH order and `tri_index` maps back to authored triangles.
- [ ] `scene` owns a TLAS; `_visible` is insertion order, `_draw` is BVH order, and
      `linear_hit()` is the old loop unchanged.
- [ ] `commit()` refits when the primitive set is unchanged and rebuilds when
      `bvh::degraded()`.
- [ ] GATE 2: all seven goldens byte-identical.
- [ ] GATE 3: scenes 1, 2, 3 and 5 within ~10% of their pre-task timings.
- [ ] GATE 4: 7 × 200 000 rays agree with `linear_hit` on `t`, `prim_id`, `instance_id`,
      `element_id`, `mat` and `normal`, with worst |Δt| = 0.
- [ ] GATE 5: no pathological input exceeds depth 60; the coplanar cases build to depth 14.
- [ ] GATE 6: 400 instances mutated for 24 frames, agreeing with `linear_hit` after every commit,
      with roughly one rebuild per dozen refits.
- [ ] GATE 7: scene 0 ≥ 8×, scene 4 ≥ 8×, scene 6 ≥ 30×.
- [ ] `viewer` still builds and runs; dragging the camera is visibly interactive on scene 6 for
      the first time.
- [ ] No `getenv`, no `#ifdef`, and no test-only accessor left in the shipped headers.
      `linear_hit()` is the one deliberate exception, and it is on the render path.

---

## Design notes — decisions made, recorded so they aren't re-litigated

**Why binned SAH and not something cheaper.** Median split is 100 lines and builds 2.5× faster.
It is also 1.80× slower to trace on scene 0, 1.36× on scene 4, and 1.27× on a mesh — measured, on
this repo's own scenes, in the table under "Tree quality". Middle split is cheaper still and lands
between the two, competitive on the icosphere (uniform density, where it approximates the SAH) and
1.34× slow on scene 0 (where it does not). The extra ~150 lines buy the difference on every frame,
forever, and the build cost they add is 0.2 ms.

**Why not LBVH/Morton codes, given `rebuild-on-mutation`.** A Morton-code build is 5–20× faster
and 15–40% worse to trace. That trade is right when the build is per-frame and unavoidable — a GPU
renderer rebuilding a deforming scene every frame. Here the build is per *edit*, refit absorbs the
common edit at 52× less than a rebuild, and the trace cost is paid by every ray of every sample of
every frame. Wrong end of the trade.

**Why one class for both levels rather than a `tlas` and a `blas`.** They differ only in what a
leaf contains, and that is exactly what the callback abstracts. The concrete payoff is that
everything in "Pre-verified facts" — the leaf-size sweep, the degeneracy fallback, the depth cap,
the stack-array fix — was measured once and applies to both.

**Why the leaf callback is a template parameter and not `std::function`.** It is on the per-ray
path. `std::function` would be an indirect call per leaf visit against a fully inlined loop.
`bvh.h` is header-only anyway, so this costs nothing in build structure.

**Why `sah_cost()` is a public method and not an internal detail.** It is the only honest way to
compare two builders without a stopwatch, it is the rebuild trigger, and it makes the leaf-size and
bin-count tables reproducible by anyone who doubts them. `refit()` returning it is what makes the
trigger free.

**Why the traversal stack is fixed at 64 and not a `std::vector`.** A heap allocation per ray
would dominate. The cap is safe by construction (`max_depth = 60`), and step 11 asserts that
degenerate input cannot reach it.

**Why `bvh::node` is 64 bytes and not 32.** `aabb` is six doubles. Float bounds with conservative
outward rounding would halve the node and is worth real cache money — measured 2.00 nodes per
primitive, so 128 B/prim resident, 128 MB at a million triangles. It is deferred because it is the
one change in this area that can make the tree *wrong* if the rounding is not conservative on both
ends, and it deserves its own gate rather than riding along with a change whose whole claim is
"the images do not move". The project's `float`/`double` compile option is already a wishlist item;
this belongs with it.

**Why `linear_hit()` stays in `scene.h` rather than moving to a test.** `_draw` is private, and an
accessor for it would be a worse thing to leave behind than twenty lines of reference
implementation. It also turns out not to be test-only: `hit()` calls it below the threshold.

**Why the rebuild trigger is a cost ratio and not "did the topology change".** The caller does not
reliably know. Hydra sets `DirtyPoints` without `DirtyTopology` for a deforming mesh, but it also
sets both for a re-tessellation that happens to preserve the triangle count, and a
scene-index plugin can change what arrives without changing which bits are set. A ratio measured
on the actual boxes is not a guess. It also degrades gracefully: a wrong guess costs a slower
frame, never a wrong pixel.

**Why `scene` keeps two vectors instead of comparing against a hash or a version stamp.**
`_visible` is `2 * sizeof(void*)` per prim — 8 KB at 484 prims. A version stamp would need every
mutation path to remember to bump it, which is exactly the kind of invariant that survives review
and dies in a refactor. Comparing the list is O(n) against a build that is O(n log n).

**Why `max_leaf = 16` exists alongside `leaf_size = 1`.** They answer different questions.
`leaf_size` is "stop before asking"; `max_leaf` is "the SAH said make a leaf, but a leaf this size
is a linear scan, so split anyway". They only ever both matter on degenerate input, which is
precisely where a linear scan hides.

---

## Appendix A — the delegate side, for 0.3.0

Recorded so the shape does not have to be re-derived. Nothing here is written now.

```cpp
// The gateway from hydra-spec §6, unchanged by this task. The BVH rebuild
// happens inside commit(), on the render thread, AFTER StopRender() has
// returned - so no lock, no atomic, and nothing new to reason about.
weekend::scene *HdWeekendRenderParam::AcquireSceneForEdit()
{
    _renderThread->StopRender();
    (*_sceneVersion)++;
    return _scene;
}

void HdWeekendMesh::Sync(HdSceneDelegate *sd, HdRenderParam *rp,
                         HdDirtyBits *dirtyBits, TfToken const &reprToken)
{
    ...
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtVec3fArray pts = sd->Get(id, HdTokens->points).Get<VtVec3fArray>();
        _mesh->verts.assign(pts.begin(), pts.end());
        // Nothing else. The next commit() sees an unchanged triangle count,
        // refits, and only rebuilds if the deformation actually degraded the
        // tree. This is the case step 7 exists for.
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _instance->set_transform(to_mat4(sd->GetTransform(id)));
        // The BLAS is untouched; only instance::bounds() moves, so the next
        // commit() is a TLAS refit over the prim count. 0.030 ms at 400 prims.
    }
    ...
}
```

Two things this task deliberately made easy for the delegate, and one it did not:

- **`DirtyTransform` never touches a triangle.** It moves one matrix; the TLAS refits over prims.
  That is the interaction budget for dragging a gizmo in usdview.
- **`DirtyPoints` never touches the index buffer or the tree topology.** `verts` is assigned, and
  `commit()` walks `tri_index` to rebuild `geom` in place.
- **§14 instancing will want the shared-prototype fix.** N instances of one prototype currently
  commit that prototype N times — 9.7 ms per commit at 20 instances of a 20 480-triangle mesh. A
  `uint64_t` commit epoch on `hittable`, set by `scene::commit()` and checked by `mesh::commit()`,
  is the whole fix. Do it *with* instancing, where the N gets large enough to matter, not before.

---

## Appendix B — what comes after, in the order the numbers say

Each of these is a self-contained follow-up. The measured build here is what they are measured
against.

**1. A parallel build (highest value, lowest risk).** 10.6 ms for a 20 480-triangle BLAS is 8% of
a single-sample viewer pass, and 676 ns/prim means a million triangles is 0.7 s. The build is a
tree of independent subproblems and the top few levels have all the work. The mechanism should be
the project's existing one: inject a scheduler, exactly as `renderer` takes a `tile_scheduler`, so
that `bvh.h` still compiles with no TBB anywhere ([[interruptible-render-loop]] step 6b gates
this). Spawn a task per subtree only above ~4 000 primitives; below that the task overhead
dominates. Expect 4–6× on twelve threads — the partition is memory-bound.

**2. Bin-derived child bounds.** `build_node` computes each node's bounds with a full pass over its
range, then bins. The bins already carry per-bin boxes, so both children's bounds fall out of the
prefix/suffix sweep for free if the bins also accumulate centroid bounds. Removes one of the three
linear passes per node — worth roughly a third of the build, and it is a local change.

**3. Float or quantised node bounds.** 64 → 32 bytes per node, so half the cache traffic on the
per-ray path. Needs conservative outward rounding (`nextafter` toward ±inf on the right ends) or it
silently drops hits, so it needs its own version of step 10's gate. See design notes.

**4. A wide (BVH4/BVH8) tree.** Collapse the binary tree — the one built here — into 4- or 8-way
nodes and test all children's slabs at once. Typically 1.3–2× traversal, and about 100 lines,
because it is a pass over a finished tree rather than a new builder. It wants (3) first: the point
is to test several boxes in one cache line, and 64-byte nodes make that impossible.

**5. Spatial splits (SBVH), treelet restructuring, reinsertion.** 10–30% on the scenes they help,
2–10× the build cost, and none of them help a scene made of spheres and icospheres. Not before
`profiling tools` exists and says the traversal is the bottleneck.

The ordering is deliberate: (1) fixes the number that is currently 8% of an interactive frame,
(2) and (3) are cheap and compounding, (4) needs (3), and (5) needs evidence this repo cannot
produce yet.

---

## Next up

`0.3.0 - hydra delegate` → `hydra wrapper`. The tracer side of the roadmap is now complete: a
scene graph with a mutation gateway, transforms, meshes, AOVs, an interruptible progressive render
loop, and — as of this task — an acceleration structure that rebuilds on mutation, which is the
last thing [[hydra-spec]] §6 and §17.7 require of the *renderer* rather than of the plugin.
Everything that remains is `HdRenderDelegate` subclasses forwarding into the API that already
exists; Appendix A of this document, of [[triangle-mesh]], of [[scene-graph]] and of
[[transform-support]] together sketch most of it. [[hdtiny-stub-delegate]] already proved the
plugin loads.
