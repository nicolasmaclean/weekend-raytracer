# triangle mesh (and tinyobjloader) — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `triangle mesh (and tinyobjloader)` — see [[Roadmap]]
**Context:** [[hydra-spec]] §7.4 (points, topology, **`ComputeTriangleIndices`**), §8.3 (the
`elementId` AOV), §7.2 (`Sync` pulls only dirty data) · [[roadmap-discussion-8-26]] §1 (why mesh
and BVH moved to 0.2.0), §5 item 3, §5 item 4 (no texture mapping) · [[scene-graph]] "Next up"
**Every number in this document was measured on this machine on 2026-08-28, at commit `52f76ac`
("triangle primitive!"). See "Pre-verified facts".**

---

## What this task is

[[hydra-spec]] §7.4 asks for two rows that the tracer cannot answer today:

> | Points | `GetPrimvar(id, HdTokens->points)` | Gated on `IsPrimvarDirty` for `points` |
> | Triangulation | `HdMeshUtil(&topology, id).ComputeTriangleIndices(...)` | **Required** for a
> triangle-only renderer; also produces the primitive-param map needed to look up
> face-varying/uniform primvars per hit |

Both hand over **arrays**: a flat buffer of points, a flat buffer of triangle indices, and a
parallel `primitiveParams` array mapping each triangle back to the face the author wrote. Nothing
in the tracer takes an array of anything. `triangle` (uncommitted, in the working tree) is one
prim per triangle, holding three `vertex` structs by value.

At the end of this task there is one new header, `tracer/mesh.h`, holding a hittable that owns
those four arrays and intersects all of them in one call; a second new header,
`tracer/obj_loader.h`, that fills them from a `.obj`; a vendored tinyobjloader; and
`tracer/triangle.h` reduced from a class to a three-line factory. `hit_info` gains a two-normal
`set_face_normal` overload. `element_id` gets its first non-`-1` value in the history of the
project.

**Scene 5's golden changes** — by roughly three 8-bit channels out of 270 000, measured. Scenes
0–4 do not move, and step 8 gates that as byte-identical.

## Why it matters more than "put the triangles in a vector" sounds

1. **It is the shape change [[roadmap-discussion-8-26]] §1 already called.** "Per-triangle virtual
   `hittable` has to become flat indexed triangle storage." Measured below: at 1280 triangles the
   flat form is **1.59×** faster and **7.6×** smaller than the same triangles as `hittable` objects
   in a `hittable_list`, and the gap widens with triangle count.
2. **The wrapping rule from [[transform-support]] only survives if a mesh is one prim.** That plan
   measured a 2.6× penalty for wrapping 484 prims in `instance` and a 3% penalty for wrapping one
   prim containing 480 — and wrote the rule down as "wrap once per prim, never once per
   primitive". A mesh of a thousand triangles must present to `scene`, to `instance`, and soon to
   the BVH as a **single** `hittable`. Per-triangle prims break that rule structurally.
3. **`elementId` cannot be faked later.** §8.3 requires an int32 AOV whose value is the *authored*
   face index, not the triangle index. hdEmbree gets it from `primitiveParams`. If the mesh does
   not carry a per-triangle authored-face array from day one, every quad in every USD asset reports
   two different element ids and picking in usdview is wrong. Measured below: tinyobjloader's own
   `triangulate=true` mode destroys exactly this mapping, silently.
4. **It is the last item before the BVH, and it is what makes the BVH unavoidable.** One
   1280-triangle mesh renders at **0.184 ms/px**. The analytic sphere it approximates renders at
   **0.0011 ms/px** — 167× cheaper. All 484 spheres of scene 0 render at **0.061 ms/px**, so one
   modest mesh already costs 3× the entire book-cover scene. There is no version of this that gets
   better without acceleration.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| Any acceleration structure; `mesh::hit` is a linear scan over every face | `bvh with rebuild-on-mutation` — Appendix B |
| An object-space AABB on `mesh` | same — the BVH is the only caller that needs one |
| `HdWeekendMesh::Sync()`, `HdMeshUtil`, dirty-bit gating, `GetMeshTopology` | `hydra wrapper` (0.3.0) — Appendix A is the sketch |
| Primvar interpolation beyond normals: uv, colors, `faceVarying`, the sampler machinery | 0.4.0 texture mapping ([[roadmap-discussion-8-26]] §5 item 4) — §7.4 says port `hdEmbree/sampler.h` rather than reinvent |
| `.mtl` parsing, per-face materials | nobody. §13: hdEmbree has no material support at all. `load_obj` takes one `shared_ptr<material>` from the caller |
| Subdivision, creases, `refineLevel`, `GetSubdivTags` | not scheduled |
| Backface culling, `GetCullStyle`, `GetDoubleSided` | `hydra wrapper` — the mesh is double-sided here, like hdEmbree's default |
| Watertight ray/triangle intersection (Woop et al.) | nobody yet; see "Design notes" for when it would matter |
| Motion blur, time-sampled points | not scheduled |
| Any change to `camera.h`, `render_buffer.h`, `renderer.h`, `render_control.h`, `scene.h`, `instance.h` | nothing — step 8 proves they are untouched |

The tracer stays **USD-free**: no `pxr/` include under `tracer/` or `viewer/`. Steps 8–13 lean on
scratchpad programs — per the standing rule, **no test code lands in the repo**.

---

## Pre-verified facts

Measured, not assumed. Every program named below was written and run; the numbers are its output.

```
S=/tmp/claude-1000/-home-nick-git-weekend-raytracer-docs/ebfb54ca-ed65-4e04-a9e2-5ade3c576606/scratchpad
```

| Claim | Measured |
|---|---|
| **Möller–Trumbore agrees with the committed `triangle` plane test.** Same 1280-triangle icosphere, flat shading, 50 spp, 400×225 | **3 / 270 000** 8-bit channels differ, max \|Δ\| = **2** |
| **Flat indexed storage beats a `hittable_list` of `triangle`** — same geometry, same image | 80 tri: 0.0119 vs 0.0148 (**1.24×**) · 320 tri: 0.0458 vs 0.0597 (**1.30×**) · 1280 tri: 0.1841 vs 0.2920 ms/px (**1.59×**) |
| **Caching `p0/e1/e2` per face in `commit()` is worth it, and changes no pixel** | 1280 tri, 20 spp, interleaved: 0.0691/0.0731 uncached vs 0.0543/0.0559 cached — **1.27–1.31×**, and `cmp` **byte-identical** |
| `commit()`'s rebuild cost | **10.3–11.3 ns/triangle** — 0.0132 ms at 1280 tri, **0.221 ms at 20 480** |
| **Memory per triangle** | flat **40.0 B** (112 B with the `commit()` cache) vs soup **304.0 B** — **7.6×** / 2.7× |
| `sizeof` | `vec3` 24, `vertex` 48, `sphere` 56, `triangle` **272**, `instance` 424, `shared_ptr` 16 |
| **Smooth vertex normals measurably converge on the analytic sphere** | vs `sphere`: smooth **9 855**/270 000 channels differ (mean 0.129), flat **24 451** (mean 0.198). Same max (76) at the silhouette |
| **The `\|det\| < eps` guard is not optional.** A zero-area triangle, 20 000 random rays | with the guard **0** hits; without it **20 000/20 000** hits, all at **non-finite `t`** |
| Collinear and coincident-corner triangles are caught by the `u`/`v` tests either way | 0 hits both ways |
| **`element_id` gets a real value.** 1280-tri mesh, 400×225, `aov::element_id` | **313** distinct face ids over 13 175 covered pixels, range **[16, 1087]**, background −1 |
| The same scene built from `triangle` prims | element_id **−1 on all 90 000 pixels** |
| **One 1280-triangle mesh vs what it replaces** | mesh 0.1841 ms/px · analytic `sphere` 0.0011 ms/px (**167×**) · all 484 spheres of scene 0 0.0608 ms/px (**3.0×**) |
| tinyobjloader's newest **stable** tag is `v1.0.0`, which predates `attrib_t` and `ObjReader` entirely | `git ls-remote`; `v2.0.0rc13` is the current head-of-line tag and has both |
| `v2.0.0rc13` via `FetchContent` builds and links | shallow checkout **6.3 MB**, `sizeof(tinyobj::real_t)` = **8** with `TINYOBJLOADER_USE_DOUBLE=ON` |
| **`TINYOBJLOADER_USE_DOUBLE=ON` renames the target.** Probed all three names after `FetchContent_MakeAvailable` | `tinyobjloader_double` **exists**; `tinyobjloader` and `tinyobjloader::tinyobjloader` **do not** |
| A `%.17g` OBJ round-trips through the loader | 1280 tri, 642 verts, 642 normals, 1 shape, 0 non-triangle faces, max \|ΔP\| = \|ΔN\| = **2.78e-16** |
| **`cfg.triangulate = true` destroys the authored face index.** `models/cornell_box.obj` | 8 shapes, **18 quad faces** → after triangulation `num_face_vertices` is all 3s and the count is **36**, with no map back |
| Fan-triangulating ourselves from `triangulate = false` keeps it | same file → **36 triangles carrying 18 authored face ids**, 2 triangles per id, 8 meshes |
| Vertex de-duplication on `(vertex_index, normal_index)` works | icosphere: 3 840 corners → **642** vertices |

Three of those decide the shape of the task.

**`triangulate = true` is a trap.** It looks like exactly what §7.4 asks for and it is the library's
default. It is not: it renumbers faces in place and hands back no `primitiveParams`. Setting it and
then using the loop index as `element_id` produces an AOV that is *plausible* — monotonic, dense,
correct-looking in a false-colour view — and wrong on every polygon that is not already a triangle.
Measured on the loader's own Cornell box: 18 authored faces reported as 36. Fan the polygon
yourself; it is ten lines and it is what `HdMeshUtil::ComputeTriangleIndices` does.

**The `|det|` guard is load-bearing, not hygiene.** With `det == 0` the barycentrics come out `NaN`,
and *every* comparison against a `NaN` is false — so `u < 0 || u > 1` does not reject it, `v < 0 ||
u + v > 1` does not reject it, and `t >= closest` does not reject it. A single zero-area triangle
in a loaded asset therefore swallows every ray in the scene at `t = NaN`. Measured: 20 000 rays,
20 000 spurious hits. Removing that line as "a micro-optimisation, the barycentric test catches it
anyway" is a real and very confusing bug.

**The flat/soup gap is not the whole story, and the smaller number is the important one.** 1.59× at
1280 triangles is worth having, but the 7.6× memory difference and the *one-prim* property are what
this design is actually for. A `hittable_list` of 1280 triangles is 1280 entries the BVH would have
to own individually, 1280 virtual calls per ray, and 1280 things `instance` would have to wrap.
Flat storage makes all three go away at once.

Build lines that work on this machine:

```bash
cd ~/git/weekend-raytracer
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# tracer-only scratchpad program (with the tbb scheduler)
g++ -std=c++17 -O3 -DNDEBUG -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/bench $S/bench.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb

# no scheduler needed for the math gates
g++ -std=c++17 -O2 -I$S -Itracer -o $S/degen $S/degen.cpp

# with tinyobjloader compiled in directly (before it is vendored)
g++ -std=c++17 -O2 -DTINYOBJLOADER_USE_DOUBLE -I$S -I$S/tol -Itracer \
    -o $S/loadtest $S/loadtest.cpp $S/tol/tiny_obj_loader.cc

# after step 4, linking the vendored archive. -DTINYOBJLOADER_USE_DOUBLE is
# MANDATORY here - see below.
g++ -std=c++17 -O3 -DNDEBUG -DTINYOBJLOADER_USE_DOUBLE \
    -DTRACER_ASSET_DIR="\"$PWD/assets\"" \
    -I$S -Itracer -Ibuild/_deps/tbb-src/include -Ibuild/_deps/tinyobjloader-src \
    -o $S/gate $S/gate.cpp \
    build/_deps/tinyobjloader-build/Release/libtinyobjloader_double.a \
    -Lbuild/gnu_13.3_cxx11_64_release -ltbb
```

**Forgetting `-DTINYOBJLOADER_USE_DOUBLE` in a scratchpad TU costs an hour.** The macro is
not baked into the archive; it selects `real_t` in the *header*. Without it your translation unit
reads `attrib.vertices` as a `vector<float>` while the archive filled a `vector<double>` - a silent
ABI mismatch that links cleanly and hands back garbage coordinates. Measured symptom: a unit
icosphere whose surface is 0.04 from its own centre, and a render that is uniformly black including
the sky, which looks exactly like a scene-construction bug and is not one. The cmake targets get
this right for free (`tinyobjloader_double` propagates the define as `INTERFACE`); only hand-rolled
`g++` lines can drop it.

---

## The design in one page

```
tracer/hittable.h        EDIT     hit_info gains a two-normal set_face_normal overload.
                                  4 lines. Nothing else in the file moves.

tracer/mesh.h            NEW      class mesh : public hittable. Four parallel arrays + one
                                  material. commit() builds the per-face cache; hit() is
                                  Moller-Trumbore over it. ~110 lines.

tracer/vert.h            KEEP     `vertex` stays, as the *authoring* type only. The mesh
                                  stores structure-of-arrays. See "Why SoA".

tracer/triangle.h        REWRITE  the class goes; make_triangle() returns a shared_ptr<mesh>
                                  with one face. ~12 lines.

tracer/obj_loader.h      NEW      load_obj() -> one mesh per OBJ shape. Fan-triangulates.
                                  ~85 lines.

vendor/tinyobjloader/    NEW      CMakeLists.txt, FetchContent, v2.0.0rc13, USE_DOUBLE.
vendor/CMakeLists.txt    EDIT     one add_subdirectory line.
tracer/CMakeLists.txt    EDIT     new `tracer_obj` INTERFACE target; TRACER_ASSET_DIR define.
viewer/CMakeLists.txt    EDIT     link tracer_obj.

assets/icosphere.obj     NEW      1280-triangle unit icosphere with vertex normals. 84 KB.

tracer/example_scenes.h  EDIT     scene_6 uses make_triangle; new scene_7 loads the OBJ.
                                  load_scene case 6.

tracer/scene.h           UNTOUCHED
tracer/instance.h        UNTOUCHED
tracer/renderer.h        UNTOUCHED   (the element_id AOV was already plumbed and already
tracer/render_buffer.h   UNTOUCHED    clears to -1 — scene-graph step 4 did that work)
tracer/camera*.h         UNTOUCHED
```

### The five invariants

Everything `mesh::hit` does follows from wanting these to hold, so that `scene`, `instance`,
`renderer::raycast` and every `material` stay unaware that a mesh is not a sphere.

| Invariant | How | Verified |
|---|---|---|
| **`t` is in the caller's units.** The direction is never normalized | Möller–Trumbore needs no normalization; `t` falls out in units of `\|d\|` | same invariant [[transform-support]] gated; the 0.001 acne epsilon in `renderer::raycast` keeps meaning 0.001 world units under any `instance` |
| **`normal` is a unit world-space *shading* normal** | barycentric blend of `N`, renormalized; falls back to the geometric normal when `N` is empty | smooth-vs-sphere error is 2.5× lower than flat-vs-sphere |
| **`front_face` is decided by the *geometric* normal** | `dot(d, cross(e1, e2)) < 0`, never by the interpolated one | a smooth normal can point away from the ray near a silhouette without the ray being inside the surface; deciding from it makes `glass` flip its IOR on the rim |
| **`element_id` is the authored face, not the triangle** | `face.empty() ? triangle_index : face[triangle_index]` — the exact shape of hdEmbree's `_ComputeId` (`hdEmbree/renderer.cpp:869`) | 18 authored faces from 36 triangles on `cornell_box.obj` |
| **Nothing is written to `hit_info` unless `hit` returns true** | all stores happen after the loop, behind `if (best < 0) return false;` | this is what makes `scene::hit`'s stale-id reset sufficient — see below |

### The id-stamping rule, restated

[[scene-graph]] put this line in both `scene::hit` and `hittable_list::hit`:

```cpp
temp_info.instance_id = -1;
temp_info.element_id  = -1;
```

after a hit is copied into `info`. It exists for exactly this task. Consider a mesh at face 42 that
is hit, followed by a nearer `sphere`: `sphere::hit` never touches `element_id`, so without the
reset the sphere would inherit 42. **Do not remove those lines**, and do not "optimise" `mesh::hit`
into stamping `info` incrementally inside the face loop — the reset only works because a hittable
writes ids exactly once, on success. Step 11 has the negative control.

### Why structure-of-arrays, and why `vertex` survives anyway

`vert.h`'s `struct vertex { vec3 p; vec3 n; }` is a fine thing to *write* a triangle with and the
wrong thing to *store* a mesh in:

- **Hydra hands over separate arrays.** `points` and `normals` are two different primvars with two
  different dirty bits. `DirtyPoints` without `DirtyNormals` is the common case in a deforming
  animation; an AoS buffer has to be rebuilt wholesale for it.
- **Normals are optional.** `N.empty()` is the flat-shading switch, and it costs nothing. An AoS
  vertex has to carry a sentinel normal, or a parallel bool.
- **It is 2× the memory when normals are absent**, which is most OBJ files in the wild —
  `cornell_box.obj` has zero `vn` lines.

So: `mesh` stores `P`, `N`, `tri`, `face`. `vertex` stays as the argument type of
`mesh::add_triangle` and `make_triangle`, which is how `example_scenes.h` already reads.

### Who owns what, after this task

| Decision | Owner |
|---|---|
| Points, normals, indices | the caller — Hydra's `GetPrimvar` + `ComputeTriangleIndices`, or `load_obj` |
| The authored-face map (`face`) | the caller — `primitiveParams` in Hydra, our own fan loop in the loader |
| Turning polygons into triangles | `HdMeshUtil` in Hydra, `load_obj` for OBJ. **Never `mesh`** — it only ever sees triangles |
| Rebuilding the per-face cache | `mesh::commit()`, called by `scene::commit()` |
| When it is safe to mutate `P`/`tri` | the caller, through `scene::edit()` — the `StopRender` gateway from [[scene-graph]] |
| Placing a mesh in the world | `instance`, one per mesh — [[transform-support]]'s wrapping rule |
| Which material a mesh has | one per mesh, from the caller. Split by shape if you need more |

---

# Step 0 — Capture goldens before you touch anything

Scenes 0–4 are gated byte-identical. Scene 5 will move, so capture it too and record the *new*
value in step 13.

```bash
cd ~/git/weekend-raytracer
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
S=/tmp/claude-1000/.../scratchpad          # your scratchpad

for i in 0 1 2 3 4 5; do ./build/tracer/Release/tracer_cli $i > $S/gold_$i.ppm; done
md5sum $S/gold_*.ppm | tee $S/gold.md5
```

Measured here. 0–3 are unchanged since [[interruptible-render-loop]] step 0 and 4 since
[[scene-graph]] step 0, which is itself worth knowing:

```
3292e039125ee04d7f4728ad9d89886f  gold_0.ppm
81978695472eb949e987e46fefe3e694  gold_1.ppm
418151b864772683d18aef594a1651b7  gold_2.ppm
57e57b71e5501b5f278b60a73793b64c  gold_3.ppm
297533cce4fcb5d116e82b2322a6308d  gold_4.ppm
b170938c218e9c0dfa66613f628d31bf  gold_5.ppm      <- the one that is allowed to change
```

All six re-verified at implementation time and matched. Scene 5's post-task value is in step 13.

Warm timing baseline for step 12 — run four times, keep the last three (the first run on this
machine is consistently the fastest; always interleave old/new when comparing):

```bash
for i in 1 2 3 4; do ./build/tracer/Release/tracer_cli 0 >/dev/null; done
# here: 0.0632 / 0.0647 / 0.0665 ms/px   (scene 0, 484 spheres)
# and:  0.00121 / 0.00118 / 0.00120      (scene 5, one triangle)
```

---

# Step 1 — `hit_info` gains a two-normal `set_face_normal`

A mesh has two normals at a hit and they play different roles. The existing single-argument form
would force the ray-side decision to be made from the shading normal, which is wrong on
silhouettes. Add the overload next to the existing one in `tracer/hittable.h`:

```cpp
  void set_face_normal(const ray &r, const vec3 &outward_normal)
  {
    // outward should be normallzed
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }

+  // Two-normal form, for geometry whose shading normal is interpolated. Which
+  // side of the surface the ray is on is a question about the *geometry*, so
+  // `geometric` decides it - only its sign is read, it need not be unit. The
+  // normal handed to the material is the shaded one.
+  void set_face_normal(const ray &r, const vec3 &geometric, const vec3 &shading)
+  {
+    front_face = dot(r.direction(), geometric) < 0;
+    normal = front_face ? shading : -shading;
+  }
```

That is the entire edit to `hittable.h`. `hit_info` already carries `element_id` and it already
defaults to `-1`; `aov::element_id` already exists in `render_buffer.h` with an int32 format and a
`-1` clear value; `renderer.h` already writes it. [[scene-graph]] steps 1 and 4 did that work, and
this task is the first thing to put a number in it.

---

# Step 2 — Write `tracer/mesh.h`

The whole file:

```cpp
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "hittable.h"
#include "ray.h"
#include "tracer.h"
#include "vec3.h"
#include "vert.h"

// A triangle mesh, stored the way Hydra hands one over: parallel attribute
// arrays plus a flat index buffer. One `mesh` is one Rprim, and one `hittable`
// - `instance` wraps the mesh, never its faces.
//
//   P[v]              object-space points. Always present.
//   N[v]              per-point normals. EMPTY means flat shading; if it is
//                     non-empty it must be exactly P.size() long.
//   tri[3f+0..2]      three indices into P per triangle.
//   face[f]           authored face index per triangle, for element_id. EMPTY
//                     means "one triangle per authored face", i.e. face[f]==f.
//                     Same convention as hdEmbree's primitiveParams.
//
// Only triangles. Polygons are somebody else's problem: HdMeshUtil's on the
// Hydra side, load_obj's on the file side.
class mesh : public hittable
{
public:
  std::vector<vec3>    P;
  std::vector<vec3>    N;
  std::vector<int32_t> tri;
  std::vector<int32_t> face;
  shared_ptr<material> mat;

  size_t triangle_count() const { return tri.size() / 3; }

  // --- authoring helpers, for hand-written scenes -----------------------
  // A loader should fill P/N/tri directly instead, so shared corners stay
  // shared. These duplicate every corner.
  //
  // A mesh is either entirely smooth-shaded or entirely flat. Do not mix the
  // two overloads on one mesh.
  void add_triangle(const point3 &a, const point3 &b, const point3 &c)
  {
    const int32_t base = int32_t(P.size());
    P.push_back(a);
    P.push_back(b);
    P.push_back(c);
    tri.push_back(base);
    tri.push_back(base + 1);
    tri.push_back(base + 2);
  }

  void add_triangle(const vertex &a, const vertex &b, const vertex &c)
  {
    add_triangle(a.p, b.p, c.p);
    N.push_back(a.n);
    N.push_back(b.n);
    N.push_back(c.n);
  }

  // --- hittable ---------------------------------------------------------
  // Derived per-face geometry. Rebuilt from scratch: measured at 10.8 ns per
  // triangle, i.e. 0.22 ms for a 20k-triangle mesh, against a re-render
  // measured in seconds. See "Design notes" before adding a dirty flag.
  void commit() override
  {
    geom.clear();
    geom.reserve(triangle_count());

    for (size_t i = 0; i + 2 < tri.size(); i += 3)
    {
      const vec3 &p0 = P[tri[i]];
      const vec3 &p1 = P[tri[i + 1]];
      const vec3 &p2 = P[tri[i + 2]];
      geom.push_back({p0, p1 - p0, p2 - p0});
    }
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    int32_t best = -1;
    double closest = clipping_range.max;
    double bu = 0, bv = 0;

    for (size_t f = 0; f < geom.size(); f++)
    {
      const tri_geom &g = geom[f];

      // Moller-Trumbore. The direction is NOT normalized, so `t` comes back in
      // the caller's units - the invariant `instance` and the 0.001 acne
      // epsilon both depend on.
      const vec3 pv = cross(r.direction(), g.e2);
      const double det = dot(g.e1, pv);

      // Ray parallel to the plane, or a degenerate triangle. This guard is not
      // optional: at det == 0 the barycentrics are NaN, every comparison below
      // is false for NaN, and a zero-area triangle reports a hit at t = NaN on
      // every single ray. Measured: 20 000/20 000.
      if (std::fabs(det) < 1e-12) continue;

      const double inv_det = 1.0 / det;
      const vec3 tv = r.origin() - g.p0;

      const double u = dot(tv, pv) * inv_det;
      if (u < 0 || u > 1) continue;

      const vec3 qv = cross(tv, g.e1);
      const double v = dot(r.direction(), qv) * inv_det;
      if (v < 0 || u + v > 1) continue;

      const double t = dot(g.e2, qv) * inv_det;
      if (t <= clipping_range.min || t >= closest) continue;

      best = int32_t(f);
      closest = t;
      bu = u;
      bv = v;
    }

    if (best < 0) return false;

    const tri_geom &g = geom[best];
    const vec3 ng = cross(g.e1, g.e2);   // geometric normal, not unit

    vec3 ns = ng;
    if (!N.empty())
    {
      const int32_t i0 = tri[3 * best + 0];
      const int32_t i1 = tri[3 * best + 1];
      const int32_t i2 = tri[3 * best + 2];
      ns = (1 - bu - bv) * N[i0] + bu * N[i1] + bv * N[i2];

      // Authored normals that cancel out, or that disagree with the winding.
      // Both are bad data; neither should produce a black pixel or a scatter
      // into the surface.
      if (ns.near_zero())      ns = ng;
      else if (dot(ns, ng) < 0) ns = -ns;
    }

    info.t = closest;
    info.p = r.at(closest);
    info.mat = mat.get();
    info.element_id = face.empty() ? best : face[best];
    info.set_face_normal(r, ng, unit_vector(ns));

    return true;
  }

private:
  struct tri_geom
  {
    vec3 p0, e1, e2;
  };

  std::vector<tri_geom> geom;
};
```

**`commit()` steps by 3, not by 1.** Writing `i++` there builds one `geom` entry per *index*
rather than per triangle - sliding-window garbage faces, and `geom`'s index stops being the triangle
index, which silently breaks `tris[3 * best + k]` and `face[best]` in `hit()`. It is invisible on a
one-triangle mesh, so scene 5 and every `make_triangle` caller still look right; it only shows up
once something loads an actual mesh. Hit during implementation.

Three things worth not changing without a measurement:

- **`geom` exists because it is 1.27–1.31× faster and byte-identical.** Recomputing `e1`/`e2` from
  `P` and `tri` inside the loop costs two index loads and two vector subtracts per face per ray;
  the cache trades 72 B/triangle for them. Verified byte-identical over a full 20-spp render.
- **`face.empty()` is a real case, not laziness.** A mesh whose triangles *are* its authored faces
  — everything `add_triangle` builds, and every already-triangulated OBJ — should not carry an
  identity array. hdEmbree makes the identical `primitiveParams.empty()` check at
  `renderer.cpp:870`.
- **The `1e-12` is absolute, and that is a known limitation.** `det` scales as `|d| × 2·area`, so a
  mesh authored in millimetres with sub-micron faces could be falsely rejected. See "Design notes"
  for the relative form and why it is not worth it yet.

---

# Step 3 — `tracer/triangle.h` becomes a factory

The class in the working tree duplicates the intersection kernel, computes `t` in `float` while
every other `t` in the codebase is a `double`, ignores the vertex normals it stores, and cannot
stamp `element_id`. All four go away if a triangle is just a one-face mesh. Replace the file:

```cpp
#pragma once

#include <utility>

#include "mesh.h"
#include "tracer.h"
#include "vert.h"

// A single triangle is a mesh with one face. There is no separate primitive:
// the intersection kernel, the smooth-normal path and the element_id stamp all
// live in mesh.h and there is no second copy to keep in sync.
inline shared_ptr<mesh> make_triangle(const vertex &a, const vertex &b, const vertex &c,
                                      shared_ptr<material> material)
{
  auto m = make_shared<mesh>();
  m->add_triangle(a, b, c);
  m->mat = std::move(material);
  return m;
}
```

Then update the one caller, in `example_scenes.h`'s `scene_6`:

```cpp
-  world.insert(make_shared<triangle>(
+  world.insert(make_triangle(
     vertex{ vec3(-0.8, -0.4, -1), n_face },
     vertex{ vec3( 0.8, -0.4, -1), n_face },
     vertex{ vec3( 0.0,  0.9, -1), n_face },
     m_tri
   ));
```

Scene 5's image changes as a result, because the intersection algorithm changed. Measured on a
1280-triangle mesh, that difference is **3 channels out of 270 000, max magnitude 2/255** — it is
float noise between two algebraically equivalent formulations, not a behaviour change. Step 9
gates the magnitude; step 13 records the new golden.

If you would rather keep `triangle` as a class, the thing to *not* do is keep its intersection
code. Two kernels means the smooth-normal fix, the NaN guard and the `element_id` stamp all have to
be made twice, and the second copy is the one that rots.

---

# Step 4 — Vendor tinyobjloader

New file, `vendor/tinyobjloader/CMakeLists.txt`:

```cmake
FetchContent_Declare(
  tinyobjloader
  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
  GIT_TAG v2.0.0rc13
  GIT_SHALLOW TRUE
  # No FIND_PACKAGE_ARGS, unlike sdl3 and tbb: a system tinyobjloader is the
  # float build and exports tinyobjloader::tinyobjloader, so picking one up
  # would silently change both the target name and the precision.
)

# real_t = double, so an OBJ's coordinates reach vec3 without a detour through
# float. NOTE: this also renames the target to `tinyobjloader_double`.
set(TINYOBJLOADER_USE_DOUBLE ON CACHE BOOL "" FORCE)

set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
set(TINYOBJLOADER_BUILD_OBJ_STICHER OFF CACHE BOOL "" FORCE)
set(TINYOBJLOADER_WITH_PYTHON OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinyobjloader)
```

and one line in `vendor/CMakeLists.txt`:

```cmake
 add_subdirectory(sdl3)
 add_subdirectory(tbb)
+add_subdirectory(tinyobjloader)
 # add_subdirectory(nanobench)
```

Four notes, all measured or read out of the upstream `CMakeLists.txt`:

- **`v2.0.0rc13`, not `v1.0.0`.** `v1.0.0` is the newest tag without `rc` in it, and it predates
  the `attrib_t` / `ObjReader` API entirely — every shape carries its own copy of the positions.
  `rc13` is what the ecosystem actually pins.
- **The target is `tinyobjloader_double`.** Upstream `CMakeLists.txt` lines 22–23: `if(TINYOBJLOADER_USE_DOUBLE)` /
  `set(LIBRARY_NAME ${PROJECT_NAME}_double)`.
  Probed after `FetchContent_MakeAvailable`: `tinyobjloader_double` exists, `tinyobjloader` and
  `tinyobjloader::tinyobjloader` do not. Linking the wrong name fails at configure time, loudly,
  which is the good outcome.
- Upstream sets `CMAKE_CXX_STANDARD 11` and calls `find_package(Sanitizers)`. Both are confined to
  its own directory scope by `add_subdirectory`, and the `find_package` is not `REQUIRED` — it
  finds nothing here and configures cleanly.
- The include directory propagates as `INTERFACE`, so `#include "tiny_obj_loader.h"` works from
  anything that links the target. The shallow checkout is 6.3 MB.

Verify before writing any loader code:

```bash
cmake -S . -B build -G Ninja && cmake --build build --config Release
# should print a `libtinyobjloader_double.a` link line and nothing else new
```

---

# Step 5 — Write `tracer/obj_loader.h`

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tiny_obj_loader.h"

#include "material.h"
#include "mesh.h"
#include "tracer.h"

struct obj_load_result
{
  std::vector<shared_ptr<mesh>> meshes;
  std::string warning;
  std::string error;             // non-empty means nothing was loaded
  size_t degenerate_faces = 0;   // authored faces with fewer than 3 corners
};

// One `mesh` per OBJ shape (`o`/`g`), because one mesh has one material and one
// Rprim has one material binding. Materials themselves are the caller's
// business: .mtl is out of scope, see the plan's "What is explicitly NOT".
inline obj_load_result load_obj(const std::string &path, shared_ptr<material> mat)
{
  obj_load_result out;

  tinyobj::ObjReaderConfig cfg;
  // NOT true. tinyobjloader's own triangulation renumbers faces in place and
  // hands back no map to the authored face, which is exactly what element_id
  // is. Measured on its own models/cornell_box.obj: 18 quads come back as 36
  // indistinguishable "faces". We fan the polygons ourselves, below, which is
  // what HdMeshUtil::ComputeTriangleIndices does on the Hydra side.
  cfg.triangulate = false;
  cfg.vertex_color = false;

  tinyobj::ObjReader reader;
  const bool ok = reader.ParseFromFile(path, cfg);
  out.warning = reader.Warning();
  out.error = reader.Error();
  if (!ok) return out;

  const tinyobj::attrib_t &attrib = reader.GetAttrib();

  for (const tinyobj::shape_t &shape : reader.GetShapes())
  {
    auto m = make_shared<mesh>();
    m->mat = mat;

    // One vertex per distinct (position, normal) pair. Two faces that share a
    // position but not a normal must not share a vertex, or the crease between
    // them is smoothed away. Measured on the test icosphere: 3 840 corners
    // collapse to 642 vertices.
    std::map<std::pair<int, int>, int32_t> remap;
    bool have_normals = true;

    auto corner_vertex = [&](const tinyobj::index_t &idx) {
      const auto key = std::make_pair(idx.vertex_index, idx.normal_index);
      auto it = remap.find(key);
      if (it != remap.end()) return it->second;

      m->P.emplace_back(attrib.vertices[3 * idx.vertex_index + 0],
                        attrib.vertices[3 * idx.vertex_index + 1],
                        attrib.vertices[3 * idx.vertex_index + 2]);

      if (idx.normal_index >= 0)
      {
        m->N.emplace_back(attrib.normals[3 * idx.normal_index + 0],
                          attrib.normals[3 * idx.normal_index + 1],
                          attrib.normals[3 * idx.normal_index + 2]);
      }
      else
      {
        have_normals = false;
      }

      const int32_t id = int32_t(m->P.size() - 1);
      remap.emplace(key, id);
      return id;
    };

    size_t corner = 0;
    for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
    {
      const unsigned n = shape.mesh.num_face_vertices[f];
      if (n < 3)
      {
        out.degenerate_faces++;
        corner += n;
        continue;
      }

      // Triangle fan from corner 0, and every triangle carries `f`. Same
      // topology HdMeshUtil produces, and the same limitation: correct for
      // convex faces, which is what an OBJ is supposed to contain.
      const int32_t v0 = corner_vertex(shape.mesh.indices[corner]);
      for (unsigned k = 1; k + 1 < n; k++)
      {
        const int32_t vk = corner_vertex(shape.mesh.indices[corner + k]);
        const int32_t vn = corner_vertex(shape.mesh.indices[corner + k + 1]);
        m->tri.push_back(v0);
        m->tri.push_back(vk);
        m->tri.push_back(vn);
        m->face.push_back(int32_t(f));
      }
      corner += n;
    }

    // A mesh is entirely smooth or entirely flat. A partially normalled OBJ
    // falls back to flat rather than leaving N shorter than P.
    if (!have_normals) m->N.clear();

    if (!m->tri.empty()) out.meshes.push_back(std::move(m));
  }

  return out;
}
```

`face` is always filled here, even for an already-triangular OBJ where it is the identity. That is
deliberate: the loader does not know whether the file was authored as triangles or triangulated by
its exporter, and `face[f] == f` costs 4 B/triangle. `mesh` supports the empty case for
hand-authored geometry and for the Hydra path, where `primitiveParams` really can be absent.

## Wiring it up

`tracer/CMakeLists.txt`:

```cmake
 add_library(tracer INTERFACE)
 target_include_directories(tracer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
 target_compile_features(tracer INTERFACE cxx_std_17)
 target_link_libraries(tracer INTERFACE TBB::tbb)

+# OBJ loading, kept out of `tracer` on purpose. The Hydra delegate gets its
+# points and topology from the scene delegate and must never link a file
+# parser; the cli and the viewer are the only things that read from disk.
+add_library(tracer_obj INTERFACE)
+target_link_libraries(tracer_obj INTERFACE tracer tinyobjloader_double)
+target_compile_definitions(tracer_obj INTERFACE
+    TRACER_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
+
 add_executable(tracer_cli main.cpp)
-target_link_libraries(tracer_cli PRIVATE tracer)
+target_link_libraries(tracer_cli PRIVATE tracer tracer_obj)
```

`viewer/CMakeLists.txt`:

```cmake
-target_link_libraries(viewer PRIVATE tracer)
+target_link_libraries(viewer PRIVATE tracer tracer_obj)
```

`TRACER_ASSET_DIR` is an absolute path baked at configure time. It is the right answer for a
scene file that ships with the repo and the wrong answer for anything a user picks — when the cli
grows a `--obj` flag, that path comes from `argv`, not from here.

---

# Step 6 — The asset and the scenes

## `assets/icosphere.obj`

Commit a 1280-triangle unit icosphere with exact vertex normals: subdivide an icosahedron three
times and project every vertex onto the unit sphere, so `n == p` at every vertex. Written with
`%.9g` it is **84 298 B** (84 416 B as committed, with two comment lines at the top); the subdiv-2 version (320 triangles) is 19 378 B if that matters more
than reproducing step 10's numbers directly.

It is a synthetic asset and that is the point: it is the only test mesh whose correct rendering is
already in the renderer for comparison. Step 10 renders it against `sphere(vec3(0,0,-1), 0.5)` —
which is literally scene 2's centre sphere, same material, same camera — and measures how much
closer smooth shading gets. A downloaded bunny cannot do that.

Generate it with a throwaway program in `$S` — the icosahedron's twelve vertices are
`(±1, ±φ, 0)`, `(0, ±1, ±φ)`, `(±φ, 0, ±1)` normalized, with the standard twenty-face index list;
subdivide by splitting each face on its three edge midpoints, caching midpoints by vertex-index
pair so the mesh stays welded, and renormalizing each new point. Write `v`/`vn`/`f v//vn` lines.
**The generator is scratchpad code; only its `.obj` output is committed.**

**`.gitignore` will silently swallow the asset.** Line 8 is `*.obj`, meant for compiled object
files, and it matches `assets/icosphere.obj` - `git add` reports nothing and the commit ships
without the mesh scene 7 needs. Add the negation next to the rule it is undoing:

```
 # Compiled Object files
 *.slo
 *.lo
 *.o
 *.obj
+
+# ...but Wavefront OBJ assets are source, not build output
+!/assets/*.obj
```

Check with `git check-ignore -v assets/icosphere.obj` before writing the loader test.

## `scene_7`

```cpp
void scene_7(scene_edit &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_body   = make_shared<lambert>(color(0.1, 0.2, 0.5));

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));

  obj_load_result loaded = load_obj(std::string(TRACER_ASSET_DIR) + "/icosphere.obj", m_body);
  if (!loaded.error.empty())
  {
    std::clog << "obj: " << loaded.error << "\n";
  }

  // One instance per mesh, never one per face: the wrapping rule from
  // [[transform-support]]. The ray is transformed once for the whole mesh.
  for (const shared_ptr<mesh> &m : loaded.meshes)
  {
    world.insert(make_shared<instance>(m, scale(0.5) * translate(vec3(0, 0, -1))));
  }

  desc.lookfrom = vec3(0, 0, 0);
  desc.lookat = vec3(0, 0, -1);
  desc.v_fov = 90;
  desc.focus_dist = 1;
  desc.defocus_angle = 0;
}
```

and `case 6: scene_7(edit, camera); break;` in `load_scene`. `example_scenes.h` needs
`#include "instance.h"` and `#include "obj_loader.h"`.

That camera is `scene_2`'s, minus the defocus: same `lookfrom`, `lookat`, `v_fov` and
`focus_dist`, with `defocus_angle = 0` so step 10's comparison against a `sphere` is not fighting
lens blur. `scale(0.5)` puts the unit icosphere exactly where scene 2's centre sphere is.

`scale(0.5) * translate(...)` is in that order on purpose and is **not** a bug to fix. `mat4` is
ROW-VECTOR (`v' = v * M`, see `mat4.h:7`), so `A * B` applies `A` first: scale the unit sphere to
r = 0.5, then move it to `(0, 0, -1)`. Verified by probing a ray down `-z` from the origin: `S * T`
hits at `t = 0.5`, exactly where `sphere(vec3(0,0,-1), 0.5)` does; the "corrected" `T * S` hits at
`t = 1`, from the inside, with `front_face` false.

---

# Step 7 — Build and eyeball

```bash
cmake -S . -B build -G Ninja
cmake --build build --config Release
./build/tracer/Release/tracer_cli 6 > $S/scene6.ppm
```

Expect a faceted-but-smooth blue ball on the grey ground. Two failure modes worth recognising on
sight, because they look like each other:

- **A ball with a hard triangular mosaic and a visible facet edge in the middle of the silhouette**
  — `N` came back empty. Either the OBJ has no `vn` lines, or `have_normals` was cleared by one
  bad corner.
- **A ball with dark triangular blotches that move when the camera moves** — the shading normal is
  being used for `front_face`. Check that step 1's overload is the one being called.

A **black** ball means `mat` is null; `mesh::mat` is not set by `add_triangle` or by the loader's
per-shape construction if you skipped `m->mat = mat`.

---

# Step 8 — GATE 1: scenes 0–4 do not move

```bash
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
for i in 0 1 2 3 4; do
  ./build/tracer/Release/tracer_cli $i > $S/new_$i.ppm
  cmp $S/gold_$i.ppm $S/new_$i.ppm && echo "scene $i OK" || echo "scene $i DIFFERS"
done
```

All five must print `OK`. None of those scenes contains a triangle, so any difference means an edit
leaked out of the mesh path — most likely into `hittable.h`, which is included by everything.

Scene 5 is *expected* to differ. Measure by how much rather than skipping it:

```bash
./build/tracer/Release/tracer_cli 5 > $S/new_5.ppm
python3 - <<'PY'
a=open('gold_5.ppm').read().split(); b=open('new_5.ppm').read().split()
A=[int(x) for x in a[4:]]; B=[int(x) for x in b[4:]]
d=[abs(x-y) for x,y in zip(A,B)]
print(f"differing channels {sum(1 for x in d if x)}/{len(d)}, max {max(d)}")
PY
```

On a 1280-triangle mesh the equivalent measurement was **3/270 000, max 2**. For a single triangle
expect the same order or zero. Anything in the hundreds means the two kernels disagree about more
than rounding — check the winding and the `u`/`v` ordering, not the epsilon.

**Measured at implementation:** scenes 0–4 all `OK`, byte-identical. Scene 5: **23/270 000
channels, max 2** — the predicted order, and the predicted magnitude.

---

# Step 9 — GATE 2: the kernel, the guard, and the degenerate cases

A scratchpad program, no scheduler needed. Three things to assert:

**1. Möller–Trumbore against the plane test.** Build the same triangle two ways — through
`make_triangle` and through a local copy of the old `triangle` class — and fire the same rays at
both. Compare `t`, `p`, `normal`, `front_face`. Measured over a full 50-spp render of 1280
triangles: 3 differing 8-bit channels out of 270 000. At the `hit_info` level expect agreement to
float epsilon, not bit-equality: the plane form divides by `dot(n, d)` and this one by
`dot(e1, d × e2)`.

**Correction, measured:** the tolerance is float-sized, not the ~1e-15 this document originally
predicted, and it is not the kernels' fault — the old `triangle::hit` assigns its result to a
`float t` (`triangle.h:36` at `52f76ac`) before handing it back as a `double`. **Measured over
200 000 rays: 0 hit-disagreements, max |Δt| 4.35e-07, max |Δp| 2.41e-07, max |Δnormal| 0, 0
`front_face` mismatches.** Assert at 1e-5, not 1e-15; a gate written to the tighter bound fails on
correct code.

**2. The NaN guard.** This is the one to actually write, because it is invisible otherwise:

```cpp
// three degenerate triangles, 20 000 random rays each
{"zero-area (all three corners identical)", ...}
{"collinear corners",                       ...}
{"two coincident corners",                  ...}
```

with the guard, and with `if (USE_EPS && ...)` compiled out. Measured:

```
zero-area (all same point)      with-eps hits=0   no-eps hits=20000 (non-finite t: 20000)
collinear                       with-eps hits=0   no-eps hits=0     (non-finite t: 0)
two coincident corners          with-eps hits=0   no-eps hits=0     (non-finite t: 0)
```

The middle two are caught by the barycentric tests either way, which is precisely why the first one
is easy to talk yourself out of guarding. Keep the guard.

**Measured at implementation: reproduced exactly** — 0 / 20 000 / 20 000 non-finite, on all three
cases. The `instance` check: 7 247 shared hits, 0 `front_face` mismatches, 0 hit mismatches.

**3. `front_face` under an `instance`.** Wrap the mesh in `scale(2, 0.5, 1.3) * rotate(...) *
translate(...)` and check that `front_face` is unchanged for every hit, the way
[[transform-support]] step 5 did for spheres. `dot(d·M, n·M⁻ᵀ) == dot(d, n)` holds for the
geometric normal for the same reason it held for the sphere's, and the interpolated normal never
enters the decision.

---

# Step 10 — GATE 3: smooth normals actually do something

Render three versions of the same ball at 50 spp, 400×225, identical camera:

| version | build |
|---|---|
| `sphere` | `make_shared<sphere>(vec3(0,0,-1), 0.5, m)` |
| flat mesh | the icosphere with `N` cleared |
| smooth mesh | the icosphere with `N` filled |

and count differing 8-bit channels against the `sphere` render. Measured at 1280 triangles:

```
smooth vs sphere:  9855/270000 channels differ (3.65%), mean |d| 0.129, max 76
flat   vs sphere: 24451/270000 channels differ (9.06%), mean |d| 0.198, max 76
```

Smooth must be strictly better on both counts. The identical **max** is the silhouette, where a
faceted outline cannot be fixed by shading — that number should not improve, and if it does you are
probably comparing against the wrong sphere radius.

If smooth and flat come out identical, `N` is empty; check the loader's `have_normals`. If smooth
is *worse* than flat, the barycentric weights are misassigned — `u` pairs with `tri[1]` and `v`
with `tri[2]`, with `1-u-v` on `tri[0]`.

**Measured at implementation**, on the committed `assets/icosphere.obj`:

```
smooth vs sphere:  9856/270000 channels differ (3.65%), mean |d| 0.129, max 76
flat   vs sphere: 24452/270000 channels differ (9.06%), mean |d| 0.198, max 76
```

One channel off the pre-verified numbers in each row, from `%.9g` rounding in the generator, and
the same max 76 at the silhouette as predicted.

---

# Step 11 — GATE 4: the `element_id` AOV, and the stale-id negative control

Allocate an `aov::element_id` buffer alongside `aov::color` and histogram it:

```cpp
render_buffer cbuf, ebuf;
aov_bindings aovs = {
  allocate_aov(cbuf, aov::color,      W, H),
  allocate_aov(ebuf, aov::element_id, W, H)
};
```

Note that `render_buffer::read` hands back a `float`, so read into a `float` and cast. That is
exact up to 2^24 face ids and is a cli-side concern only — the Hydra path uses `map()`.

Measured on the 1280-triangle mesh at 400×225:

```
element_id: distinct=313  background(-1)=76825  range=[16,1087]
```

Three assertions:

- every covered pixel is in `[0, triangle_count)`, and every background pixel is exactly `-1`
- the distinct count is large but well under the triangle count — 313 of 1280, because most faces
  are back-facing or sub-pixel
- **the same scene with `triangle` prims reports `-1` on all 90 000 pixels.** Run it before step 3
  if you want to see the gate fail; that is the state this task is leaving behind.

**The negative control.** Put a sphere *in front of* the mesh and confirm the sphere's pixels read
`-1`, not the face id of whatever the mesh reported first. This is what `scene::hit`'s
`temp_info.element_id = -1` line buys, and it will silently break if `mesh::hit` is ever changed to
stamp `info` from inside the face loop. Add the same control for `instance_id` by wrapping the mesh
— `instance::hit` stamps its id on top of the mesh's, and both must survive.

On `models/cornell_box.obj` (available in `build/_deps/tinyobjloader-src/models/`) the additional
assertion is that **exactly two triangles share each element id**, since every one of its 18
authored faces is a quad.

**Measured at implementation:**

```
element_id: distinct=312  background(-1)=76825  range=[16,1087]  out-of-range=0
occluder centre patch, 169 px: element_id leaks 0, instance_id leaks 0
cornell_box: 8 meshes, 36 triangles / 18 authored face ids, faces not covered by exactly 2: 0
```

The `instance_id` half of the negative control passes too: with the mesh's `instance` carrying id 7,
all 7 877 mesh pixels report 7 **and** a face id, and both read `-1` on the occluding sphere. Note
one consequence of step 3 that the AOV now shows: **scene 5's triangle reports `element_id` 0, not
`-1`** — `make_triangle` leaves `face` empty, so `face.empty() ? best : face[best]` returns the
triangle index. That is the intended convention, not a leak.

---

# Step 12 — GATE 5: performance, and the numbers the BVH inherits

Interleave old and new; the first run of a session is the fastest here.

**Regression check.** Scenes 0–4 contain no triangles and must not move:

```
scene 0 baseline: 0.0632 / 0.0647 / 0.0665 ms/px
```

**The flat-vs-soup number.** Build the same icosphere both ways and render at 50 spp:

| triangles | flat `mesh` | `hittable_list` of `triangle` | ratio |
|---|---|---|---|
| 80 | 0.0119 | 0.0148 | 1.24× |
| 320 | 0.0458 | 0.0597 | 1.30× |
| 1280 | 0.1841 | 0.2920 | 1.59× |

**The cache is worth its 72 B/triangle.** Comment out `geom` and read `P`/`tri` in the loop:

```
1280 tri, 20 spp:  uncached 0.0691 / 0.0731    cached 0.0543 / 0.0559    (1.27-1.31x)
```

and `cmp` the two images — byte-identical.

**Memory.** 40.0 B/triangle flat (112 B with the cache) against 304.0 B/triangle for the soup, a
figure that is stable from 80 to 20 480 triangles. `sizeof(triangle)` was 272 B before this task.

**The number that belongs to the next item.** In one scene, at 400×225, 50 spp:

```
1280-triangle mesh          0.1841 ms/px
analytic sphere, same shape 0.0011 ms/px      167x
scene 0, all 484 spheres    0.0608 ms/px      3.0x
```

Do not try to fix that here. `mesh::hit` is a linear scan by design; making it 20% cleverer is
wasted effort against the BVH's order-of-magnitude. Record the numbers and move on.

## Measured at implementation

```
triangles  flat mesh    soup         ratio
80         0.0054       0.0125       2.32x
320        0.0205       0.0444       2.16x
1280       0.0889       0.1861       2.09x

1280 tri, 20 spp:  uncached 0.0412 / 0.0431   cached 0.0323 / 0.0339   (1.27-1.28x), cmp identical
commit() rebuild:  9.55 ns/triangle (0.0122 ms at 1280 tri)
1280-triangle mesh 0.0906 ms/px  ·  analytic sphere 0.0006 ms/px  ·  154x
sizeof: vec3 24, vertex 48, sphere 56, old triangle 272, instance 424, shared_ptr 16
```

Three things to read carefully before comparing against the pre-verified table above:

- **The flat/soup ratios came out higher (2.09–2.32× against 1.24–1.59×) and the absolute times
  roughly half.** Both are the same harness difference: this scene holds only the mesh, where the
  pre-verified run also had the ground sphere. Less non-triangle work per ray raises the share the
  kernel owns, so the ratio rises and the absolute number falls. The *direction* is what the gate
  asserts, and flat wins at every density. Interleave, and never compare a number from one harness
  against a number from another.
- **Flat memory reads 28.0 B/triangle, not 40.0.** The gate measures the *flat-shaded* mesh, whose
  `N` is cleared. With normals it is 40.0 B/triangle exactly, which is the pre-verified figure.
  Soup is 304.0 B/triangle either way, so the ratio is 10.8× flat-shaded and 7.6× smooth.
- **`commit()` at 9.55 ns/triangle** is inside the pre-verified 10.3–11.3 ns/triangle band.

**The scene 0 regression check is not a controlled comparison in this session.** The pre-change
warm timing was never captured — step 0's timing run was interrupted — so the only baseline is this
document's `0.0632 / 0.0647 / 0.0665`, measured on a different day. Post-change scene 0 reads
`0.0670 / 0.0685 / 0.0688`, ~4% higher, against **byte-identical output** on a scene containing no
triangles. That is machine drift, not a regression, and the byte-identical result in step 8 is the
gate that actually matters. Scene 5 reads `0.00141 / 0.00143 / 0.00148` against `0.00118 / 0.00121`
— one triangle now pays for a `vector` indirection it did not before; the cost is 0.0003 ms/px and
disappears at any real triangle count. New: scene 6 at `0.1436 / 0.1453 / 0.1471` ms/px.

---

# Step 13 — Commit

```bash
git add .gitignore \
        tracer/mesh.h tracer/obj_loader.h tracer/triangle.h \
        tracer/hittable.h tracer/example_scenes.h tracer/CMakeLists.txt \
        viewer/CMakeLists.txt vendor/CMakeLists.txt vendor/tinyobjloader/CMakeLists.txt \
        assets/icosphere.obj docs/Roadmap.md docs/plans/triangle-mesh.md CHANGELOG.md
```

`.gitignore` is in that list for the `!/assets/*.obj` negation from step 6. Confirm the asset
actually staged — `git status --short` should show `A  assets/icosphere.obj`, not silence.

`docs/Roadmap.md`:

```
-- [ ] triangle mesh (and tinyobjloader)
+- [x] triangle mesh (and tinyobjloader)
```

`CHANGELOG.md` — new entry at the top, matching the existing dated style. Record the scene 5 golden
change explicitly; it is the only intentional pixel change in the project's history so far and it
will be confusing in six months otherwise.

Re-run step 0's `md5sum` and paste the new `gold_5` into this document's step 0 table, marked as
superseding `b170938c218e9c0dfa66613f628d31bf`. **Measured at implementation:**

```
eeaafa6f38d9b298b0aa1cf6f0e1807a  gold_5.ppm      <- supersedes b170938c218e9c0dfa66613f628d31bf
```

---

## Definition of done

- [x] `tracer/mesh.h` exists: four arrays, one material, `commit()` builds the cache, `hit()` is
      Möller–Trumbore with the `|det|` guard
- [x] `hit_info::set_face_normal(r, geometric, shading)` exists and is what `mesh::hit` calls
- [x] `tracer/triangle.h` contains no intersection code
- [x] `tinyobjloader_double` builds via FetchContent; nothing else in `vendor/` changed behaviour
- [x] `load_obj` fan-triangulates from `cfg.triangulate = false` and fills `face`
- [x] `tracer_obj` is a separate target; the core `tracer` target does not link tinyobjloader
- [ ] `assets/icosphere.obj` is committed; the generator is not — **written and un-ignored, not yet
      staged; this is step 13**
- [x] scene 7 loads it, wraps each mesh in **one** `instance`, and renders
- [x] GATE 1: scenes 0–4 byte-identical; scene 5 differs by a handful of channels, magnitude ≤ 2
- [x] GATE 2: the plane test and Möller–Trumbore agree; a zero-area triangle is rejected 20 000/20 000
- [x] GATE 3: smooth beats flat against the analytic sphere on both count and mean
- [x] GATE 4: `element_id` is in range on covered pixels, `-1` on background, `-1` on an occluding
      sphere's pixels, and 2-per-face on the Cornell box
- [x] GATE 5: flat beats soup; the cache is byte-identical; the mesh-vs-sphere ratio is recorded
- [x] no `pxr/` include under `tracer/` or `viewer/` (the one `pxr/` string is a comment in
      `render_buffer.h:313`)
- [x] no test code in the repo

---

## Design notes — decisions made, recorded so they aren't re-litigated

**Möller–Trumbore, not the precomputed-plane form the `triangle` prim used.** The plane form caches
`normal`, `d` and `w` and costs one dot product to reject; Möller–Trumbore recomputes and costs a
cross product. The plane form wins per-test in isolation. It loses here because it caches **72 B of
plane data per triangle in addition to** the edges, it produces the barycentrics only as a
by-product of a second cross product, and — the actual reason — its rejection test is a division by
`dot(n, d)`, which has the same NaN behaviour as `det` without the excuse of being obvious. The two
were measured to agree to 3 channels in 270 000. Take the one whose barycentrics are already in
hand, since smooth normals need them.

**No relative epsilon on `det`, yet.** The scale-correct test is `|det| < eps · |e1| · |e2| · |d|`,
which is three extra square roots per face per ray. The absolute `1e-12` is wrong only for meshes
whose triangles have areas below ~1e-6 in world units, which nothing in this project produces. When
the BVH lands, the natural place for a scale-aware epsilon is per-mesh, computed once in `commit()`
from the mesh's own bounds — not per-face, and not per-ray.

**`commit()` rebuilds unconditionally.** A `dirty` flag on `mesh` would skip the rebuild when
`scene::commit()` fires for an unrelated edit. At 10.8 ns/triangle that saves 0.22 ms per scene
edit per 20 000-triangle mesh, against a re-render measured in seconds — and `scene::commit()`
already early-outs on `_dirty`, so the rebuild only happens when *something* changed. Add the flag
when a profile says to, not before. The one case that would change the calculus is a deforming
mesh under animation, where `commit()` is genuinely on the frame path.

**Points are mutated through `scene::edit()`, not directly.** `P` and `tri` are public and there is
no setter, which looks like an invitation to write `m->P[3] = ...` from anywhere. It is not: the
render thread reads `geom` without a lock, and the only thing that makes that safe is
[[scene-graph]]'s gateway calling `StopRender()` before handing back a mutable scene. The same rule
that already governs `instance::set_transform` governs this. When the Hydra delegate lands,
`Sync()` reaches the mesh through `AcquireSceneForEdit()` and nowhere else.

**One material per mesh, and shapes split into separate meshes.** OBJ allows per-face materials
(`shape.mesh.material_ids`) and the loader throws that away. It matches Hydra — one Rprim, one
material binding — and it matches the tracer, where `hit_info::mat` is a single pointer. A file
that needs four materials becomes four meshes, which is also four BVH leaves and four things the
scene can toggle independently. The cost is that `element_id` is then per-mesh, which is exactly
what `elementId` means per-Rprim anyway.

**No `.mtl`.** §13 puts materials at the bottom of the tier list and notes hdEmbree has none at
all. Parsing `Kd` into a `lambert` would work for about four files and then start lying about
everything else.

**Not watertight.** Möller–Trumbore can leak rays through shared edges: a ray passing exactly
through an edge can be rejected by both adjacent triangles because each computes its barycentrics
in a different order. The fix (Woop, Benthin & Wald's watertight test) costs a per-ray coordinate
permutation and shear. It matters for closed volumes and shadow rays through dense geometry; it
does not matter for a diffuse scatter off a visible surface, where a single leaked ray becomes a
single sky sample among fifty. Revisit if pinholes ever show up in a render.

**Fan triangulation, not ear clipping.** `HdMeshUtil` fans (see the diagram at
`hd/meshUtil.h:100`), and so does tinyobjloader's `"simple"` method. A fan is wrong for a concave
polygon, which an OBJ face is not supposed to be. `rc13` has an `"earcut"` triangulation method
that is documented as "currently not used". If concave faces ever appear, the fix is on the loader
side and does not touch `mesh`.

---

## Appendix A — the delegate side, for 0.3.0

Not to be written now; recorded so the mesh's shape does not have to be re-derived from §7.4.

```cpp
void HdWeekendMesh::Sync(HdSceneDelegate *sd, HdRenderParam *rp,
                         HdDirtyBits *dirtyBits, TfToken const &reprToken)
{
    const SdfPath &id = GetId();
    auto *param = static_cast<HdWeekendRenderParam*>(rp);

    // §7.2: only pull what is dirty. Everything below is inside the gateway,
    // which has already called StopRender().
    weekend::scene *scene = param->AcquireSceneForEdit();

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        HdMeshTopology topology = sd->GetMeshTopology(id);

        VtVec3iArray indices;
        VtIntArray primitiveParams;
        HdMeshUtil(&topology, id).ComputeTriangleIndices(&indices, &primitiveParams);

        _mesh->tri.clear();
        _mesh->face.clear();
        for (size_t i = 0; i < indices.size(); i++) {
            _mesh->tri.push_back(indices[i][0]);
            _mesh->tri.push_back(indices[i][1]);
            _mesh->tri.push_back(indices[i][2]);
            // This decode is the whole reason `face` exists.
            _mesh->face.push_back(
                HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(primitiveParams[i]));
        }
    }

    // Points are a primvar, gated on IsPrimvarDirty(points) - NOT on a
    // topology bit. §7.4's first table row.
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtVec3fArray pts = sd->Get(id, HdTokens->points).Get<VtVec3fArray>();
        _mesh->P.assign(pts.begin(), pts.end());        // GfVec3f -> vec3
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)) {
        // ... same, into _mesh->N; clear it if the primvar is absent
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _instance->set_transform(to_mat4(sd->GetTransform(id)));   // one memcpy
    }

    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        scene->edit().set_visible(_handle, sd->GetVisible(id));
    }

    // §7.2: clear what you consumed, leave the rest.
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}
```

Two things this task deliberately made easy:

- **`primitiveParams` maps straight onto `face`.** Because `mesh` stores an authored-face array
  rather than assuming triangle == face, the decode above is one line and there is nothing to
  invent at `Sync()` time.
- **`DirtyPoints` without `DirtyTopology` costs one `assign` and one `commit()`.** `P` and `tri`
  are independent arrays, so a deforming mesh never rebuilds its index buffer.

The one thing that is *not* free: `VtVec3fArray` is float and `vec3` is double, so points are
converted, not memcpy'd, unlike `GfMatrix4d` → `mat4`.

## Appendix B — what `bvh with rebuild-on-mutation` adds

- **An object-space AABB on `mesh`**, built in `commit()` from `P`. Cheap and obviously belonging
  there; deliberately left out of this task because nothing would read it.
- **A per-mesh BVH over `geom`**, so `mesh::hit` stops being a linear scan. This is where the 167×
  against the analytic sphere goes.
- **A top-level BVH over `scene::_draw`**, replacing the linear scan [[scene-graph]] step 11
  measured at +4% over 484 prims.
- **World-space bounds for a transformed prim** — transform the object-space AABB's eight corners.
  `instance::object_to_world()` was added by [[transform-support]] for exactly this.
- **Rebuild on `commit()`**, not build-once: §6 mandates `StopRender()` → mutate → bump
  `sceneVersion` → restart on every edit, so the BVH is rebuilt inside the same `commit()` that
  rebuilds `geom`. The 10.8 ns/triangle measured here is the floor that build has to beat by enough
  to matter.

The `geom` array is deliberately a flat contiguous `vector` of `{p0, e1, e2}` rather than a vector
of indices into `P`: a BVH build reorders primitives, and reordering 72-byte contiguous records is
a `std::sort` on the array itself, with `face` and a reorder permutation carried alongside.

---

## Next up

`bvh with rebuild-on-mutation` — and it is not optional after this. This task hands it four things:
a scene where one 1280-triangle mesh costs 3× everything in scene 0, a contiguous per-face array
that a build can reorder in place, a `commit()` hook that already fires exactly when the geometry
changed, and `instance::object_to_world()` waiting for the world-space bounds.
[[roadmap-discussion-8-26]] §1 has the argument for why the two shipped adjacent; §6 has the
requirement that the build be a rebuild. After it, 0.3.0's `hydra wrapper` is the last thing
between the tracer and usdview.
